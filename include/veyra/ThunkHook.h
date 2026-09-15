#pragma once

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>

namespace veyra {

// Minimal x64 detour for jump thunks.
//
// Several provider DLLs expose their internal entry points as five-byte
// relative jump thunks (`E9 rel32`) followed by padding. Redirecting such a
// thunk is the safest way to hook provider internals: the whole hookable region
// is one instruction, so no instruction boundary can be split and no
// disassembler is required. Anything that is not the canonical thunk shape is
// rejected instead of guessed at.
//
// The hook keeps a trampoline that continues to the original jump target, so
// the replacement can forward calls. Every patch is verified by read-back and
// restored byte-exactly on removal.
class ThunkHook {
public:
    struct Status {
        bool installed = false;
        std::string error;
        void* originalTarget = nullptr;
        void* trampoline = nullptr;
    };

    ThunkHook() = default;
    ~ThunkHook();
    ThunkHook(const ThunkHook&) = delete;
    ThunkHook& operator=(const ThunkHook&) = delete;

    // Requires target[0] == 0xE9 and `paddingBytes` 0xCC bytes after the jump.
    // The replacement may live anywhere: the hook builds a nearby page holding
    // an absolute-jump stub to the replacement plus the trampoline that
    // continues to the original target.
    Status install(void* target, void* replacement, size_t paddingBytes = 11);
    bool remove();
    bool installed() const { return installed_; }
    void* trampoline() const { return trampoline_; }

private:
    void releaseTrampoline();

    std::mutex mutex_;
    void* target_ = nullptr;
    void* trampoline_ = nullptr;
    uint8_t original_[8]{};
    size_t patchBytes_ = 0;
    bool installed_ = false;
};

} // namespace veyra
