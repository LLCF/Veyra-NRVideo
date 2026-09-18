#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace veyra::ngx {

// Diagnostic-only architecture substitution, enabled explicitly through
// VEYRA_TEST_NVAPI_SPOOF_ARCH. Hooks the audited provider's GetArchInfo wrapper
// using a relocated trampoline, modifying the returned architecture for all
// provider callers. This affects network selection as well as capabilities:
// normal Ampere operation must preserve the real architecture. Process memory
// only, restored on release. Ada/Blackwell product paths never install it.
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
    static bool release();
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
