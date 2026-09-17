#include "veyra/source/AverMediaAudioSwitch.h"

#include "veyra/Log.h"

#include <windows.h>

#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace veyra::source {
namespace {

// ---------------------------------------------------------------------------
// ABI mirror of the component's public headers (GPLv2, shipped in
// AVerMedia-Technologies-Inc/obs-MultichannelAudio). Only the layout matters:
// both the component and Veyra are built with the MSVC x64 toolchain, so these
// are plain aggregates with the same member order.
// ---------------------------------------------------------------------------
struct VendorDeviceOpenerParam {
    std::wstring name;  // +0x00
    std::wstring path;  // +0x20
};

using RawFn = long long (*)(void*, void*, void*, void*, void*);

struct RawCall {
    RawFn fn = nullptr;
    void* self = nullptr;
    void* a = nullptr;
    void* b = nullptr;
    void* c = nullptr;
    void* d = nullptr;
    long long result = -1;
    unsigned sehCode = 0;
};

// SEH must live in a function that owns no C++ objects, which is why the
// invocation is split away from everything else. Foreign code can both throw
// (their wrappers use std::string/throw) and fault, so both are handled.
void invokeRaw(RawCall* call) noexcept {
    __try {
        call->result = call->fn(call->self, call->a, call->b, call->c, call->d);
    } __except (call->sehCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        call->result = -1;
    }
}

// Entry points of avt_device_opener.dll, decorated names as exported.
constexpr const char* kExportVendorSdkCtor = "??0VendorSdk@AVerMedia@@QEAA@PEBD@Z";
constexpr const char* kExportVendorSdkDtor = "??1VendorSdk@AVerMedia@@QEAA@XZ";
constexpr const char* kExportSdkInitialize = "?initialize@VendorSdk@AVerMedia@@QEAAHXZ";
constexpr const char* kExportSdkUninitialize = "?uninitialize@VendorSdk@AVerMedia@@QEAAHXZ";
constexpr const char* kExportSdkClosePort = "?closePort@VendorSdk@AVerMedia@@QEAAHXZ";
constexpr const char* kExportSdkGetAudioFormat = "?getAudioFormat@VendorSdk@AVerMedia@@QEAAHPEAH@Z";
constexpr const char* kExportOpenerCtor = "??0DeviceOpener@AVerMedia@@QEAA@XZ";
constexpr const char* kExportOpenerDtor = "??1DeviceOpener@AVerMedia@@QEAA@XZ";
constexpr const char* kExportOpenerSetVendorSdk = "?SetVendorSdk@DeviceOpener@AVerMedia@@QEAAXPEAVVendorSdk@2@@Z";
constexpr const char* kExportOpenerSwitch = "?SwitchDeviceThenDetectAudioFormat@DeviceOpener@AVerMedia@@QEAAXAEBUDeviceOpenerParam@2@@Z";
constexpr const char* kExportOpenerIsNonPcm = "?IsAudioFormatNonPcm@DeviceOpener@AVerMedia@@QEBA_NXZ";
constexpr const char* kExportOpenerStartChecking = "?StartChecking@DeviceOpener@AVerMedia@@QEAAXXZ";
constexpr const char* kExportOpenerStopChecking = "?StopChecking@DeviceOpener@AVerMedia@@QEAAXXZ";

// The chip reports this value while the HDMI source sends a compressed
// (non-PCM) stream; their enable path refuses to switch for anything else.
constexpr int kChipFormatNonPcm = 20;

// Both classes hold a single opaque implementation pointer, but the storage is
// over-sized on purpose: the constructor only writes what it needs, and a
// larger zeroed block keeps a layout surprise from corrupting the stack.
struct ObjectStorage {
    alignas(16) unsigned char bytes[64] = {};
    void* raw() noexcept { return bytes; }
};

std::string narrowUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(size_t(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

bool fileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool directoryExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring registryPathValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return {};
    DWORD type = 0;
    DWORD bytes = 0;
    std::wstring value;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && type == REG_SZ && bytes > sizeof(wchar_t)) {
        value.resize(bytes / sizeof(wchar_t));
        if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS) value.clear();
        while (!value.empty() && value.back() == L'\0') value.pop_back();
    }
    RegCloseKey(key);
    return value;
}

std::wstring joinPath(const std::wstring& base, const wchar_t* relative) {
    if (base.empty()) return {};
    std::wstring out = base;
    if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    out.append(relative);
    return out;
}

