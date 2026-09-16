#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace veyra::gfx {

// Spreads Intel's generated frames out in time above 2X.
//
// Ported from Coldwood1026/OptiScaler (GPL-3.0, commit
// 70676c5f037c8c26f1ec355b250a72303cd268da, OptiScaler/proxies/XeFGPacing.h) and
// reduced to the part that fixes the actual defect: the provider schedules only
// the last frame of a burst, so at 3X/4X the intermediate ones are presented
// back to back and read as a clump followed by a gap.
//
// The provider already knows the correct spacing - it computes
// "real frame duration / multiplier" once per burst - but only ever hands the
// last generated frame to its own scheduler. This port intercepts the present
// thunk, recognises the two call sites that present generated frames, and hands
// the loop's frames to the provider's own scheduler with the arguments the
// present already carries (burst, frame index, gate byte, ring snapshot).
// Everything else is forwarded untouched.
//
// Provider identity and structure are verified before the thunk is touched; a
// mismatch refuses the hook instead of guessing, and release() restores the
// original bytes exactly. 2X never installs anything.
class XessPacing {
public:
    // libxess_fg.dll 1.3.1.78 (same audited build as XessMfgUnlock).
    static constexpr uint64_t kKnownProviderSize = 22957432ull;
    static constexpr const char* kKnownProviderSha256 =
        "EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27";
    static constexpr uint32_t kKnownTimeDateStamp = 0x69CB0F4Du;
    static constexpr uint32_t kKnownSizeOfImage = 0x015ED000u;
    // Provider internals (verified against the audited build before hooking).
    static constexpr uint32_t kPresentThunkRva = 0x25C0;
    static constexpr uint32_t kNativePresentRva = 0x21F730;
    static constexpr uint32_t kSchedThunkRva = 0x3100;
    static constexpr uint32_t kSchedFnRva = 0x21EE30;
    static constexpr uint32_t kRingSnapshotThunkRva = 0x4DA0;
    static constexpr uint32_t kRingSnapshotFnRva = 0x224CF0;
    static constexpr uint32_t kPacedCallerRva = 0x2202ED;
    static constexpr uint32_t kLastFrameCallerRva = 0x220467;
    static constexpr uint32_t kBurstGateOffset = 0xC0;
    static constexpr uint32_t kSchedLimiterOffset = 0x340;
    static constexpr uint32_t kSchedEnableOffset = 0x341;
    static constexpr uint32_t kRingOffset = 0x168;

    struct State {
        bool providerLoaded = false;
        bool structureVerified = false;   // thunks and RVAs match the audited build
        bool installed = false;           // present thunk redirected
        uint32_t generatedFrames = 0;     // requested (1 = 2X, no hook needed)
        uint64_t scheduled = 0;           // frames handed to the provider scheduler
        uint64_t refused = 0;             // scheduler said no (its own gate)
        uint64_t forwarded = 0;           // presents left untouched
        uint64_t bypassed = 0;            // frames skipped because the scheduler was off
        // Measured spacing between two consecutive scheduled frames.
        double meanGapMs = 0.0, minGapMs = 0.0, maxGapMs = 0.0;
        uint64_t gapSamples = 0;
        std::wstring detail;
    };

    // Installs the hook when the session really asks for more than one generated
    // frame. Anything that does not match the audited provider refuses.
    static State install(HMODULE provider, uint32_t generatedFrames);
    static void release();
    static State snapshot();
    static bool installed();
    // Read-only identity check, mirroring XessMfgUnlock::providerIsAudited.
    static bool providerIsAudited(const std::wstring& path);
};

} // namespace veyra::gfx
