#include "veyra/gfx/XessPacing.h"

#include "veyra/Log.h"
#include "veyra/ThunkHook.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <intrin.h>
#include <mutex>

namespace veyra::gfx {
namespace {

// Provider signatures. The present is a member function: `this` in rcx plus six
// arguments, two of which arrive on the stack.
using PresentFn = int64_t (*)(void*, uint32_t, uint32_t, uint64_t, void*, void*, uint64_t);
// The provider's own frame scheduler and its ring snapshot helper.
using SchedFn = bool (*)(void*, void*, uint8_t, void*, uint32_t);
using RingSnapshotFn = void* (*)(void*, void*);

struct PacingState {
    std::mutex mutex;
    ThunkHook hook;
    bool installed = false;
    uint8_t* base = nullptr;
    PresentFn native = nullptr;
    SchedFn sched = nullptr;
    RingSnapshotFn ringSnapshot = nullptr;
    uint32_t generatedFrames = 0;
    // Counters, written only from the provider's present thread.
    uint64_t scheduled = 0, refused = 0, forwarded = 0, bypassed = 0;
    LARGE_INTEGER frequency{};
    int64_t lastScheduledQpc = 0;
    uint64_t lastScheduledIndex = 0;
    uint64_t lastScheduledCount = 0;
    int64_t gapSum = 0, gapMin = 0, gapMax = 0;
    uint64_t gapSamples = 0;
    // Gaps that cross a burst boundary include the real frame the provider
    // presents itself, so they are counted but not mixed into the in-burst
    // spacing that shows whether the fix worked.
    uint64_t boundaryGaps = 0;
    int64_t statsDeadlineQpc = 0;
    int64_t lastMultiplier = 0;
    int32_t logBudget = 6;
};

PacingState& state() {
    static PacingState instance;
    return instance;
}

double msFromQpc(const PacingState& s, int64_t qpc) {
    return s.frequency.QuadPart > 0 ? (double(qpc) * 1000.0) / double(s.frequency.QuadPart) : 0.0;
}

std::string narrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string text(size_t(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), text.data(), length, nullptr, nullptr);
    return text;
}

// The provider's scheduler is only live when its limiter is off; calling it
// while the limiter owns the pacing is a silent no-op, which would look like a
// successful fix while changing nothing.
bool schedulerUsable(void* ctx) {
    const auto* bytes = static_cast<const uint8_t*>(ctx);
    return bytes[XessPacing::kSchedLimiterOffset] == 0 && bytes[XessPacing::kSchedEnableOffset] != 0;
}

// One schedule call per generated frame, mirroring the provider's own order:
// snapshot the ring first, then schedule. Every argument is in hand because we
// sit inside the present call the provider itself made.
void scheduleFrame(void* ctx, uint8_t* burst, uint64_t index) {
    auto& s = state();
    if (s.sched == nullptr || s.ringSnapshot == nullptr) return;
    alignas(16) uint8_t timing[0x20]{};
    s.ringSnapshot(reinterpret_cast<uint8_t*>(ctx) + XessPacing::kRingOffset, timing);
    const uint8_t gate = burst[XessPacing::kBurstGateOffset] & 1;
    const uint64_t count = *reinterpret_cast<uint64_t*>(burst + 8);

    LARGE_INTEGER before{}, after{};
    QueryPerformanceCounter(&before);
    const bool ok = s.sched(ctx, burst, gate, timing, static_cast<uint32_t>(index));
    QueryPerformanceCounter(&after);

    ++s.scheduled;
    if (!ok) ++s.refused;

    const bool sameBurst = s.lastScheduledQpc != 0 && count == s.lastScheduledCount && index == s.lastScheduledIndex + 1;
    if (s.lastScheduledQpc != 0 && !sameBurst) ++s.boundaryGaps;
    if (sameBurst) {
        const int64_t gap = after.QuadPart - s.lastScheduledQpc;
        s.gapSum += gap;
        s.gapMin = s.gapSamples == 0 ? gap : (std::min)(s.gapMin, gap);
        s.gapMax = (std::max)(s.gapMax, gap);
        ++s.gapSamples;
    }
    s.lastScheduledQpc = after.QuadPart;
    s.lastScheduledIndex = index;
    s.lastScheduledCount = count;
    s.lastMultiplier = static_cast<int64_t>(count) + 1;

    // Bounded logging: the schedule must be visible going out, but this runs on
    // the present thread.
    if (s.logBudget > 0 && index == 1) {
        --s.logBudget;
        log::info("xess-pacing", std::format("frame {}/{} -> {} in {:.3f} ms (ctx+0x340={} ctx+0x341={} ring samples={} median={})",
                                             index, count + 1, ok ? "scheduled" : "REFUSED",
                                             msFromQpc(s, after.QuadPart - before.QuadPart),
                                             reinterpret_cast<uint8_t*>(ctx)[XessPacing::kSchedLimiterOffset],
                                             reinterpret_cast<uint8_t*>(ctx)[XessPacing::kSchedEnableOffset],
                                             *reinterpret_cast<uint64_t*>(timing),
                                             *reinterpret_cast<uint64_t*>(timing + 8)));
    }
    if (s.statsDeadlineQpc != 0 && after.QuadPart >= s.statsDeadlineQpc && s.gapSamples > 0) {
        // The measured gap is the only direct evidence that frames really come
        // out spread; report it instead of claiming the intent.
        log::info("xess-pacing", std::format("multiplier={}X scheduled={} refused={} bypassed={} forwarded={} inBurstGaps={} mean={:.3f}ms min={:.3f}ms max={:.3f}ms burstBoundaries={}",
                                             s.lastMultiplier, s.scheduled, s.refused, s.bypassed, s.forwarded,
                                             s.gapSamples, msFromQpc(s, s.gapSum / int64_t(s.gapSamples)),
                                             msFromQpc(s, s.gapMin), msFromQpc(s, s.gapMax), s.boundaryGaps));
        s.gapSum = 0; s.gapSamples = 0; s.gapMin = 0; s.gapMax = 0;
        s.statsDeadlineQpc = after.QuadPart + (s.frequency.QuadPart * 2);
    }
}

// Only the two call sites that present *generated* frames are paced; the other
// presenters of this thunk are forwarded byte-for-byte untouched.
void tryPace(void* ctx, void* arg5, void* arg6, uint64_t arg7, bool isLast) {
    if (arg5 == nullptr) return;
    const uint8_t flag = static_cast<uint8_t>(arg7);
    if ((flag == 1) == isLast) return;
    auto* burst = reinterpret_cast<uint8_t*>(arg5) - 0x38;
    const uint64_t count = *reinterpret_cast<uint64_t*>(burst + 8);
    if (count < 1 || count > 5) return;
    uint64_t index = count;
    if (!isLast) {
        auto* frames = *reinterpret_cast<uint8_t**>(burst);
        if (frames == nullptr || reinterpret_cast<uint8_t*>(arg6) < frames) return;
        const uint64_t offset = reinterpret_cast<uint8_t*>(arg6) - frames;
        if ((offset % 8) != 0) return;
        index = offset / 8;
        if (index < 1 || index >= count) return;
    }
    auto& s = state();
    if (s.sched != nullptr && schedulerUsable(ctx)) {
        // The provider keeps the last frame of the burst; scheduling it twice
        // would put two timestamps on one frame.
        if (!isLast) scheduleFrame(ctx, burst, index);
        return;
    }
    // Limiter owns the pacing: the provider already waits for the last frame,
    // and nothing here pretends to have paced the intermediate ones.
    ++s.bypassed;
}

int64_t detour(void* ctx, uint32_t a2, uint32_t a3, uint64_t a4, void* arg5, void* arg6, uint64_t arg7) {
    auto& s = state();
    if (s.installed) {
        auto* caller = static_cast<uint8_t*>(_ReturnAddress());
        if (caller == s.base + XessPacing::kPacedCallerRva) tryPace(ctx, arg5, arg6, arg7, false);
        else if (caller == s.base + XessPacing::kLastFrameCallerRva) tryPace(ctx, arg5, arg6, arg7, true);
        else ++s.forwarded;
    }
    return s.native(ctx, a2, a3, a4, arg5, arg6, arg7);
}

// A thunk is `E9 rel32` followed by 0xCC padding; the rel32 must reach
// `expectedTarget`. Anything else means this is not the build the RVAs were
// derived from, and the hook refuses.
bool thunkTargets(const uint8_t* base, uint32_t rva, uint32_t expectedTargetRva) {
    const uint8_t* thunk = base + rva;
    if (thunk[0] != 0xE9) return false;
    int32_t displacement = 0;
    std::memcpy(&displacement, thunk + 1, sizeof(displacement));
    const auto target = reinterpret_cast<uintptr_t>(thunk) + 5 + displacement;
    return target == reinterpret_cast<uintptr_t>(base + expectedTargetRva);
}

} // namespace

