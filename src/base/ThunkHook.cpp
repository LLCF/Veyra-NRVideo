#include "veyra/ThunkHook.h"

#include <format>

namespace veyra {

namespace {

constexpr size_t kJumpBytes = 5;
// jmp qword ptr [rip+0] ; <abs64>  -> continues to the original target.
constexpr size_t kTrampolineBytes = 14;

bool writeVerified(void* address, const uint8_t* bytes, size_t size) {
    DWORD previous = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &previous)) return false;
    memcpy(address, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    VirtualProtect(address, size, previous, &previous);
    return memcmp(address, bytes, size) == 0;
}

// Allocates an executable page within +/-2 GB of `near` so the five-byte
// relative jump can reach it. Scans outwards in 64 KB steps; returns nullptr
// when no address is free (the caller then refuses to install).
void* allocateNear(void* anchor, size_t size) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t granularity = info.dwAllocationGranularity ? info.dwAllocationGranularity : 0x10000;
    const uintptr_t base = reinterpret_cast<uintptr_t>(anchor);
    const uintptr_t minAddress = base > 0x80000000ull ? base - 0x70000000ull : granularity;
    for (uintptr_t step = granularity; step < 0x70000000ull; step += granularity) {
        for (int direction = 0; direction < 2; ++direction) {
            const uintptr_t candidate = direction == 0 ? base + step : base - step;
            if (candidate < minAddress || candidate > base + 0x70000000ull) continue;
            void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                                        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (memory != nullptr) return memory;
            if (direction == 1 && candidate <= granularity) break;
        }
    }
    return nullptr;
}

} // namespace

ThunkHook::~ThunkHook() {
    remove();
}

ThunkHook::Status ThunkHook::install(void* target, void* replacement, size_t paddingBytes) {
    std::lock_guard lock(mutex_);
    Status status{};
    if (target == nullptr || replacement == nullptr) {
        status.error = "null target or replacement";
        return status;
    }
    if (installed_) {
        status.installed = true;
        status.originalTarget = trampoline_ != nullptr ? reinterpret_cast<void*>(*reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(trampoline_) + 6)) : nullptr;
        status.trampoline = trampoline_;
        return status;
    }

    auto* bytes = reinterpret_cast<uint8_t*>(target);
    if (bytes[0] != 0xE9) {
        status.error = std::format("target {:#x} is not a relative jump thunk (opcode {:#x})",
                                   reinterpret_cast<uintptr_t>(target), unsigned(bytes[0]));
        return status;
    }
    for (size_t i = 0; i < paddingBytes; ++i) {
        if (bytes[kJumpBytes + i] != 0xCC) {
            status.error = std::format("target {:#x} padding byte {} is {:#x}, expected 0xCC",
                                       reinterpret_cast<uintptr_t>(target), i, unsigned(bytes[kJumpBytes + i]));
            return status;
        }
    }

    const int32_t relative = *reinterpret_cast<int32_t*>(bytes + 1);
    auto* originalTarget = bytes + kJumpBytes + relative;
    status.originalTarget = originalTarget;

    // One nearby page holds two absolute-jump stubs: one continues to the
    // original target (the trampoline the replacement calls) and one enters the
    // replacement. The replacement itself may live anywhere; only this page has
    // to be inside the five-byte relative jump range.
    auto* page = reinterpret_cast<uint8_t*>(allocateNear(target, 0x1000));
    if (page == nullptr) {
        status.error = std::format("no executable page within 2 GB of {:#x}", reinterpret_cast<uintptr_t>(target));
        return status;
    }
    auto writeAbsoluteJump = [](uint8_t* at, void* destination) {
        at[0] = 0xFF;
        at[1] = 0x25;
        at[2] = 0x00;
        at[3] = 0x00;
        at[4] = 0x00;
        at[5] = 0x00;
        *reinterpret_cast<uint64_t*>(at + 6) = reinterpret_cast<uint64_t>(destination);
    };
    auto* trampoline = page;
    auto* entry = page + 64;
    writeAbsoluteJump(trampoline, originalTarget);
    writeAbsoluteJump(entry, replacement);
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);

    const int64_t distance = reinterpret_cast<uint8_t*>(entry) - (bytes + kJumpBytes);
    if (distance > 0x7FFFFFFFll || distance < -0x80000000ll) {
        VirtualFree(page, 0, MEM_RELEASE);
        status.error = std::format("stub page {:#x} is out of relative jump range from {:#x}",
                                   reinterpret_cast<uintptr_t>(entry), reinterpret_cast<uintptr_t>(target));
        return status;
    }

    uint8_t patch[kJumpBytes]{};
    patch[0] = 0xE9;
    *reinterpret_cast<int32_t*>(patch + 1) = static_cast<int32_t>(distance);
    memcpy(original_, bytes, kJumpBytes);
    if (!writeVerified(target, patch, kJumpBytes)) {
        VirtualFree(page, 0, MEM_RELEASE);
        status.error = std::format("patch verification failed at {:#x}", reinterpret_cast<uintptr_t>(target));
        return status;
    }

    target_ = target;
    trampoline_ = trampoline;
    patchBytes_ = kJumpBytes;
    installed_ = true;
    status.installed = true;
    status.trampoline = trampoline;
    return status;
}

bool ThunkHook::remove() {
    std::lock_guard lock(mutex_);
    bool restored = true;
    if (installed_ && target_ != nullptr) {
        restored = writeVerified(target_, original_, patchBytes_);
    }
    installed_ = false;
    target_ = nullptr;
    releaseTrampoline();
    return restored;
}

void ThunkHook::releaseTrampoline() {
    if (trampoline_ != nullptr) {
        VirtualFree(trampoline_, 0, MEM_RELEASE);
        trampoline_ = nullptr;
    }
}

} // namespace veyra
