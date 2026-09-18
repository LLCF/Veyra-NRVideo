#pragma once
#include "protected_pointer.h"
#include "veyra/Log.h"
#include <format>
#include <atomic>

namespace veyra::ngx::compat {
inline std::atomic<bool> memoryProtectionUncertain{false};
// Called only after NGX shutdown. Retain patch records and backing storage
// whenever restoration cannot be proved, including page protection failures.
inline bool restoreMemory(void* destination, const void* original, size_t bytes) {
    if (GetEnvironmentVariableW(L"VEYRA_TEST_PATCH_RESTORE_FAIL", nullptr, 0)) return false;
    DWORD protection = 0;
    const auto address = reinterpret_cast<uintptr_t>(destination);
    if (!protected_pointer::QueryProtection(address, protection, bytes)) { log::error("patch-memory",std::format("query failed address=0x{:X} bytes={} win32={}",address,bytes,GetLastError())); return false; }
    DWORD oldProtection=0;
    // Image-backed pages may report EXECUTE_WRITECOPY until the first write
    // faults in a private page, even after a successful READWRITE request.
    const bool changed=VirtualProtect(destination,bytes,PAGE_EXECUTE_READWRITE,&oldProtection)!=FALSE;
    DWORD writable=0;
    const bool queried=protected_pointer::QueryProtection(address,writable,bytes);
    if (!changed||!queried||(writable!=PAGE_EXECUTE_READWRITE&&writable!=PAGE_EXECUTE_WRITECOPY)) {
        if(!protected_pointer::RestoreProtectionWithRetry(destination, bytes, protection, &VirtualProtect))memoryProtectionUncertain=true;
        log::error("patch-memory",std::format("writable protection failed address=0x{:X} bytes={} protection=0x{:X} win32={}",address,bytes,protection,GetLastError())); return false;
    }
    std::memcpy(destination, original, bytes);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), destination, bytes) != FALSE;
    const bool restored = protected_pointer::RestoreProtectionWithRetry(destination, bytes, protection, &VirtualProtect);
    const bool equal=std::memcmp(destination, original, bytes)==0;
    if(!flushed||!restored||!equal)memoryProtectionUncertain=true;
    if(!flushed||!restored||!equal)log::error("patch-memory",std::format("verification failed address=0x{:X} bytes={} protection=0x{:X} flush={} restored={} equal={} win32={}",address,bytes,protection,flushed,restored,equal,GetLastError()));
    return flushed && restored && equal;
}
}
