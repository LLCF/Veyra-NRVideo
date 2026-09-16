#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace veyra::gfx {

// Process-memory unlock for Intel XeSS frame-generation multi-frame support on
// non-Intel GPUs.
//
// Ported from Coldwood1026/OptiScaler (GPL-3.0,
// commit 70676c5f037c8c26f1ec355b250a72303cd268da, proxies/XeFGUnlock.h) and
// adapted for Veyra: strict provider identity (path + size + SHA-256) before any
// write, transactional install with read-back verification, and full rollback
// when the frame-generation session ends.
//
// Only the already-mapped image is modified. The DLL on disk is never touched,
// never re-signed and never renamed; if the provider build does not match the
// audited identity the unlock refuses to run and the caller keeps the stock
// 2X behaviour.
class XessMfgUnlock {
public:
    // Generated frames the UI may request: 1 = 2X, 2 = 3X, 3 = 4X.
    static constexpr uint32_t kMaxGeneratedFrames = 3;
    // Audited provider identity (Intel libxess_fg.dll 1.3.1.78 shipped with Veyra).
    static constexpr uint64_t kKnownProviderSize = 22957432ull;
    static constexpr const char* kKnownProviderSha256 =
        "EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27";
    static constexpr uint32_t kKnownTimeDateStamp = 0x69CB0F4Du;
    static constexpr uint32_t kKnownSizeOfImage = 0x015ED000u;

    struct State {
        bool providerLoaded = false;     // a module handle was supplied
        bool identityVerified = false;   // size + SHA-256 matched the audited build
        bool recognisedBuild = false;    // PE TimeDateStamp + SizeOfImage matched
        bool applied = false;            // every requested patch is installed
        uint32_t generatedFrames = 1;    // patched ceiling (1 = stock 2X)
        uint32_t maxInterpolations = 1;  // runtime-reported ceiling after patching
        std::wstring detail;             // last outcome, safe to log and display
    };

    // Installs the unlock into the mapped provider image. Idempotent. Passing
    // generatedFrames <= 1 leaves the provider untouched (stock 2X).
    static State apply(HMODULE provider, uint32_t generatedFrames);
    // Records the runtime-reported interpolation ceiling after the XeFG context
    // exists, so the settings UI can offer exactly what is supported.
    static void reportRuntimeCeiling(uint32_t maxInterpolations);
    // Restores every patched byte. Safe to call repeatedly and when nothing was
    // applied.
    static void release();

    static State snapshot();
    static bool applied();
    // Read-only identity check: true when the provider file at `path` is the
    // audited build these byte tables apply to. Never patches anything, so the
    // settings UI can offer 3X/4X before a session has run.
    static bool providerIsAudited(const std::wstring& path);
};

} // namespace veyra::gfx