XessPacing::State XessPacing::install(HMODULE provider, uint32_t generatedFrames) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    State result{};
    result.generatedFrames = generatedFrames;
    if (provider == nullptr) {
        result.detail = L"no provider module";
        return result;
    }
    result.providerLoaded = true;
    if (generatedFrames < 2) {
        result.detail = L"2X: pacing is not needed (the provider presents one generated frame per real frame)";
        return result;
    }
    if (s.installed) {
        result = snapshot();
        return result;
    }
    auto* base = reinterpret_cast<uint8_t*>(provider);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        result.detail = L"provider is not a PE image";
        return result;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage != kKnownSizeOfImage ||
        nt->FileHeader.TimeDateStamp != kKnownTimeDateStamp) {
        result.detail = L"provider build does not match the audited identity";
        return result;
    }
    // All three thunks must point where the port expects, in this exact build.
    const bool presentOk = thunkTargets(base, kPresentThunkRva, kNativePresentRva);
    const bool schedOk = thunkTargets(base, kSchedThunkRva, kSchedFnRva);
    const bool ringOk = thunkTargets(base, kRingSnapshotThunkRva, kRingSnapshotFnRva);
    if (!presentOk || !schedOk || !ringOk) {
        result.detail = std::format(L"thunk verification failed (present={} scheduler={} ring={})",
                                    presentOk ? 1 : 0, schedOk ? 1 : 0, ringOk ? 1 : 0);
        return result;
    }
    result.structureVerified = true;
    QueryPerformanceFrequency(&s.frequency);
    s.base = base;
    s.sched = reinterpret_cast<SchedFn>(base + kSchedFnRva);
    s.ringSnapshot = reinterpret_cast<RingSnapshotFn>(base + kRingSnapshotFnRva);
    s.generatedFrames = generatedFrames;
    s.scheduled = s.refused = s.forwarded = s.bypassed = 0;
    s.lastScheduledQpc = 0;
    s.gapSum = s.gapSamples = 0;
    s.gapMin = s.gapMax = 0;
    s.lastMultiplier = 0;
    s.logBudget = 6;
    s.statsDeadlineQpc = s.frequency.QuadPart > 0 ? 0 : 0;
    auto status = s.hook.install(base + kPresentThunkRva, reinterpret_cast<void*>(&detour), 11);
    if (!status.installed) {
        s.sched = nullptr;
        s.ringSnapshot = nullptr;
        result.detail = std::format(L"thunk hook refused: {}", std::wstring(status.error.begin(), status.error.end()));
        return result;
    }
    s.native = reinterpret_cast<PresentFn>(status.trampoline != nullptr ? status.trampoline : base + kNativePresentRva);
    s.installed = true;
    // Arm the periodic stats window only once frame times are being measured.
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    s.statsDeadlineQpc = now.QuadPart + (s.frequency.QuadPart * 2);
    result.installed = true;
    result.detail = std::format(L"present thunk {} hooked, scheduler at +0x{:X} (original target +0x{:X})",
                                kPresentThunkRva, kSchedFnRva, kNativePresentRva);
    log::info("xess-pacing", std::format("installed for {}X: present thunk hooked, scheduler wired, {}",
                                         generatedFrames + 1, narrow(result.detail)));
    return result;
}

