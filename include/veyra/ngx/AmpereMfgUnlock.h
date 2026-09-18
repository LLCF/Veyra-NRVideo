#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace veyra::ngx {

// Process-memory RTX 30 (sm_86) native DLSS multi-frame unlock.
//
// Ported from dashdogy/RTX40MFG-Unlock v1.3.3 (MIT, commit
// 33b41835dc39c5d8ab1ef93efb2449be31139c09) `source/native/ampere_gpu.cpp`
// (`FindUniqueSm89Ptx` / `BuildAmpereSm86Fatbin` / `PublishProgram`) and
// adapted to the audited DLSS-G 310.7 layout used by Veyra:
//
//   * the provider embeds 69 fatbins. 25 of them are the DLFG program slots
//     ("dlfg_kernel"/"main_kernel", each with exactly one sm_89 PTX entry),
//     referenced by eight identical 25-entry registration tables of 48-byte
//     records whose +8 field holds the fatbin pointer (8x25 = 200 slot
//     pointers);
//   * 38 network and 6 auxiliary fatbins are inventoried (44 LEA sites).
//     Only the 25 registered programs and hash-identified font program are
//     rebuilt, matching upstream's scope. Network programs stay untouched.
//     The font is replaced in place to preserve all its references.
//     The temporal slot's
//     PTX additionally gets the midpoint correction (compiled-in 0.5 replaced
//     with the kernel's own temporal parameter), exactly as on Ada;
//   * the two architecture compares (0x1b0, Blackwell) are retargeted to
//     0x170 (Ampere) so 30/40/50 series all pass, and nothing else changes.
//
// Everything is process memory only: the on-disk DLL is never written,
// re-signed or renamed, every patched byte is recorded and restored on
// release, and any structural mismatch is a refusal with a logged reason.
class AmpereMfgUnlock {
public:
    // Audited runtime identity: runtime_local/nvidia/nvngx_dlssg.dll 310.7.
    static constexpr uint64_t kKnownModuleSize = 7519856ull;
    static constexpr const char* kKnownModuleSha256 =
        "135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F";
    static constexpr uint32_t kKnownTimeDateStamp = 0x69FB633Cu;
    static constexpr uint32_t kKnownSizeOfImage = 0x00745000u;

    // Structural expectations of the audited 310.7 build.
    static constexpr size_t kExpectedSlotRuns = 8;
    static constexpr size_t kExpectedSlotsPerRun = 25;
    static constexpr size_t kExpectedProgramFatbins = 25;
    static constexpr size_t kExpectedRdataNetworkFatbins = 38;
    static constexpr size_t kExpectedAuxFatbins = 6;
    static constexpr size_t kExpectedLeaSites = 44;
    static constexpr uint32_t kAmpereArchId = 0x170u;
    static constexpr size_t kExpectedArchGateSites = 2;
    static constexpr size_t kMinArchGateSites = 1;
    static constexpr size_t kMaxArchGateSites = 4;

    struct Scan {
        bool moduleValid = false;
        bool identityMatched = false;
        size_t slotRuns = 0;
        size_t slotPointers = 0;
        size_t programFatbins = 0;
        size_t networkFatbins = 0;
        size_t auxFatbins = 0;
        size_t leaSites = 0;
        size_t archGateSites = 0;
        bool temporalSlotUnique = false;
        std::string detail;
    };

    struct State {
        bool moduleFound = false;
        bool identityVerified = false;
        bool applied = false;
        bool archGatesPatched = false;
        bool fatbinsRedirected = false;
        size_t slotRuns = 0;
        size_t slotPointers = 0;
        size_t programFatbins = 0;
        size_t networkFatbins = 0;
        size_t auxFatbins = 0;
        size_t leaSites = 0;
        size_t archGateSites = 0;
        std::wstring detail;
    };

    // Read-only structural report; never writes. Used by the probe and by the
    // engine before deciding to apply.
    static Scan scan(HMODULE module);

    // True when the adapter is Ampere (RTX 30, sm_86 window). Ada and Blackwell
    // keep their own paths and are refused here.
    static bool adapterIsAmpere(uint32_t vendorId, uint32_t deviceId);

    // Installs the sm_86 fatbin redirection and (unless `retargetArchGates` is
    // false) the architecture compares. Refuses (and rolls back) unless every
    // audited structure matches. Callers that make the provider see Blackwell
    // through the NVAPI spoof must pass false so the provider's own 0x1b0
    // compare stays byte-identical and therefore passes.
    // The product supplies its D3D12 adapter LUID; zero selects CUDA device 0
    // only for standalone diagnostics without a D3D12 context.
    static State apply(HMODULE module, bool retargetArchGates = true, uint64_t adapterLuid = 0);

    // Restores every patched byte and frees the rebuilt fatbins. Safe to call
    // repeatedly and when nothing was applied.
    static bool release();

    static State snapshot();
    static bool applied();
};

} // namespace veyra::ngx