bool looksLikeComponent(const std::wstring& root) {
    if (root.empty()) return false;
    return fileExists(joinPath(root, L"obs-plugins\\64bit\\avt_device_opener.dll")) &&
           fileExists(joinPath(root, L"data\\obs-plugins\\AVerMediaMultichannelAudio\\RTICE_SDK_x64.dll")) &&
           fileExists(joinPath(root, L"data\\obs-plugins\\AVerMediaMultichannelAudio\\RTK_IO_x64.dll"));
}

// Qt6Core.dll is what avt_device_opener.dll links against and what OBS ships
// next to its own executable. Loading it first by absolute path keeps the
// dependency from resolving against whatever else happens to be on PATH; we
// never add OBS directories to this process's search path.
std::wstring findQt6Core(const std::wstring& root) {
    const wchar_t* candidates[] = {
        L"bin\\64bit\\Qt6Core.dll",
        L"obs-plugins\\64bit\\Qt6Core.dll",
        L"Qt6Core.dll",
    };
    for (const wchar_t* candidate : candidates) {
        const std::wstring path = joinPath(root, candidate);
        if (fileExists(path)) return path;
    }
    return {};
}

// Applies one vendor call, converting both C++ exceptions and SEH faults into
// a negative result so a foreign component can never take the capture down.
bool callVendor(RawCall& call, std::string& error) {
    if (call.fn == nullptr) {
        error = "entry point missing";
        return false;
    }
    try {
        invokeRaw(&call);
    } catch (...) {
        error = "component raised a C++ exception";
        return false;
    }
    if (call.sehCode != 0) {
        error = std::format("component faulted (SEH 0x{:08X})", call.sehCode);
        return false;
    }
    return true;
}

} // namespace

struct AverMediaAudioSwitch::Impl {
    struct Exports {
        RawFn vendorCtor = nullptr;
        RawFn vendorDtor = nullptr;
        RawFn initialize = nullptr;
        RawFn uninitialize = nullptr;
        RawFn closePort = nullptr;
        RawFn getAudioFormat = nullptr;
        RawFn openerCtor = nullptr;
        RawFn openerDtor = nullptr;
        RawFn setVendorSdk = nullptr;
        RawFn switchDevice = nullptr;
        RawFn isNonPcm = nullptr;
        RawFn startChecking = nullptr;
        RawFn stopChecking = nullptr;
        bool complete() const {
            return vendorCtor && vendorDtor && initialize && uninitialize && closePort && getAudioFormat &&
                   openerCtor && openerDtor && setVendorSdk && switchDevice && isNonPcm &&
                   startChecking && stopChecking;
        }
    };

    AverMediaSwitchStatus status;
    std::wstring componentRoot;   // install root (owns obs-plugins\64bit)
    std::wstring dataDirectory;   // ...\data\obs-plugins\AVerMediaMultichannelAudio
    std::string dataDirectoryUtf8;
    HMODULE avtModule = nullptr;
    HMODULE qtModule = nullptr;
    Exports exports;
    ObjectStorage sdkStorage;
    ObjectStorage openerStorage;
    bool sdkConstructed = false;
    bool openerConstructed = false;
    bool sdkInitialized = false;
    bool checking = false;

    void* sdk() noexcept { return sdkStorage.raw(); }
    void* opener() noexcept { return openerStorage.raw(); }

    bool resolveExports() {
        struct Entry { const char* name; RawFn* target; };
        const Entry entries[] = {
            {kExportVendorSdkCtor, &exports.vendorCtor},
            {kExportVendorSdkDtor, &exports.vendorDtor},
            {kExportSdkInitialize, &exports.initialize},
            {kExportSdkUninitialize, &exports.uninitialize},
            {kExportSdkClosePort, &exports.closePort},
            {kExportSdkGetAudioFormat, &exports.getAudioFormat},
            {kExportOpenerCtor, &exports.openerCtor},
            {kExportOpenerDtor, &exports.openerDtor},
            {kExportOpenerSetVendorSdk, &exports.setVendorSdk},
            {kExportOpenerSwitch, &exports.switchDevice},
            {kExportOpenerIsNonPcm, &exports.isNonPcm},
            {kExportOpenerStartChecking, &exports.startChecking},
            {kExportOpenerStopChecking, &exports.stopChecking},
        };
        for (const Entry& entry : entries) {
            FARPROC address = GetProcAddress(avtModule, entry.name);
            if (address == nullptr) {
                status.detail = std::format("missing export {}", entry.name);
                return false;
            }
            *entry.target = reinterpret_cast<RawFn>(address);
        }
        return true;
    }

