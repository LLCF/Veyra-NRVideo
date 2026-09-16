// DLSS-G Ada multi-frame unlock probe (diagnostic only, never shipped).
//
// Exercises the product library (veyra_ngx::AdaMfgUnlock) against the local
// nvngx_dlssg.dll without involving NGX: identity, structural scan, then an
// apply / read-back / rollback cycle. On a non-Ada machine this is the only way
// to prove the patch mechanics; the Ada behaviour itself still has to be
// verified on RTX 40 hardware.
//
// Usage: veyra_dlssg_unlock_probe [path-to-nvngx_dlssg.dll] [--apply-test]
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "veyra/ngx/AdaMfgUnlock.h"

namespace {

std::string toUtf8(const std::wstring& value) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string text(size_t(length > 0 ? length - 1 : 0), '\0');
    if (length > 1) WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, text.data(), length, nullptr, nullptr);
    return text;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::filesystem::path modulePath;
    bool applyTest = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--apply-test") == 0) applyTest = true;
        else modulePath = argv[i];
    }
    if (modulePath.empty()) {
        modulePath = std::filesystem::current_path() / L"runtime_local/nvidia/nvngx_dlssg.dll";
    }
    std::printf("[dlssg-unlock] module=%s\n", toUtf8(modulePath.wstring()).c_str());
    if (!std::filesystem::exists(modulePath)) {
        std::printf("[dlssg-unlock] module not found\n");
        return 2;
    }
    std::printf("[dlssg-unlock] size=%llu bytes auditedIdentity=%s\n",
                static_cast<unsigned long long>(std::filesystem::file_size(modulePath)),
                veyra::ngx::AdaMfgUnlock::moduleIsAudited(modulePath.wstring()) ? "yes" : "no");

    // Load normally: the unlock edits the mapped image, which is exactly what
    // this probe has to exercise.
    HMODULE module = LoadLibraryExW(modulePath.wstring().c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        std::printf("[dlssg-unlock] LoadLibraryExW failed win32=%lu\n", GetLastError());
        return 3;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
    std::printf("[dlssg-unlock] TimeDateStamp=0x%08X SizeOfImage=0x%08X (audited 0x%08X/0x%08X)\n",
                nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage,
                veyra::ngx::AdaMfgUnlock::kKnownTimeDateStamp, veyra::ngx::AdaMfgUnlock::kKnownSizeOfImage);

    const auto scan = veyra::ngx::AdaMfgUnlock::scan(module);
    std::printf("[dlssg-unlock] scan: valid=%d identity=%d gateSites=%llu descriptors=%llu ptx=%llu midpoints=%llu joinLabelUnique=%d\n",
                scan.moduleValid ? 1 : 0, scan.identityMatched ? 1 : 0,
                static_cast<unsigned long long>(scan.archGateSites),
                static_cast<unsigned long long>(scan.descriptorSlots),
                static_cast<unsigned long long>(scan.ptxBytes),
                static_cast<unsigned long long>(scan.midpointCount),
                scan.joinLabelUnique ? 1 : 0);
    std::printf("[dlssg-unlock] scan detail: %s\n", scan.detail.c_str());

    const bool structureOk = scan.moduleValid && scan.archGateSites >= veyra::ngx::AdaMfgUnlock::kMinArchGateSites &&
                             scan.archGateSites <= veyra::ngx::AdaMfgUnlock::kMaxArchGateSites &&
                             scan.descriptorSlots > 0 && scan.ptxBytes == veyra::ngx::AdaMfgUnlock::kExpectedPtxBytes &&
                             scan.midpointCount == veyra::ngx::AdaMfgUnlock::kExpectedMidpoints &&
                             scan.joinLabelUnique;
    if (!structureOk) {
        std::printf("[dlssg-unlock] VERDICT structure does not match the audited build; unlock would refuse (correct behaviour)\n");
        FreeLibrary(module);
        return 1;
    }

    if (!applyTest) {
        std::printf("[dlssg-unlock] VERDICT structure verified (no patch applied; pass --apply-test to exercise it)\n");
        FreeLibrary(module);
        return 0;
    }

    const auto applied = veyra::ngx::AdaMfgUnlock::apply(module, true);
    std::printf("[dlssg-unlock] apply: applied=%d gates=%d(%llu) kernel=%d descriptors=%llu detail=%s\n",
                applied.applied ? 1 : 0, applied.archGatesPatched ? 1 : 0,
                static_cast<unsigned long long>(applied.archGateSites), applied.kernelPatched ? 1 : 0,
                static_cast<unsigned long long>(applied.descriptorSlots), toUtf8(applied.detail).c_str());

    // Read-back: the gate bytes must now read 0x90 and the descriptors must point
    // at the rebuilt fatbin, so a second scan sees zero gates and no matching
    // descriptor any more (the rebuilt one is uncompressed).
    const auto afterApply = veyra::ngx::AdaMfgUnlock::scan(module);
    std::printf("[dlssg-unlock] post-apply scan: gateSites=%llu descriptors=%llu ptx=%llu detail=%s\n",
                static_cast<unsigned long long>(afterApply.archGateSites),
                static_cast<unsigned long long>(afterApply.descriptorSlots),
                static_cast<unsigned long long>(afterApply.ptxBytes), afterApply.detail.c_str());
    const bool readBack = applied.applied && afterApply.archGateSites == 0;

    veyra::ngx::AdaMfgUnlock::release();
    const auto afterRelease = veyra::ngx::AdaMfgUnlock::scan(module);
    std::printf("[dlssg-unlock] post-release scan: gateSites=%llu descriptors=%llu ptx=%llu midpoints=%llu\n",
                static_cast<unsigned long long>(afterRelease.archGateSites),
                static_cast<unsigned long long>(afterRelease.descriptorSlots),
                static_cast<unsigned long long>(afterRelease.ptxBytes),
                static_cast<unsigned long long>(afterRelease.midpointCount));
    const bool restored = afterRelease.archGateSites == scan.archGateSites &&
                          afterRelease.descriptorSlots == scan.descriptorSlots &&
                          afterRelease.ptxBytes == scan.ptxBytes;
    std::printf("[dlssg-unlock] applied=%d readBack=%d restored=%d\n", applied.applied ? 1 : 0, readBack ? 1 : 0,
                restored ? 1 : 0);
    FreeLibrary(module);
    if (applied.applied && readBack && restored) {
        std::printf("[dlssg-unlock] VERDICT patch mechanics verified in memory and fully rolled back\n");
        return 0;
    }
    std::printf("[dlssg-unlock] VERDICT patch cycle incomplete\n");
    return 1;
}
