#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace veyra::ngx {

// Makes the DLSS-G provider see a different GPU architecture than the physical
// card reports. The audited 310.7 build has exactly one place that turns the
// driver's NV_GPU_ARCH_INFO into the value the provider stores and compares
// (DLSSGInstanceManager::SetGPUArch, "mov eax,[rsp+294h]" at RVA 0x20DCA). That
// seven-byte load is replaced with "mov eax,<architecture>" so the provider
// sees the requested id while everything else - including the real
// implementation/revision fields it later reads - stays intact. Process memory
// only, fully reverted on release.
//
// Used on RTX 30 (sm_86): the provider's frame-generation availability gate
// reads the architecture and refuses Ampere outright. Reporting Blackwell makes
// every architecture-based check pass; the kernel rewrite (AmpereMfgUnlock) is
// still required so the programs can actually run on sm_86. Nothing here runs
// on Ada or Blackwell - those keep their untouched native paths.
class NvapiArchSpoof {
public:
    static constexpr uint32_t kNvapiGetArchInfoId = 0xD8265D24u;   // nvapi_QueryInterface id
    static constexpr uintptr_t kGetArchInfoWrapperRva = 0x1670u; // audited 310.7 internal wrapper
    static constexpr uint32_t kArchBlackwell = 0x1B0u;             // NV_GPU_ARCHITECTURE_GB200
    static constexpr uint32_t kArchAda = 0x190u;                   // NV_GPU_ARCHITECTURE_AD100
    static constexpr uint32_t kArchAmpere = 0x170u;                // NV_GPU_ARCHITECTURE_GA100

    // `module` is the loaded nvngx_dlssg.dll. `reportedArchitecture` is an
    // NV_GPU_ARCHITECTURE_ID; 0 refuses.
    static bool install(HMODULE module, uint32_t reportedArchitecture);
    static void release();
    static bool installed();

    struct State {
        bool installed = false;
        bool targetResolved = false;
        bool slotWritten = false;
        uint32_t reportedArchitecture = 0;
        uint32_t wrapperCalls = 0;
        uint32_t realArchitectureSeen = 0;
        std::wstring detail;
    };
    static State snapshot();
};

} // namespace veyra::ngx