void XessPacing::release() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (!s.installed) return;
    s.installed = false;
    (void)s.hook.remove();
    s.native = nullptr;
    s.sched = nullptr;
    s.ringSnapshot = nullptr;
    log::info("xess-pacing", std::format("removed (scheduled={} refused={} bypassed={} forwarded={})",
                                         s.scheduled, s.refused, s.bypassed, s.forwarded));
}

XessPacing::State XessPacing::snapshot() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    State result{};
    result.providerLoaded = s.base != nullptr;
    result.structureVerified = s.sched != nullptr;
    result.installed = s.installed;
    result.generatedFrames = s.generatedFrames;
    result.scheduled = s.scheduled;
    result.refused = s.refused;
    result.forwarded = s.forwarded;
    result.bypassed = s.bypassed;
    result.gapSamples = s.gapSamples;
    if (s.gapSamples > 0) {
        result.meanGapMs = msFromQpc(s, s.gapSum / int64_t(s.gapSamples));
        result.minGapMs = msFromQpc(s, s.gapMin);
        result.maxGapMs = msFromQpc(s, s.gapMax);
    }
    return result;
}

bool XessPacing::installed() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    return s.installed;
}

bool XessPacing::providerIsAudited(const std::wstring& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size != kKnownProviderSize) return false;
    return true;
}

} // namespace veyra::gfx
