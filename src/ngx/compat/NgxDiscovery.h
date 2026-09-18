// Adapted from dashdogy/RTX40MFG-Unlock ampere_backend.cpp, MIT.
// Commit 33b41835dc39c5d8ab1ef93efb2449be31139c09. Image and decoded
// discovery only; Veyra owns scoped calls and resources without detours.
#pragma once
#include "NgxPatterns.h"
#include "hde/hde64.h"
#include <algorithm>
#include <cstring>
namespace veyra::ngx::compat {
using namespace ampere_patterns;
struct Image
{
    HMODULE module{};
    uintptr_t base{};
    uint32_t size{};
    const IMAGE_NT_HEADERS64* nt{};
    bool Open(HMODULE value) noexcept
    {
        module = value; base = reinterpret_cast<uintptr_t>(value);
        __try
        {
            const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(value);
            if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return false;
            nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
            size = nt->OptionalHeader.SizeOfImage;
            const uintptr_t sectionEnd = reinterpret_cast<uintptr_t>(IMAGE_FIRST_SECTION(nt) + nt->FileHeader.NumberOfSections);
            return size >= 4096 && size < 1024u * 1024u * 1024u && sectionEnd >= base && sectionEnd - base <= size;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool Contains(uintptr_t address, size_t bytes = 1) const noexcept
    { return address >= base && address - base < size && bytes <= size - (address - base); }
    bool Readable(uintptr_t address,size_t bytes) const noexcept
    {
        if(!Contains(address,bytes))return false;
        const auto end=address+bytes;
        while(address<end){
            MEMORY_BASIC_INFORMATION m{};
            if(VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))!=sizeof(m)||m.AllocationBase!=module||
               m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
            const DWORD p=m.Protect&0xff;
            if(p!=PAGE_READONLY&&p!=PAGE_READWRITE&&p!=PAGE_WRITECOPY&&p!=PAGE_EXECUTE_READ&&p!=PAGE_EXECUTE_READWRITE&&p!=PAGE_EXECUTE_WRITECOPY)return false;
            const auto next=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
            if(next<=address)return false;
            address=next;
        }
        return true;
    }
    bool Executable(uintptr_t address) const noexcept
    {
        MEMORY_BASIC_INFORMATION m{};
        return Contains(address) && VirtualQuery(reinterpret_cast<void*>(address), &m, sizeof(m)) == sizeof(m)
            && m.AllocationBase == module && m.Type == MEM_IMAGE && m.State == MEM_COMMIT
            && (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0
            && ((m.Protect & 0xff) == PAGE_EXECUTE_READ || (m.Protect & 0xff) == PAGE_EXECUTE);
    }
};

// Signature masks permit relocation operands, not opcode or layout changes.
// Decode the whole window and require every relative call/LEA and branch to
// belong to the same image. No driver version or fixed RVA grants admission.
bool DecodedWindow(const Image& image, uintptr_t address, size_t length) noexcept
{
    size_t offset = 0;
    while (offset < length)
    {
        hde64s ins{};
        const unsigned n = hde64_disasm(reinterpret_cast<void*>(address + offset), &ins);
        if (!n || (ins.flags & F_ERROR) || n > length - offset) return false;
        const auto* bytes = reinterpret_cast<const uint8_t*>(address + offset);
        if (ins.opcode == 0xe8)
        {
            int32_t displacement = 0; memcpy(&displacement, bytes + n - 4, 4);
            if (!image.Executable(address + offset + n + displacement)) return false;
        }
        if (ins.opcode == 0x8d && ins.modrm_mod == 0 && ins.modrm_rm == 5)
        {
            int32_t displacement = 0; memcpy(&displacement, bytes + n - 4, 4);
            if (!image.Contains(address + offset + n + displacement)) return false;
        }
        offset += n;
    }
    return offset == length;
}

uintptr_t FindPattern(const Image& image, const uint8_t* pattern, const uint8_t* mask,
    size_t length, size_t branchOffset = SIZE_MAX, uint32_t minimum = 0, uint32_t maximum = 0,
    unsigned* matches = nullptr) noexcept
{
    if (matches) *matches = 0;
    uintptr_t match = 0;
    const auto* sections = IMAGE_FIRST_SECTION(image.nt);
    for (unsigned i = 0; i < image.nt->FileHeader.NumberOfSections; ++i)
    {
        const auto& s = sections[i];
        if (!(s.Characteristics & IMAGE_SCN_MEM_EXECUTE) || s.VirtualAddress >= image.size) continue;
        const uint32_t begin = std::max<uint32_t>(s.VirtualAddress, minimum);
        const uint32_t end = static_cast<uint32_t>(std::min<uint64_t>(
            std::min<uint64_t>(image.size, maximum ? maximum : image.size),
            uint64_t{s.VirtualAddress} + std::max(s.Misc.VirtualSize, s.SizeOfRawData)));
        if(end<=begin||!image.Readable(image.base+begin,end-begin))continue;
        for (uint64_t offset = begin; offset + length <= end; ++offset)
        {
            const auto* candidate = reinterpret_cast<const uint8_t*>(image.base + offset);
            bool same = true;
            for (size_t j = 0; same && j < length; ++j) same = !mask[j] || candidate[j] == pattern[j];
            if (!same || !DecodedWindow(image, image.base + offset, length)) continue;
            if (branchOffset != SIZE_MAX)
            {
                const int displacement = static_cast<int8_t>(candidate[branchOffset + 1]);
                if (displacement <= 0 || !image.Executable(image.base + offset + branchOffset + 2 + displacement)) continue;
            }
            if (matches) ++*matches;
            if (match) return 0;
            match = image.base + offset;
        }
    }
    return match;
}

bool UnwindRoot(const Image& image, RUNTIME_FUNCTION function, RUNTIME_FUNCTION& root) noexcept
{
    for (unsigned depth = 0; depth != 8; ++depth)
    {
        const uintptr_t unwind = image.base + function.UnwindData;
        if (!image.Contains(unwind, 4)) return false;
        const auto* bytes = reinterpret_cast<const uint8_t*>(unwind);
        if ((bytes[0] & 7u) != 1u) return false;
        const unsigned flags = bytes[0] >> 3;
        if (!(flags & UNW_FLAG_CHAININFO)) { root = function; return true; }
        if (flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) return false;
        const size_t offset = (4u + 2u * bytes[2] + 3u) & ~size_t{3};
        if (!image.Contains(unwind, offset + sizeof(RUNTIME_FUNCTION))) return false;
        RUNTIME_FUNCTION parent{};
        memcpy(&parent, bytes + offset, sizeof(parent));
        DWORD64 owner = 0;
        const auto* actual = RtlLookupFunctionEntry(image.base + parent.BeginAddress, &owner, nullptr);
        if (!actual || owner != image.base
            || !image.Contains(reinterpret_cast<uintptr_t>(actual), sizeof(*actual))
            || memcmp(actual, &parent, sizeof(parent))) return false;
        function = parent;
    }
    return false;
}

bool FunctionBounds(const Image& image, uintptr_t entry, uint32_t& begin, uint32_t& end) noexcept
{
    DWORD64 owner = 0;
    const auto* function = RtlLookupFunctionEntry(entry, &owner, nullptr);
    if (!function || owner != image.base
        || !image.Contains(reinterpret_cast<uintptr_t>(function), sizeof(*function))
        || function->BeginAddress >= function->EndAddress
        || function->EndAddress > image.size
        || image.base + function->BeginAddress != entry
        || function->EndAddress - function->BeginAddress > 0x4000
        || !image.Executable(entry) || !image.Executable(image.base + function->EndAddress - 1))
        return false;
    begin = function->BeginAddress;
    end = function->EndAddress;
    RUNTIME_FUNCTION root{};
    if (!UnwindRoot(image, *function, root) || root.BeginAddress != begin) return false;
    // MSVC splits one function's unwind coverage when a nonvolatile register
    // is saved only on one branch. Adjacent entries with an exact chained
    // unwind parent are continuations, not independent exported functions.
    for (unsigned fragments = 0; fragments != 32; ++fragments)
    {
        owner = 0;
        const auto* next = RtlLookupFunctionEntry(image.base + end, &owner, nullptr);
        if (!next || owner != image.base
            || !image.Contains(reinterpret_cast<uintptr_t>(next), sizeof(*next))
            || next->BeginAddress != end) return true;
        RUNTIME_FUNCTION nextRoot{};
        if (!UnwindRoot(image, *next, nextRoot)) return false;
        if (memcmp(&root, &nextRoot, sizeof(root))) return true;
        if (next->EndAddress <= end || next->EndAddress > image.size
            || next->EndAddress - begin > 0x4000
            || !image.Executable(image.base + next->EndAddress - 1)) return false;
        end = next->EndAddress;
    }
    return false;
}

// NGX may outline Create's architecture validation into a helper far from
// its public export. Inspect only that function and its decoded direct callees;
// neighboring API implementations and unrelated image-wide matches grant no
// coverage. Resolve before installing our Create entry detour, then retain the
// owned addresses for the scoped startup/Create patches.
uintptr_t FindCreateValidation(const Image& image, uintptr_t create) noexcept
{
    uint32_t begin = 0, end = 0;
    if (!FunctionBounds(image, create, begin, end)) return 0;
    std::array<uintptr_t, 65> functions{};
    functions[0] = create;
    size_t count = 1;
    for (uintptr_t cursor = create; cursor < image.base + end;)
    {
        hde64s ins{};
        const unsigned length = hde64_disasm(reinterpret_cast<void*>(cursor), &ins);
        if (!length || (ins.flags & F_ERROR) || length > image.base + end - cursor) return 0;
        if (ins.opcode == 0xe8)
        {
            int32_t displacement = 0;
            memcpy(&displacement, reinterpret_cast<void*>(cursor + length - 4), sizeof(displacement));
            const uintptr_t target = cursor + length + displacement;
            uint32_t childBegin = 0, childEnd = 0;
            if (FunctionBounds(image, target, childBegin, childEnd)
                && std::find(functions.begin(), functions.begin() + count, target) == functions.begin() + count)
            {
                if (count == functions.size()) return 0;
                functions[count++] = target;
            }
        }
        cursor += length;
    }
    uintptr_t selected = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (!FunctionBounds(image, functions[i], begin, end)) return 0;
        unsigned matches = 0;
        const uintptr_t candidate = FindPattern(image, kAmpereNgxCreateValidationPattern.data(),
            kAmpereNgxCreateValidationPatternMask.data(), kAmpereNgxCreateValidationPattern.size(),
            kAmpereNgxCreateValidationBranchOffset, begin, end, &matches);
        if (matches > 1 || (candidate && selected)) return 0;
        if (candidate) selected = candidate;
    }
    return selected;
}


}
