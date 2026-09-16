#include "veyra/ngx/NvapiArchSpoof.h"

#include "veyra/Log.h"

#include <format>
#include <mutex>

namespace veyra::ngx {
namespace {

// NV_GPU_ARCH_INFO_V1 from the public nvapi.h: version, architecture,
// implementation, revision. The provider sets the version field to 0x20010
// before calling (seen in the audited disassembly at 0x180020DB4).
struct NvGpuArchInfo {
    uint32_t version;
    uint32_t architecture;
    uint32_t implementation;
    uint32_t revision;
};
using GetArchInfoFn = int(__cdecl*)(void* physicalGpu, NvGpuArchInfo* info);
using EnumPhysicalGpusFn = int(__cdecl*)(void* handles, uint32_t* count);
using QueryInterfaceFn = void*(__cdecl*)(uint32_t id);
constexpr uint32_t kNvapiEnumPhysicalGpusId = 0xADD604D1u;
constexpr uint32_t kNvapiInitializeId = 0x0150E828u;
constexpr uint32_t kNvGpuArchInfoVersion = 0x20010u;
using NvapiInitializeFn = int(__cdecl*)();

// The audited provider resolves NvAPI_GPU_GetArchInfo through a small internal
// wrapper at RVA 0x1670. Its first 24 bytes are a stable prologue ending at the
// next instruction boundary; a 14-byte absolute jump fits in the first 14 of
// them, and a trampoline holding the original 24 bytes plus a jump back runs the
// real logic. That keeps every real field (implementation/revision included)
// and only rewrites the architecture the provider adopts.
constexpr uint8_t kExpectedPrologue[24] = {
    0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
    0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1,
    0xF0, 0x83, 0x05, 0x4C, 0x14, 0x73, 0x00, 0x01};
constexpr size_t kRelocatedBytes = sizeof(kExpectedPrologue);
constexpr size_t kJumpBytes = 14; // ff 25 00 00 00 00 <addr64>
constexpr size_t kTrampolineBytes = kRelocatedBytes + kJumpBytes;

struct Global {
    std::mutex mutex;
    uint8_t* entry = nullptr;
    uint8_t* trampoline = nullptr;
    uint32_t reportedArchitecture = 0;
    uint32_t realArchitectureSeen = 0;
    bool targetResolved = false;
    bool installed = false;
    std::wstring detail;
};

Global& global() {
    static Global value;
    return value;
}

bool writeCode(void* target, const uint8_t* bytes, size_t count) {
    DWORD oldProtect = 0;
    if (VirtualProtect(target, count, PAGE_EXECUTE_READWRITE, &oldProtect) == 0) {
        return false;
    }
    std::memcpy(target, bytes, count);
    DWORD ignored = 0;
    VirtualProtect(target, count, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, count);
    return true;
}

int __cdecl SpoofedGetArchInfo(void* physicalGpu, NvGpuArchInfo* info) {
    auto& g = global();
    if (g.trampoline == nullptr) {
        return -1;
    }
    const int status = reinterpret_cast<GetArchInfoFn>(g.trampoline)(physicalGpu, info);
    if (status == 0 && info != nullptr && g.reportedArchitecture != 0) {
        if (g.realArchitectureSeen == 0) {
            g.realArchitectureSeen = info->architecture;
        }
        info->architecture = g.reportedArchitecture;
    }
    return status;
}

} // namespace

bool NvapiArchSpoof::install(HMODULE module, uint32_t reportedArchitecture) {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    if (g.installed) {
        return true;
    }
    if (module == nullptr || reportedArchitecture == 0) {
        g.detail = L"module or reported architecture missing";
        return false;
    }

    // Record what the driver really reports, purely for the log.
    HMODULE nvapi = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (nvapi != nullptr) {
        const auto query = reinterpret_cast<QueryInterfaceFn>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
        if (query != nullptr) {
            const auto getArchInfo = reinterpret_cast<GetArchInfoFn>(query(kNvapiGetArchInfoId));
            const auto enumGpus = reinterpret_cast<EnumPhysicalGpusFn>(query(kNvapiEnumPhysicalGpusId));
            const auto initialize = reinterpret_cast<NvapiInitializeFn>(query(kNvapiInitializeId));
            if (initialize != nullptr) {
                initialize();
            }
            if (getArchInfo != nullptr && enumGpus != nullptr) {
                uint64_t handles[64]{};
                uint32_t count = 0;
                if (enumGpus(handles, &count) == 0 && count > 0) {
                    NvGpuArchInfo info{};
                    info.version = kNvGpuArchInfoVersion;
                    if (getArchInfo(reinterpret_cast<void*>(handles[0]), &info) == 0) {
                        g.realArchitectureSeen = info.architecture;
                    }
                }
            }
        }
    }

    g.entry = reinterpret_cast<uint8_t*>(module) + kGetArchInfoWrapperRva;
    if (std::memcmp(g.entry, kExpectedPrologue, kRelocatedBytes) != 0) {
        g.detail = L"provider GetArchInfo wrapper prologue does not match the audited build";
        g.entry = nullptr;
        return false;
    }
    g.targetResolved = true;

    // Trampoline: the original 24 bytes followed by "jmp [rip+0] <wrapper+24>".
    g.trampoline = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, kTrampolineBytes, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (g.trampoline == nullptr) {
        g.detail = L"trampoline allocation failed";
        g.entry = nullptr;
        return false;
    }
    std::memcpy(g.trampoline, g.entry, kRelocatedBytes);
    {
        uint8_t* jump = g.trampoline + kRelocatedBytes;
        jump[0] = 0xFF; jump[1] = 0x25;
        const uint32_t zero = 0;
        std::memcpy(jump + 2, &zero, 4);
        const uint64_t back = reinterpret_cast<uint64_t>(g.entry + kRelocatedBytes);
        std::memcpy(jump + 6, &back, 8);
    }
    FlushInstructionCache(GetCurrentProcess(), g.trampoline, kTrampolineBytes);

    // Entry: "jmp [rip+0] <SpoofedGetArchInfo>" (14 bytes, no register touched).
    uint8_t patch[kJumpBytes]{};
    patch[0] = 0xFF; patch[1] = 0x25;
    const uint32_t zero = 0;
    std::memcpy(patch + 2, &zero, 4);
    const uint64_t target = reinterpret_cast<uint64_t>(&SpoofedGetArchInfo);
    std::memcpy(patch + 6, &target, 8);
    if (!writeCode(g.entry, patch, kJumpBytes)) {
        g.detail = L"the provider GetArchInfo wrapper page is not writable";
        VirtualFree(g.trampoline, 0, MEM_RELEASE);
        g.trampoline = nullptr;
        g.entry = nullptr;
        return false;
    }
    g.reportedArchitecture = reportedArchitecture;
    g.installed = true;
    log::info("nvapi-spoof", std::format(
        "provider GetArchInfo wrapper hooked (trampoline at {}): will report 0x{:X}",
        static_cast<const void*>(g.trampoline), reportedArchitecture));
    return true;
}

void NvapiArchSpoof::release() {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    if (!g.installed) {
        return;
    }
    const bool restored = writeCode(g.entry, kExpectedPrologue, kRelocatedBytes);
    if (g.trampoline != nullptr) {
        VirtualFree(g.trampoline, 0, MEM_RELEASE);
        g.trampoline = nullptr;
    }
    log::info("nvapi-spoof", std::format(
        "provider architecture load site restored={} (process memory only)", restored ? 1 : 0));
    g.installed = false;
    g.entry = nullptr;
}

bool NvapiArchSpoof::installed() {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    return g.installed;
}

NvapiArchSpoof::State NvapiArchSpoof::snapshot() {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    State state;
    state.installed = g.installed;
    state.targetResolved = g.targetResolved;
    state.slotWritten = g.entry != nullptr;
    state.reportedArchitecture = g.reportedArchitecture;
    state.detail = g.detail;
    return state;
}

} // namespace veyra::ngx