    bool loadComponent() {
        if (avtModule != nullptr) return true;
        componentRoot = AverMediaAudioSwitch::locateComponentRoot();
        status.componentRoot = componentRoot;
        status.componentFound = !componentRoot.empty();
        if (!status.componentFound) {
            status.detail = "AVerMedia Multichannel Audio is not installed";
            return false;
        }
        dataDirectory = joinPath(componentRoot, L"data\\obs-plugins\\AVerMediaMultichannelAudio");
        // Qt is optional here: if it is already loaded (or absent because the
        // component does not need it) the loader resolves it on its own.
        const std::wstring qtPath = findQt6Core(componentRoot);
        if (!qtPath.empty()) {
            qtModule = LoadLibraryExW(qtPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        }
        const std::wstring avtPath = joinPath(componentRoot, L"obs-plugins\\64bit\\avt_device_opener.dll");
        avtModule = LoadLibraryExW(avtPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (avtModule == nullptr) {
            status.detail = std::format("LoadLibraryExW failed (error {})", GetLastError());
            if (qtModule != nullptr) { FreeLibrary(qtModule); qtModule = nullptr; }
            return false;
        }
        status.dllsLoaded = true;
        if (!resolveExports()) {
            FreeLibrary(avtModule);
            avtModule = nullptr;
            status.dllsLoaded = false;
            if (qtModule != nullptr) { FreeLibrary(qtModule); qtModule = nullptr; }
            return false;
        }
        return true;
    }
};

AverMediaAudioSwitch::AverMediaAudioSwitch() : p_(std::make_unique<Impl>()) {}
AverMediaAudioSwitch::~AverMediaAudioSwitch() { stop(); }

std::wstring AverMediaAudioSwitch::locateComponentRoot() {
    // Diagnostic override: a user whose component lives outside the OBS default
    // layout can point Veyra at the folder that owns obs-plugins\64bit. The
    // layout is still validated, so a bad value is ignored rather than loaded.
    wchar_t overrideBuffer[32768] = {};
    const DWORD overrideLength = GetEnvironmentVariableW(L"VEYRA_AVERMEDIA_COMPONENT", overrideBuffer, 32768);
    if (overrideLength > 0 && overrideLength < 32768) {
        const std::wstring candidate(overrideBuffer);
        if (looksLikeComponent(candidate)) return candidate;
    }
    std::wstring root = registryPathValue(HKEY_LOCAL_MACHINE,
                                          L"SOFTWARE\\AVerMedia\\OBS_Plugins\\AVerMediaMultichannelAudio", L"Path");
    if (looksLikeComponent(root)) return root;
    // Fall back to the default OBS locations when the registry entry is missing
    // (portable installs, or a plugin folder copied by hand).
    const wchar_t* environmentRoots[] = {L"ProgramFiles", L"ProgramFiles(x86)"};
    for (const wchar_t* variable : environmentRoots) {
        wchar_t buffer[MAX_PATH] = {};
        const DWORD length = GetEnvironmentVariableW(variable, buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) continue;
        for (const wchar_t* suffix : {L"\\obs-studio", L"\\OBS Studio"}) {
            const std::wstring candidate = std::wstring(buffer) + suffix;
            if (looksLikeComponent(candidate)) return candidate;
        }
    }
    return {};
}

bool AverMediaAudioSwitch::isAverMediaDevicePath(std::wstring_view devicePath) {
    std::wstring lowered(devicePath);
    for (wchar_t& character : lowered) character = wchar_t(towlower(character));
    return lowered.find(L"vid_07ca") != std::wstring::npos;
}

bool AverMediaAudioSwitch::apply(const std::wstring& deviceName, const std::wstring& devicePath) {
    auto& impl = *p_;
    if (impl.sdkInitialized && !devicePath.empty()) {
        // Already armed (for example after an audio reconnect); the vendor
        // monitor thread keeps the mode applied, so refreshing the status is
        // enough. This deliberately avoids a second setDevice/setPort cycle.
        RawCall refresh{impl.exports.isNonPcm, impl.opener()};
        std::string refreshError;
        impl.status.nonPcmActive = callVendor(refresh, refreshError) && (refresh.result & 0xFF) != 0;
        return true;
    }
    if (!impl.loadComponent()) {
        impl.status.state = AverMediaSwitchState::Unavailable;
        return false;
    }
    if (!impl.status.sdkReady) {
        impl.dataDirectoryUtf8 = narrowUtf8(impl.dataDirectory);
        // The component keeps no reference to this string; const_cast only
        // satisfies the raw-call signature.
        RawCall ctor{impl.exports.vendorCtor, impl.sdk(), const_cast<char*>(impl.dataDirectoryUtf8.c_str())};
        if (!callVendor(ctor, impl.status.detail)) {
            impl.status.state = AverMediaSwitchState::Failed;
            return false;
        }
        impl.sdkConstructed = true;
        RawCall initialize{impl.exports.initialize, impl.sdk()};
        if (!callVendor(initialize, impl.status.detail) || initialize.result != 0) {
            impl.status.detail = std::format("vendor initialize failed (ret={})", initialize.result);
            impl.status.state = AverMediaSwitchState::Failed;
            return false;
        }
        impl.sdkInitialized = true;
        impl.status.sdkReady = true;
    }
    if (!impl.openerConstructed) {
        RawCall ctor{impl.exports.openerCtor, impl.opener()};
        if (!callVendor(ctor, impl.status.detail)) {
            impl.status.state = AverMediaSwitchState::Failed;
            return false;
        }
        impl.openerConstructed = true;
        RawCall bind{impl.exports.setVendorSdk, impl.opener(), impl.sdk()};
        std::string error;
        if (!callVendor(bind, error)) {
            impl.status.detail = "SetVendorSdk failed: " + error;
            impl.status.state = AverMediaSwitchState::Failed;
            return false;
        }
    }
    VendorDeviceOpenerParam param{deviceName, devicePath};
    RawCall switchCall{impl.exports.switchDevice, impl.opener(), &param};
    if (!callVendor(switchCall, impl.status.detail)) {
        impl.status.state = AverMediaSwitchState::Failed;
        return false;
    }
    int chipFormat = -1;
    RawCall formatCall{impl.exports.getAudioFormat, impl.sdk(), &chipFormat};
    std::string formatError;
    const bool formatOk = callVendor(formatCall, formatError) && formatCall.result == 0;
    impl.status.chipAudioFormat = formatOk ? chipFormat : -1;
    RawCall nonPcmCall{impl.exports.isNonPcm, impl.opener()};
    std::string nonPcmError;
    impl.status.nonPcmActive = callVendor(nonPcmCall, nonPcmError) && (nonPcmCall.result & 0xFF) != 0;
    if (!formatOk) {
        // SwitchDeviceThenDetectAudioFormat returns void and swallows its own
        // errors, so the chip query is the only confirmation we get. If it does
        // not answer, the card was not reached and the mode is not armed.
        impl.status.state = AverMediaSwitchState::Failed;
        impl.status.detail = std::format("card not reached: getAudioFormat ret={}{}{}",
            formatCall.result, formatError.empty() ? "" : " ", formatError);
        return false;
    }
    impl.status.detail = std::format("chipAudioFormat={}{}", chipFormat,
        chipFormat == kChipFormatNonPcm ? " (non-PCM/Dolby)" : " (PCM)");
    impl.status.state = AverMediaSwitchState::Switched;
    startMonitoring();
    return true;
}

void AverMediaAudioSwitch::stop() noexcept {
    if (!p_) return;
    auto& impl = *p_;
    try {
        if (impl.checking && impl.avtModule != nullptr) {
            RawCall stopChecking{impl.exports.stopChecking, impl.opener()};
            std::string error;
            callVendor(stopChecking, error);
            impl.checking = false;
        }
        if (impl.sdkInitialized) {
            RawCall close{impl.exports.closePort, impl.sdk()};
            std::string error;
            callVendor(close, error);
            RawCall uninitialize{impl.exports.uninitialize, impl.sdk()};
            callVendor(uninitialize, error);
            impl.sdkInitialized = false;
        }
        if (impl.openerConstructed) {
            RawCall dtor{impl.exports.openerDtor, impl.opener()};
            std::string error;
            callVendor(dtor, error);
            impl.openerConstructed = false;
        }
        if (impl.sdkConstructed) {
            RawCall dtor{impl.exports.vendorDtor, impl.sdk()};
            std::string error;
            callVendor(dtor, error);
            impl.sdkConstructed = false;
        }
    } catch (...) {
        // Teardown must never throw; the vendors' worker threads are already
        // stopped by the calls above.
    }
    // The modules stay loaded on purpose: their monitor thread and the Qt
    // runtime are not safe to unload while other threads may still touch them.
}

void AverMediaAudioSwitch::startMonitoring() {
    auto& impl = *p_;
    if (!impl.sdkInitialized || impl.checking || impl.avtModule == nullptr) return;
    RawCall start{impl.exports.startChecking, impl.opener()};
    std::string error;
    if (!callVendor(start, error)) {
        impl.status.detail = "StartChecking failed: " + error;
        return;
    }
    impl.checking = true;
}

bool AverMediaAudioSwitch::active() const {
    return p_ && p_->checking;
}

const AverMediaSwitchStatus& AverMediaAudioSwitch::status() const {
    return p_->status;
}

} // namespace veyra::source
