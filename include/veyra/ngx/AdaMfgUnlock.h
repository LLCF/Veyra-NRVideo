#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace veyra::ngx {

// Process-memory unlock for DLSS multi-frame generation (3X/4X/6X) on RTX 40
// series, where NVIDIA gates it to RTX 50.
//
// Ported from ImDreamt/MFGAdaUnlock-RenoDx (MIT) — itself an independent
// implementation of the technique published by dashdogy/RTX40MFG-Unlock — and
// adapted for Veyra: the whole edit is applied to the already-mapped image,
// nothing on disk is touched or re-signed, every site is pattern-verified
// before a write and fully rolled back when the session ends.
//
// Two independent gates and one kernel fix are required; each is verified
// against the local runtime build before anything is written:
//   1. DLSSGInstanceManager::PopulateParameters compares the NVAPI arch id
//      against 0x1b0 (Blackwell) to decide what MultiFrameCountMax it reports;
//   2. a second compare against the same constant feeds the runtime capability
//      flag that actually drives generation (patching only (1) renders black);
//   3. the interpolation kernel blends with a compiled-in 0.5, so every
//      generated frame lands at the temporal midpoint (frames repeat). The PTX
//      is decompressed, the 104 midpoint multiplies are replaced with the
//      kernel's own temporal parameter, and the fatbin is truncated after that
//      PTX entry so the driver JITs the corrected code instead of loading the
//      precompiled cubin.
//
// Safety rules baked in:
//   - never applied on Blackwell (50 series keeps its native path untouched);
//   - refuses when the arch-gate site count or the PTX structure does not match
//     the audited expectations, with the reason logged instead of guessing.
class AdaMfgUnlock {
public:
    // Audited runtime identity (runtime_local/nvidia/nvngx_dlssg.dll 310.7.0.0).
    static constexpr uint64_t kKnownModuleSize = 7519856ull;
    static constexpr const char* kKnownModuleSha256 =
        "135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F";
    static constexpr uint32_t kKnownTimeDateStamp = 0x69FB633Cu;
    static constexpr uint32_t kKnownSizeOfImage = 0x00745000u;
    // Structural expectations inside the runtime (verified locally before any
    // write; a mismatch is a refusal, not a guess).
    static constexpr uint64_t kExpectedPtxBytes = 99362ull;
    static constexpr size_t kExpectedMidpoints = 104;
    static constexpr size_t kMinArchGateSites = 1;
    static constexpr size_t kMaxArchGateSites = 4;
    static constexpr uint32_t kAdaArchId = 0x190u;      // NVAPI arch id, Ada
    static constexpr uint32_t kBlackwellArchId = 0x1B0u;

    struct Scan {
        bool moduleValid = false;
        bool identityMatched = false;
        size_t archGateSites = 0;
        size_t descriptorSlots = 0;
        uint64_t ptxBytes = 0;
        size_t midpointCount = 0;
        bool joinLabelUnique = false;
        std::string detail;
    };

    struct State {
        bool moduleFound = false;
        bool identityVerified = false;
        bool applied = false;
        bool archGatesPatched = false;
        bool kernelPatched = false;
        size_t archGateSites = 0;
        size_t descriptorSlots = 0;
        std::wstring detail;
    };

    // Read-only structural report; never writes. Used by the probe and by the
    // engine before deciding to apply.
    static Scan scan(HMODULE module);

    // True when the adapter is in the Ada device-id window AND the runtime still
    // reports the gated ceiling (1 generated frame). Blackwell is refused here
    // even if a caller asks.
    static bool adapterIsAda(uint32_t vendorId, uint32_t deviceId);

    // Installs both patches into the mapped image. `includeKernelFix` should be
    // true for any real use: without it multi-frame output repeats.
    static State apply(HMODULE module, bool includeKernelFix);

    // Restores every patched byte and frees the rebuilt fatbin. Safe to call
    // repeatedly and when nothing was applied.
    static void release();

    static State snapshot();
    static bool applied();
    // Read-only identity check against the audited runtime.
    static bool moduleIsAudited(const std::wstring& path);

private:
    struct Impl;
};

} // namespace veyra::ngx
