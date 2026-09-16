#include "veyra/ngx/AmpereMfgUnlock.h"

#include "veyra/Log.h"

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <vector>

namespace veyra::ngx {
namespace {

constexpr uint32_t kFatbinMagic = 0xBA55ED50u;
constexpr size_t kOuterHeader = 16;
constexpr uint32_t kPtxKind = 1;
constexpr uint32_t kCubinKind = 2;
constexpr uint32_t kSm89 = 89;
constexpr uint32_t kSm86 = 86;
constexpr uint64_t kCompressedFlag = 0x2000ull;
constexpr size_t kRecordBytes = 48;
constexpr size_t kEntryNameOffset = 0x10;   // slot+0x10 -> "main_kernel"
constexpr size_t kDescNameOffset = 0x28;    // slot+0x28 -> next record +0 ("dlfg_kernel")
constexpr char kEntryName[] = "main_kernel";
constexpr char kDescriptorName[] = "dlfg_kernel";
constexpr size_t kMaximumFatbinBytes = 16ull << 20;

// Temporal (midpoint) correction, identical technique to the Ada port.
constexpr char kSm89Target[] = ".target sm_89";
constexpr char kSm86Target[] = ".target sm_86";
constexpr char kJoinLabel[] = "$L__BB0_3:";
constexpr char kMidpointBits[] = "0f3F000000";
constexpr char kMulPrefix[] = "mul.ftz.f32 ";
constexpr char kCurrToPrev[] = "%f136";
constexpr char kPrevToCurr[] = "%f134";
constexpr size_t kTemporalMidpoints = 104;
// The Ampere compiler must never see Ada/Blackwell-only constructs; the same
// refusal list dashdogy applies before retargeting.
constexpr const char* kUnsupportedPtxMarkers[] = {
    ".e4m3", ".e5m2", "wgmma.", "tcgen05.", "mma.sp::ordered_metadata",
    "sm_90", "sm_120"};

uint16_t readU16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
uint32_t readU32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint64_t readU64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }

bool sha256Equals(const uint8_t* bytes, size_t count, const char* expected) {
    if (bytes == nullptr || count == 0 || count > ULONG_MAX || expected == nullptr ||
        std::strlen(expected) != 64) {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    std::array<uint8_t, 32> digest{};
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0) {
        status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes),
                            static_cast<ULONG>(count), digest.data(),
                            static_cast<ULONG>(digest.size()));
    }
    if (algorithm != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    if (status < 0) {
        return false;
    }
    static constexpr char kDigits[] = "0123456789ABCDEF";
    for (size_t index = 0; index < digest.size(); ++index) {
        if (expected[index * 2] != kDigits[digest[index] >> 4] ||
            expected[index * 2 + 1] != kDigits[digest[index] & 0x0F]) {
            return false;
        }
    }
    return true;
}

// Plain LZ4 block format, as emitted by the CUDA fatbin packer.
bool lz4BlockDecompress(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
    size_t in = 0, out = 0;
    while (in < srcSize) {
        const uint8_t token = src[in++];
        size_t literals = token >> 4;
        if (literals == 15) {
            uint8_t ext = 0;
            do {
                if (in >= srcSize) return false;
                ext = src[in++];
                literals += ext;
            } while (ext == 0xFF);
        }
        if (literals > srcSize - in || literals > dstSize - out) return false;
        std::memcpy(dst + out, src + in, literals);
        in += literals;
        out += literals;
        if (in == srcSize) break;
        if (srcSize - in < 2) return false;
        const size_t back = size_t(src[in]) | (size_t(src[in + 1]) << 8);
        in += 2;
        if (back == 0 || back > out) return false;
        size_t match = 4 + (token & 0x0F);
        if ((token & 0x0F) == 15) {
            uint8_t ext = 0;
            do {
                if (in >= srcSize) return false;
                ext = src[in++];
                match += ext;
            } while (ext == 0xFF);
        }
        if (match > dstSize - out) return false;
        for (size_t i = 0; i < match; ++i) dst[out + i] = dst[out + i - back];
        out += match;
    }
    return in == srcSize && out == dstSize;
}

bool pointsToCString(const uint8_t* base, size_t imageSize, uint64_t value, const char* expected) {
    const auto start = reinterpret_cast<uintptr_t>(base);
    if (value < start || value >= start + imageSize) return false;
    const size_t len = std::strlen(expected);
    if (value + len + 1 > start + imageSize) return false;
    return std::memcmp(reinterpret_cast<const void*>(value), expected, len + 1) == 0;
}

// One sm_89 PTX entry inside a fatbin (the upstream FindUniqueSm89Ptx profile).
struct FatbinInfo {
    const uint8_t* address = nullptr;
    size_t size = 0;
    size_t entryOffset = 0;
    uint32_t entryHeaderBytes = 0;
    uint32_t compressedBytes = 0;
    uint64_t rawBytes = 0;
    uint64_t flags = 0;
    uint32_t entryCount = 0;
    bool temporalShape = false;
};

bool parseFatbin(const uint8_t* fat, size_t size, FatbinInfo& info) {
    info = FatbinInfo{};
    if (fat == nullptr || size < kOuterHeader + 64 || size > kMaximumFatbinBytes) return false;
    if (readU32(fat) != kFatbinMagic || readU16(fat + 6) != kOuterHeader) return false;
    if (readU64(fat + 8) + kOuterHeader != size) return false;
    size_t offset = kOuterHeader;
    size_t ptxCount = 0;
    while (offset + 64 <= size) {
        const uint16_t kind = readU16(fat + offset);
        const uint32_t headerBytes = readU32(fat + offset + 4);
        const uint64_t payloadBytes = readU64(fat + offset + 8);
        if (headerBytes < 64 || payloadBytes == 0 ||
            offset + headerBytes + payloadBytes > size) {
            return false;
        }
        const uint32_t arch = readU32(fat + offset + 28);
        ++info.entryCount;
        if (kind == kPtxKind && arch == kSm89) {
            ++ptxCount;
            info.entryOffset = offset;
            info.entryHeaderBytes = headerBytes;
            info.compressedBytes = readU32(fat + offset + 16);
            info.rawBytes = readU64(fat + offset + 56);
            info.flags = readU64(fat + offset + 40);
        }
        offset += headerBytes + payloadBytes;
    }
    if (offset != size || ptxCount != 1 || info.compressedBytes == 0 ||
        info.rawBytes == 0 || (info.flags & kCompressedFlag) == 0) {
        return false;
    }
    info.address = fat;
    info.size = size;
    return true;
}

// Decompress the single sm_89 PTX entry; returns the raw PTX copy.
bool extractSm89Ptx(const FatbinInfo& info, std::vector<uint8_t>& ptx) {
    ptx.assign(size_t(info.rawBytes), uint8_t{0});
    const uint8_t* payload = info.address + info.entryOffset + info.entryHeaderBytes;
    return lz4BlockDecompress(payload, info.compressedBytes, ptx.data(), ptx.size());
}

// The temporal kernel carries a unique join label and exactly 104 compiled-in
// 0.5 midpoint multiplies; that shape decides which fatbin gets the midpoint
// correction. Returns hasShape plus the midpoint offsets.
bool inspectTemporalShape(const std::vector<uint8_t>& ptx, size_t& joinLabelCount,
                          std::vector<size_t>& midpoints) {
    joinLabelCount = 0;
    midpoints.clear();
    const char* begin = reinterpret_cast<const char*>(ptx.data());
    const size_t n = ptx.size();
    const size_t labelLen = sizeof(kJoinLabel) - 1;
    for (size_t i = 0; i + labelLen <= n; ++i) {
        if (std::memcmp(begin + i, kJoinLabel, labelLen) == 0) ++joinLabelCount;
    }
    const size_t midLen = sizeof(kMidpointBits) - 1;
    const size_t mulLen = sizeof(kMulPrefix) - 1;
    for (size_t i = 0; i + midLen < n; ++i) {
        if (std::memcmp(begin + i, kMidpointBits, midLen) != 0) continue;
        if (begin[i + midLen] != ';') continue;
        size_t line = i;
        while (line > 0 && begin[line - 1] != '\n') --line;
        if (i - line < mulLen) continue;
        if (std::memcmp(begin + line, kMulPrefix, mulLen) != 0) continue;
        midpoints.push_back(i);
    }
    return joinLabelCount == 1 && midpoints.size() == kTemporalMidpoints;
}

// Rewrite the temporal PTX exactly like the Ada port: inject the temporal
// parameter right after the unique join label and replace the first 52
// midpoints with 1-t and the second 52 with t.
bool correctTemporalPtx(std::vector<uint8_t>& ptx) {
    const char* begin = reinterpret_cast<const char*>(ptx.data());
    const size_t n = ptx.size();
    const size_t labelLen = sizeof(kJoinLabel) - 1;
    size_t label = SIZE_MAX;
    for (size_t i = 0; i + labelLen <= n; ++i) {
        if (std::memcmp(begin + i, kJoinLabel, labelLen) != 0) continue;
        label = i;
        break;
    }
    if (label == SIZE_MAX) return false;
    size_t insertion = label + labelLen;
    while (insertion < n && begin[insertion] != '\n') ++insertion;
    if (insertion >= n) return false;
    ++insertion;

    size_t labelCount = 0;
    std::vector<size_t> marks;
    if (!inspectTemporalShape(ptx, labelCount, marks)) return false;
    if (marks.empty() || marks.front() <= insertion) return false;

    static constexpr char kTemporalInput[] =
        "ld.param.f32 %f134, [main_kernel_param_0+32];\r\n"
        "mov.f32 %f135, 0f3F800000;\r\n"
        "sub.ftz.f32 %f136, %f135, %f134;\r\n";
    const size_t midLen = sizeof(kMidpointBits) - 1;
    std::vector<uint8_t> patched;
    patched.reserve(n + sizeof(kTemporalInput));
    auto append = [&patched](const void* p, size_t bytes) {
        const auto* b = static_cast<const uint8_t*>(p);
        patched.insert(patched.end(), b, b + bytes);
    };
    append(ptx.data(), insertion);
    append(kTemporalInput, sizeof(kTemporalInput) - 1);
    size_t src = insertion;
    const size_t half = marks.size() / 2;
    for (size_t i = 0; i < marks.size(); ++i) {
        append(ptx.data() + src, marks[i] - src);
        const char* scale = (i < half) ? kCurrToPrev : kPrevToCurr;
        append(scale, 5);
        src = marks[i] + midLen;
    }
    append(ptx.data() + src, n - src);
    ptx.swap(patched);
    return true;
}

// Build the single-entry sm_86 fatbin for one source fatbin.
// Layout: outer header (16) + one PTX entry header (104) + padded raw PTX.
bool buildSm86Fatbin(const FatbinInfo& info, std::vector<uint8_t>& out, std::string& why) {
    std::vector<uint8_t> ptx;
    if (!extractSm89Ptx(info, ptx)) {
        why = "LZ4 decompression failed";
        return false;
    }
    if (info.temporalShape) {
        if (!correctTemporalPtx(ptx)) {
            why = "temporal midpoint correction failed";
            return false;
        }
    }
    const std::string_view text(reinterpret_cast<const char*>(ptx.data()), ptx.size());
    for (const char* marker : kUnsupportedPtxMarkers) {
        if (text.find(marker) != std::string_view::npos) {
            why = std::format("PTX contains '{}', which the Ampere compiler cannot consume", marker);
            return false;
        }
    }
    const size_t targetOffset = text.find(kSm89Target);
    if (targetOffset == std::string_view::npos) {
        why = "PTX does not declare .target sm_89";
        return false;
    }
    std::memcpy(ptx.data() + targetOffset, kSm86Target, sizeof(kSm86Target) - 1);

    const size_t padded = (ptx.size() + 7) & ~size_t{7};
    const size_t finalSize = kOuterHeader + info.entryHeaderBytes + padded;
    out.assign(finalSize, 0);
    // outer header: same magic/header size, updated payload
    std::memcpy(out.data(), info.address, kOuterHeader);
    const uint64_t payload = finalSize - kOuterHeader;
    std::memcpy(out.data() + 8, &payload, 8);
    // entry header: copy and rewrite sizes / arch / flags
    uint8_t* entry = out.data() + kOuterHeader;
    std::memcpy(entry, info.address + info.entryOffset, info.entryHeaderBytes);
    const uint64_t padded64 = padded;
    const uint32_t zero32 = 0;
    const uint64_t zero64 = 0;
    const uint32_t sm86 = kSm86;
    const uint64_t uncompressed = info.flags & ~kCompressedFlag;
    std::memcpy(entry + 8, &padded64, 8);
    std::memcpy(entry + 16, &zero32, 4);
    std::memcpy(entry + 28, &sm86, 4);
    std::memcpy(entry + 40, &uncompressed, 8);
    std::memcpy(entry + 56, &zero64, 8);
    std::memcpy(out.data() + kOuterHeader + info.entryHeaderBytes, ptx.data(), ptx.size());
    return true;
}

// ---- module and section helpers -------------------------------------------

struct ImageLayout {
    uint8_t* base = nullptr;
    const IMAGE_NT_HEADERS64* nt = nullptr;
    const IMAGE_SECTION_HEADER* sections = nullptr;
};

bool openImage(HMODULE module, ImageLayout& image) {
    if (module == nullptr) return false;
    auto* base = reinterpret_cast<uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    image.base = base;
    image.nt = nt;
    image.sections = IMAGE_FIRST_SECTION(nt);
    return true;
}

struct SlotHit {
    uint8_t* slot = nullptr;         // address of the fatbin pointer field
    const uint8_t* fatbin = nullptr;
    size_t fatbinSize = 0;
};

// Find every registration-table slot: an 8-byte field inside a read-only,
// non-executable section that points at a fatbin with one sm_89 PTX entry,
// whose record neighbours carry the audited name pointers.
std::vector<SlotHit> findSlotHits(const ImageLayout& image) {
    std::vector<SlotHit> hits;
    const size_t imageSize = image.nt->OptionalHeader.SizeOfImage;
    const auto imageStart = reinterpret_cast<uintptr_t>(image.base);
    const auto* section = image.sections;
    for (WORD i = 0; i < image.nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0) continue;
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) continue;
        uint8_t* sec = image.base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < kDescNameOffset + 8) continue;
        for (size_t off = 8; off + kDescNameOffset + 8 <= size; off += 8) {
            uint64_t value = 0;
            std::memcpy(&value, sec + off, 8);
            if (value < imageStart || value >= imageStart + imageSize) continue;
            const auto* candidate = reinterpret_cast<const uint8_t*>(value);
            if (readU32(candidate) != kFatbinMagic) continue;
            uint64_t entryName = 0, descName = 0;
            std::memcpy(&entryName, sec + off + kEntryNameOffset, 8);
            // The record's own name field sits 8 bytes before the slot; the
            // following record's first field would read past a run's end.
            std::memcpy(&descName, sec + off - 8, 8);
            if (!pointsToCString(image.base, imageSize, entryName, kEntryName)) continue;
            if (!pointsToCString(image.base, imageSize, descName, kDescriptorName)) continue;
            const size_t total = size_t(readU64(candidate + 8)) + kOuterHeader;
            if (total < 1024 || total > kMaximumFatbinBytes) continue;
            if (value + total > imageStart + imageSize) continue;
            if (value < imageStart) continue;
            FatbinInfo info{};
            if (!parseFatbin(candidate, total, info)) continue;
            hits.push_back({sec + off, candidate, total});
        }
    }
    return hits;
}

// RIP-relative `lea reg, [rip+disp32]` sites whose target is exactly one of the
// known fatbin addresses.
struct LeaSite {
    uint8_t* disp32 = nullptr;
    const uint8_t* target = nullptr;
};

std::vector<LeaSite> findLeaSites(const ImageLayout& image,
                                  const std::vector<const uint8_t*>& fatbins) {
    std::vector<LeaSite> sites;
    const auto* section = image.sections;
    for (WORD i = 0; i < image.nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        uint8_t* begin = image.base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        for (size_t off = 0; off + 7 <= size; ++off) {
            const uint8_t first = begin[off];
            const uint8_t second = begin[off + 1];
            if (!((first == 0x48 || first == 0x4C) && second == 0x8D)) continue;
            const uint8_t modrm = begin[off + 2];
            if ((modrm & 0xC7) != 0x05) continue;
            int32_t disp = 0;
            std::memcpy(&disp, begin + off + 3, 4);
            const auto target = reinterpret_cast<const uint8_t*>(
                reinterpret_cast<uintptr_t>(begin + off) + 7 + intptr_t(disp));
            if (std::find(fatbins.begin(), fatbins.end(), target) == fatbins.end()) continue;
            sites.push_back({begin + off + 3, target});
        }
    }
    return sites;
}

struct SectionInfo {
    const char* name = "";
    const uint8_t* begin = nullptr;
    size_t size = 0;
};

std::vector<SectionInfo> listedSections(const ImageLayout& image) {
    std::vector<SectionInfo> result;
    const auto* section = image.sections;
    for (WORD i = 0; i < image.nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0) continue;
        result.push_back({reinterpret_cast<const char*>(section->Name),
                          image.base + section->VirtualAddress,
                          section->Misc.VirtualSize});
    }
    return result;
}

// Collect every structurally valid fatbin in the mapped image.
std::vector<FatbinInfo> collectFatbins(const ImageLayout& image) {
    std::vector<FatbinInfo> result;
    for (const auto& section : listedSections(image)) {
        const uint8_t* begin = section.begin;
        const size_t size = section.size;
        if (size < kOuterHeader) continue;
        for (size_t off = 0; off + kOuterHeader <= size; off += 8) {
            if (readU32(begin + off) != kFatbinMagic) continue;
            const size_t total = size_t(readU64(begin + off + 8)) + kOuterHeader;
            if (total < 1024 || total > kMaximumFatbinBytes ||
                off + total > size) {
                continue;
            }
            FatbinInfo info{};
            if (!parseFatbin(begin + off, total, info)) continue;
            bool duplicate = false;
            for (const auto& existing : result) {
                if (existing.address == info.address) { duplicate = true; break; }
            }
            if (!duplicate) result.push_back(info);
        }
    }
    return result;
}

bool sectionIsRdata(const SectionInfo& section) {
    return std::strncmp(section.name, ".rdata", 6) == 0;
}

bool sectionIsData(const SectionInfo& section) {
    return std::strncmp(section.name, ".data", 5) == 0;
}

struct LeaWrite {
    uint8_t* disp32 = nullptr;
    int32_t original = 0;
};

struct GlobalState {
    std::mutex mutex;
    AmpereMfgUnlock::State state{};
    bool installed = false;
    std::vector<std::pair<uint8_t*, uint8_t>> archGateWrites;
    std::vector<std::pair<uint64_t*, uint64_t>> slotWrites;
    std::vector<LeaWrite> leaWrites;
    void* allocation = nullptr;
};

GlobalState& global() {
    static GlobalState instance;
    return instance;
}

// Patch the 0x1b0 architecture compares to 0x170 (Ampere) so 30/40/50 series
// all pass; the gate only decides the reported multi-frame ceiling.
void patchArchGates(uint8_t* base, const IMAGE_NT_HEADERS64* nt,
                    std::vector<std::pair<uint8_t*, uint8_t>>& sites) {
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        uint8_t* start = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < 6) continue;
        for (size_t off = 0; off + 6 <= size; ++off) {
            unsigned char* target = nullptr;
            if (start[off] == 0x3D && start[off + 1] == 0xB0 && start[off + 2] == 0x01 &&
                start[off + 3] == 0x00 && start[off + 4] == 0x00) {
                target = start + off + 1;
            } else if (start[off] == 0x81 && start[off + 1] >= 0xF8 &&
                       start[off + 1] <= 0xFF && start[off + 2] == 0xB0 &&
                       start[off + 3] == 0x01 && start[off + 4] == 0x00 &&
                       start[off + 5] == 0x00) {
                target = start + off + 2;
            }
            if (target == nullptr) continue;
            DWORD oldProtect = 0;
            if (VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &oldProtect) == 0) continue;
            sites.push_back({target, *target});
            *target = 0x70;  // 0x1b0 -> 0x170 (Ampere passes)
            DWORD ignored = 0;
            VirtualProtect(target, 1, oldProtect, &ignored);
            if (target + 1 - start <= LONG(size)) {
                off += 5;
            }
        }
    }
    FlushInstructionCache(GetCurrentProcess(), base, size_t(nt->OptionalHeader.SizeOfImage));
}

// Allocate the rebuilt set inside an address window that every RIP-relative
// reference can reach. Walks the address space with VirtualQuery and asks for
// a concrete free region, which avoids the VirtualAlloc2 address-requirement
// path that rejects the same request with ERROR_INVALID_PARAMETER on some
// Windows builds (observed locally with 26100).
void* allocateReachable(size_t bytes, const uint8_t* moduleBase) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(moduleBase);
    constexpr uintptr_t kReach = 0x50000000ull;  // 1.25 GiB, well inside disp32
    const uintptr_t low = base > kReach ? base - kReach : 0x10000;
    const uintptr_t high = base + kReach;
    uintptr_t address = (low + 0xFFFF) & ~uintptr_t{0xFFFF};
    while (address < high) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) !=
            sizeof(memory)) {
            break;
        }
        const auto regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
        const uintptr_t regionEnd = regionBase + memory.RegionSize;
        if (regionEnd <= address) break;
        if (memory.State == MEM_FREE && memory.RegionSize >= bytes) {
            uintptr_t candidate = (regionBase + 0xFFFF) & ~uintptr_t{0xFFFF};
            if (candidate < base - kReach) candidate = (base - kReach + 0xFFFF) & ~uintptr_t{0xFFFF};
            if (candidate + bytes <= regionEnd && candidate + bytes <= high) {
                void* result = VirtualAlloc(reinterpret_cast<void*>(candidate), bytes,
                                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (result != nullptr) return result;
            }
        }
        address = regionEnd;
    }
    // Fall back to any address; the lea publication step re-validates reach
    // and rolls everything back if a displacement would not fit.
    return VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

// CUDA driver preflight (the intent of dashdogy's ampere_cuda_program
// validation): in a private context on the active GPU, the driver must accept
// every rebuilt fatbin before any pointer is published. On an RTX 30 host this
// compiles the sm_86 programs on the real Ampere device; on other hosts it at
// least proves the PTX/JIT path is legal.
bool preflightPrograms(const std::vector<std::pair<const uint8_t*, size_t>>& programs,
                       std::string& why, size_t& loaded) {
    loaded = 0;
    HMODULE cuda = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (cuda == nullptr) {
        why = "nvcuda.dll is not available";
        return false;
    }
    // nvcuda.dll is intentionally never unloaded: the CUDA driver does not
    // support being freed while the process lives.
    const auto init = reinterpret_cast<int (*)(unsigned)>(
        reinterpret_cast<void*>(GetProcAddress(cuda, "cuInit")));
    const auto deviceGet = reinterpret_cast<int (*)(int*, int)>(
        reinterpret_cast<void*>(GetProcAddress(cuda, "cuDeviceGet")));
    const auto ctxCreate = reinterpret_cast<int (*)(void**, unsigned, int)>(
        reinterpret_cast<void*>(GetProcAddress(cuda, "cuCtxCreate_v2")));
    const auto ctxDestroy = reinterpret_cast<int (*)(void*)>(
        reinterpret_cast<void*>(GetProcAddress(cuda, "cuCtxDestroy_v2")));
    const auto moduleLoad = reinterpret_cast<int (*)(void**, const void*, unsigned, void*, void*)>(
        reinterpret_cast<void*>(GetProcAddress(cuda, "cuModuleLoadDataEx")));
    const auto moduleUnload = reinterpret_cast<int (*)(void*)>(
        reinterpret_cast<void*>(GetProcAddress(cuda, "cuModuleUnload")));
    if (init == nullptr || deviceGet == nullptr || ctxCreate == nullptr ||
        ctxDestroy == nullptr || moduleLoad == nullptr || moduleUnload == nullptr) {
        why = "nvcuda.dll does not export the required driver API";
        return false;
    }
    if (init(0) != 0) {
        why = "cuInit failed";
        return false;
    }
    int device = 0;
    if (deviceGet(&device, 0) != 0) {
        why = "cuDeviceGet failed";
        return false;
    }
    void* context = nullptr;
    if (ctxCreate(&context, 0, device) != 0) {
        why = "cuCtxCreate failed";
        return false;
    }
    bool ok = true;
    for (size_t index = 0; index < programs.size(); ++index) {
        void* module = nullptr;
        const int result = moduleLoad(&module, programs[index].first, 0, nullptr, nullptr);
        if (result != 0) {
            why = std::format("the CUDA driver rejected rebuilt program {} (cuModuleLoadDataEx={})",
                              index, result);
            ok = false;
            break;
        }
        if (module != nullptr) moduleUnload(module);
        ++loaded;
    }
    ctxDestroy(context);
    return ok;
}

} // namespace

AmpereMfgUnlock::Scan AmpereMfgUnlock::scan(HMODULE module) {
    Scan result{};
    ImageLayout image{};
    if (!openImage(module, image)) {
        result.detail = "not a valid PE image";
        return result;
    }
    result.moduleValid = true;
    result.identityMatched = image.nt->OptionalHeader.SizeOfImage == kKnownSizeOfImage &&
                             image.nt->FileHeader.TimeDateStamp == kKnownTimeDateStamp;

    // Architecture gate sites (0x1b0 compares, two encodings).
    {
        const auto* section = image.sections;
        for (WORD i = 0; i < image.nt->FileHeader.NumberOfSections; ++i, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
            const uint8_t* start = image.base + section->VirtualAddress;
            const size_t size = section->Misc.VirtualSize;
            for (size_t off = 0; off + 6 <= size; ++off) {
                if (start[off] == 0x3D && start[off + 1] == 0xB0 && start[off + 2] == 0x01 &&
                    start[off + 3] == 0x00 && start[off + 4] == 0x00) {
                    ++result.archGateSites;
                } else if (start[off] == 0x81 && start[off + 1] >= 0xF8 &&
                           start[off + 1] <= 0xFF && start[off + 2] == 0xB0 &&
                           start[off + 3] == 0x01 && start[off + 4] == 0x00 &&
                           start[off + 5] == 0x00) {
                    ++result.archGateSites;
                }
            }
        }
    }

    const auto fatbins = collectFatbins(image);
    const auto hits = findSlotHits(image);
    result.slotPointers = hits.size();

    // Debug aid: per-section fatbin counts (kept in the detail string).
    std::string perSection;
    for (const auto& section : listedSections(image)) {
        size_t count = 0;
        for (size_t off = 0; off + kOuterHeader <= section.size; off += 8) {
            if (readU32(section.begin + off) != kFatbinMagic) continue;
            const size_t total = size_t(readU64(section.begin + off + 8)) + kOuterHeader;
            if (total < 1024 || total > kMaximumFatbinBytes ||
                off + total > section.size) {
                continue;
            }
            FatbinInfo info{};
            if (parseFatbin(section.begin + off, total, info)) ++count;
        }
        perSection += std::format(" [{}:{}]", section.name, count);
    }
    perSection += std::format(" total={}", fatbins.size());

    // Validate the eight runs of 25 consecutive 48-byte-strided slots.
    {
        size_t index = 0;
        std::vector<const uint8_t*> seen;
        while (index < hits.size()) {
            size_t run = 1;
            while (index + run < hits.size() &&
                   hits[index + run].slot == hits[index + run - 1].slot + kRecordBytes) {
                ++run;
            }
            if (run == kExpectedSlotsPerRun) {
                ++result.slotRuns;
                for (size_t k = 0; k < run; ++k) {
                    const auto* fb = hits[index + k].fatbin;
                    if (std::find(seen.begin(), seen.end(), fb) == seen.end()) seen.push_back(fb);
                }
            }
            index += run;
        }
        result.programFatbins = seen.size();
    }

    // Classify the remaining fatbins: .rdata network programs vs .data aux.
    std::vector<const uint8_t*> programAddresses;
    for (const auto& hit : hits) {
        if (std::find(programAddresses.begin(), programAddresses.end(), hit.fatbin) ==
            programAddresses.end()) {
            programAddresses.push_back(hit.fatbin);
        }
    }
    std::vector<const uint8_t*> allAddresses;
    for (const auto& fb : fatbins) allAddresses.push_back(fb.address);
    for (const auto& section : listedSections(image)) {
        const auto begin = reinterpret_cast<uintptr_t>(section.begin);
        const auto end = begin + section.size;
        for (const auto& fb : fatbins) {
            const auto address = reinterpret_cast<uintptr_t>(fb.address);
            if (address < begin || address >= end) continue;
            const bool isProgram =
                std::find(programAddresses.begin(), programAddresses.end(), fb.address) !=
                programAddresses.end();
            if (isProgram) continue;
            if (sectionIsRdata(section)) {
                ++result.networkFatbins;
            } else if (std::strncmp(section.name, ".data", 5) == 0) {
                ++result.auxFatbins;
            }
        }
    }

    // RIP-relative references.
    {
        const auto sites = findLeaSites(image, allAddresses);
        result.leaSites = sites.size();
        // Each fatbin outside the registration table must be referenced once.
        size_t referencedOnce = 0;
        for (const auto& fb : fatbins) {
            const bool isProgram =
                std::find(programAddresses.begin(), programAddresses.end(), fb.address) !=
                programAddresses.end();
            if (isProgram) continue;
            const size_t count = std::count_if(
                sites.begin(), sites.end(),
                [&](const LeaSite& site) { return site.target == fb.address; });
            if (count == 1) ++referencedOnce;
        }
        result.detail = std::format(
            "runs={} slotPointers={} programFatbins={} networkFatbins={} auxFatbins={} "
            "leaSites={} uniquelyReferenced={} gateSites={}",
            result.slotRuns, result.slotPointers, result.programFatbins,
            result.networkFatbins, result.auxFatbins, sites.size(), referencedOnce,
            result.archGateSites);
        result.detail += perSection;
    }

    // The temporal slot must be unique among the program fatbins.
    {
        size_t temporalCount = 0;
        for (const auto& address : programAddresses) {
            FatbinInfo info{};
            bool found = false;
            for (const auto& fb : fatbins) {
                if (fb.address == address) { info = fb; found = true; break; }
            }
            if (!found) continue;
            std::vector<uint8_t> ptx;
            if (!extractSm89Ptx(info, ptx)) continue;
            size_t labelCount = 0;
            std::vector<size_t> midpoints;
            if (inspectTemporalShape(ptx, labelCount, midpoints)) ++temporalCount;
        }
        result.temporalSlotUnique = temporalCount == 1;
    }
    return result;
}

bool AmpereMfgUnlock::adapterIsAmpere(uint32_t vendorId, uint32_t deviceId) {
    if (vendorId != 0x10DE) return false;
    // Ampere GA10x device-id window (RTX 30 series, desktop and mobile).
    // Turing tops out below 0x2200 and Ada starts at 0x2680, so this window
    // cannot catch a different generation.
    return deviceId >= 0x2200 && deviceId < 0x2680;
}

AmpereMfgUnlock::State AmpereMfgUnlock::apply(HMODULE module) {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    State state{};
    if (module == nullptr) {
        state.detail = L"no runtime module handle";
        g.state = state;
        return state;
    }
    state.moduleFound = true;
    if (g.installed) {
        state = g.state;
        return state;
    }

    ImageLayout image{};
    if (!openImage(module, image)) {
        state.detail = L"runtime module is not a valid PE image";
        g.state = state;
        return state;
    }
    state.identityVerified = image.nt->OptionalHeader.SizeOfImage == kKnownSizeOfImage &&
                             image.nt->FileHeader.TimeDateStamp == kKnownTimeDateStamp;
    if (!state.identityVerified) {
        state.detail = L"runtime identity (SizeOfImage/TimeDateStamp) does not match the audited 310.7 build";
        g.state = state;
        return state;
    }

    auto fatbins = collectFatbins(image);
    const auto hits = findSlotHits(image);

    // Registration tables: eight runs of 25 consecutive 48-byte-strided slots.
    size_t slotRuns = 0;
    std::vector<const uint8_t*> programAddresses;
    {
        size_t index = 0;
        while (index < hits.size()) {
            size_t run = 1;
            while (index + run < hits.size() &&
                   hits[index + run].slot == hits[index + run - 1].slot + kRecordBytes) {
                ++run;
            }
            if (run == kExpectedSlotsPerRun) {
                ++slotRuns;
                for (size_t k = 0; k < run; ++k) {
                    const auto* fb = hits[index + k].fatbin;
                    if (std::find(programAddresses.begin(), programAddresses.end(), fb) ==
                        programAddresses.end()) {
                        programAddresses.push_back(fb);
                    }
                }
            }
            index += run;
        }
    }
    if (slotRuns != kExpectedSlotRuns ||
        hits.size() != kExpectedSlotRuns * kExpectedSlotsPerRun ||
        programAddresses.size() != kExpectedProgramFatbins) {
        state.detail = std::format(
            L"registration tables do not match the audited layout (runs={} pointers={} programs={})",
            slotRuns, hits.size(), programAddresses.size());
        g.state = state;
        return state;
    }

    const auto sections = listedSections(image);
    auto classify = [&](const uint8_t* address) -> int {
        if (std::find(programAddresses.begin(), programAddresses.end(), address) !=
            programAddresses.end()) {
            return 0;  // program slot
        }
        const auto value = reinterpret_cast<uintptr_t>(address);
        for (const auto& section : sections) {
            const auto begin = reinterpret_cast<uintptr_t>(section.begin);
            if (value < begin || value >= begin + section.size) continue;
            if (sectionIsRdata(section)) return 1;
            if (sectionIsData(section)) return 2;
        }
        return -1;
    };

    size_t networkCount = 0, auxCount = 0, temporalCount = 0;
    for (auto& fb : fatbins) {
        const int cls = classify(fb.address);
        if (cls == 1) ++networkCount;
        else if (cls == 2) ++auxCount;
        std::vector<uint8_t> ptx;
        size_t labelCount = 0;
        std::vector<size_t> midpoints;
        fb.temporalShape = extractSm89Ptx(fb, ptx) &&
                           inspectTemporalShape(ptx, labelCount, midpoints);
        if (fb.temporalShape) ++temporalCount;
    }
    if (fatbins.size() != kExpectedProgramFatbins + kExpectedRdataNetworkFatbins +
                             kExpectedAuxFatbins ||
        networkCount != kExpectedRdataNetworkFatbins ||
        auxCount != kExpectedAuxFatbins) {
        state.detail = std::format(
            L"fatbin inventory does not match the audited layout (total={} network={} aux={})",
            fatbins.size(), networkCount, auxCount);
        g.state = state;
        return state;
    }
    if (temporalCount != 1) {
        state.detail = std::format(L"expected exactly one temporal program, found {}", temporalCount);
        g.state = state;
        return state;
    }
    {
        bool temporalInPrograms = false;
        for (const auto& fb : fatbins) {
            if (!fb.temporalShape) continue;
            temporalInPrograms = std::find(programAddresses.begin(), programAddresses.end(),
                                           fb.address) != programAddresses.end();
        }
        if (!temporalInPrograms) {
            state.detail = L"the temporal program is not one of the registration slots";
            g.state = state;
            return state;
        }
    }

    std::vector<const uint8_t*> allAddresses;
    allAddresses.reserve(fatbins.size());
    for (const auto& fb : fatbins) allAddresses.push_back(fb.address);
    const auto leaSites = findLeaSites(image, allAddresses);
    {
        size_t leaOnNonPrograms = 0;
        for (const auto& site : leaSites) {
            if (classify(site.target) != 0) ++leaOnNonPrograms;
        }
        if (leaOnNonPrograms != kExpectedLeaSites) {
            state.detail = std::format(
                L"expected {} RIP-relative fatbin references, found {}",
                kExpectedLeaSites, leaOnNonPrograms);
            g.state = state;
            return state;
        }
    }

    state.slotRuns = slotRuns;
    state.slotPointers = hits.size();
    state.programFatbins = programAddresses.size();
    state.networkFatbins = networkCount;
    state.auxFatbins = auxCount;
    state.leaSites = leaSites.size();

    // Rebuild every fatbin as a single-entry sm_86 program (the temporal slot
    // additionally gets the midpoint correction).
    struct Replacement {
        const uint8_t* original = nullptr;
        std::vector<uint8_t> data;
    };
    std::vector<Replacement> replacements;
    replacements.reserve(fatbins.size());
    for (const auto& fb : fatbins) {
        std::vector<uint8_t> data;
        std::string why;
        if (!buildSm86Fatbin(fb, data, why)) {
            state.detail = std::format(
                L"sm_86 rebuild refused for the fatbin at RVA 0x{:X}: {}",
                reinterpret_cast<uintptr_t>(fb.address) - reinterpret_cast<uintptr_t>(image.base),
                std::wstring(why.begin(), why.end()));
            g.state = state;
            return state;
        }
        replacements.push_back({fb.address, std::move(data)});
    }

    // The driver must accept the whole rebuilt program set before anything is
    // published; a rejected program is a refusal, never a half-installed set.
    size_t preflightLoaded = 0;
    {
        std::vector<std::pair<const uint8_t*, size_t>> programData;
        programData.reserve(replacements.size());
        for (const auto& replacement : replacements) {
            programData.push_back({replacement.data.data(), replacement.data.size()});
        }
        std::string preflightWhy;
        if (!preflightPrograms(programData, preflightWhy, preflightLoaded)) {
            state.detail = std::format(L"CUDA preflight refused the rebuilt program set: {}",
                                       std::wstring(preflightWhy.begin(), preflightWhy.end()));
            g.state = state;
            return state;
        }
    }

    size_t total = 0;
    std::vector<size_t> offsets;
    offsets.reserve(replacements.size());
    for (const auto& replacement : replacements) {
        const size_t aligned = (replacement.data.size() + 63) & ~size_t{63};
        offsets.push_back(total);
        total += aligned;
    }
    void* block = allocateReachable(total, image.base);
    if (block == nullptr) {
        state.detail = L"no reachable allocation window for the rebuilt fatbins (VirtualAlloc2 within 1.5 GiB failed)";
        g.state = state;
        return state;
    }
    g.allocation = block;
    auto* cursor = static_cast<uint8_t*>(block);
    for (size_t i = 0; i < replacements.size(); ++i) {
        std::memcpy(cursor + offsets[i], replacements[i].data.data(),
                    replacements[i].data.size());
    }
    const auto newAddressOf = [&](const uint8_t* original) -> const uint8_t* {
        for (size_t i = 0; i < replacements.size(); ++i) {
            if (replacements[i].original == original) return cursor + offsets[i];
        }
        return nullptr;
    };

    auto rollbackAll = [&]() {
        for (auto it = g.leaWrites.rbegin(); it != g.leaWrites.rend(); ++it) {
            DWORD old = 0;
            if (VirtualProtect(it->disp32, 4, PAGE_EXECUTE_READWRITE, &old) == 0) continue;
            std::memcpy(it->disp32, &it->original, 4);
            DWORD ignored = 0;
            VirtualProtect(it->disp32, 4, old, &ignored);
        }
        g.leaWrites.clear();
        for (auto it = g.slotWrites.rbegin(); it != g.slotWrites.rend(); ++it) {
            DWORD old = 0;
            if (VirtualProtect(it->first, 8, PAGE_READWRITE, &old) == 0) continue;
            *it->first = it->second;
            DWORD ignored = 0;
            VirtualProtect(it->first, 8, old, &ignored);
        }
        g.slotWrites.clear();
        for (auto it = g.archGateWrites.rbegin(); it != g.archGateWrites.rend(); ++it) {
            DWORD old = 0;
            if (VirtualProtect(it->first, 1, PAGE_EXECUTE_READWRITE, &old) == 0) continue;
            *it->first = it->second;
            DWORD ignored = 0;
            VirtualProtect(it->first, 1, old, &ignored);
        }
        g.archGateWrites.clear();
        FlushInstructionCache(GetCurrentProcess(), image.base,
                              size_t(image.nt->OptionalHeader.SizeOfImage));
        if (g.allocation != nullptr) {
            VirtualFree(g.allocation, 0, MEM_RELEASE);
            g.allocation = nullptr;
        }
    };

    // 1. architecture compares (0x1b0 -> 0x170)
    patchArchGates(image.base, image.nt, g.archGateWrites);
    if (g.archGateWrites.size() < kMinArchGateSites ||
        g.archGateWrites.size() > kMaxArchGateSites) {
        const size_t found = g.archGateWrites.size();
        rollbackAll();
        state.detail = std::format(L"arch-gate site count {} outside the audited range {}-{}",
                                   found, kMinArchGateSites, kMaxArchGateSites);
        g.state = state;
        return state;
    }
    state.archGateSites = g.archGateWrites.size();
    state.archGatesPatched = true;

    // 2. registration slot pointers
    for (const auto& hit : hits) {
        const uint8_t* target = newAddressOf(hit.fatbin);
        if (target == nullptr) {
            rollbackAll();
            state.archGatesPatched = false;
            state.detail = L"internal error: rebuilt fatbin mapping missing for a slot";
            g.state = state;
            return state;
        }
        DWORD old = 0;
        if (VirtualProtect(hit.slot, 8, PAGE_READWRITE, &old) == 0) {
            rollbackAll();
            state.archGatesPatched = false;
            state.detail = L"a registration slot page is not writable; runtime left untouched";
            g.state = state;
            return state;
        }
        uint64_t original = 0;
        std::memcpy(&original, hit.slot, 8);
        g.slotWrites.push_back({reinterpret_cast<uint64_t*>(hit.slot), original});
        const uint64_t replacement = reinterpret_cast<uint64_t>(target);
        std::memcpy(hit.slot, &replacement, 8);
        DWORD ignored = 0;
        VirtualProtect(hit.slot, 8, old, &ignored);
    }

    // 3. RIP-relative references
    for (const auto& site : leaSites) {
        const uint8_t* target = newAddressOf(site.target);
        if (target == nullptr) continue;
        DWORD old = 0;
        if (VirtualProtect(site.disp32, 4, PAGE_EXECUTE_READWRITE, &old) == 0) {
            rollbackAll();
            state.archGatesPatched = false;
            state.detail = L"a code page carrying a fatbin reference is not writable; runtime left untouched";
            g.state = state;
            return state;
        }
        int32_t original = 0;
        std::memcpy(&original, site.disp32, 4);
        const int64_t displacement = intptr_t(target) -
                                     (reinterpret_cast<intptr_t>(site.disp32) + 4);
        if (displacement < INT32_MIN || displacement > INT32_MAX) {
            DWORD ignored0 = 0;
            VirtualProtect(site.disp32, 4, old, &ignored0);
            rollbackAll();
            state.archGatesPatched = false;
            state.detail = L"a rebuilt fatbin is out of reach of its RIP-relative reference; runtime left untouched";
            g.state = state;
            return state;
        }
        const int32_t replacement = static_cast<int32_t>(displacement);
        g.leaWrites.push_back({site.disp32, original});
        std::memcpy(site.disp32, &replacement, 4);
        DWORD ignored = 0;
        VirtualProtect(site.disp32, 4, old, &ignored);
    }
    FlushInstructionCache(GetCurrentProcess(), image.base,
                          size_t(image.nt->OptionalHeader.SizeOfImage));

    g.installed = true;
    state.fatbinsRedirected = true;
    state.applied = state.archGatesPatched && state.fatbinsRedirected;
    state.detail = std::format(
        L"runs={} slots={} fatbins={} lea={} gates={} preflight={}/{} (in-memory only)",
        slotRuns, hits.size(), replacements.size(), g.leaWrites.size(), state.archGateSites,
        preflightLoaded, replacements.size());
    g.state = state;
    veyra::log::info("ampere-mfg",
                     std::format("RTX 30 sm_86 unlock: {} runs / {} slot pointers, {} fatbins redirected, "
                                 "{} lea references, {} arch gates, identityVerified={}",
                                 slotRuns, hits.size(), replacements.size(), g.leaWrites.size(),
                                 state.archGateSites, state.identityVerified ? 1 : 0));
    return state;
}

void AmpereMfgUnlock::release() {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    if (!g.installed && g.archGateWrites.empty() && g.slotWrites.empty() &&
        g.leaWrites.empty() && g.allocation == nullptr) {
        return;
    }
    for (auto it = g.leaWrites.rbegin(); it != g.leaWrites.rend(); ++it) {
        DWORD old = 0;
        if (VirtualProtect(it->disp32, 4, PAGE_EXECUTE_READWRITE, &old) == 0) continue;
        std::memcpy(it->disp32, &it->original, 4);
        DWORD ignored = 0;
        VirtualProtect(it->disp32, 4, old, &ignored);
    }
    g.leaWrites.clear();
    for (auto it = g.slotWrites.rbegin(); it != g.slotWrites.rend(); ++it) {
        DWORD old = 0;
        if (VirtualProtect(it->first, 8, PAGE_READWRITE, &old) == 0) continue;
        *it->first = it->second;
        DWORD ignored = 0;
        VirtualProtect(it->first, 8, old, &ignored);
    }
    g.slotWrites.clear();
    for (auto it = g.archGateWrites.rbegin(); it != g.archGateWrites.rend(); ++it) {
        DWORD old = 0;
        if (VirtualProtect(it->first, 1, PAGE_EXECUTE_READWRITE, &old) == 0) continue;
        *it->first = it->second;
        DWORD ignored = 0;
        VirtualProtect(it->first, 1, old, &ignored);
    }
    g.archGateWrites.clear();
    if (g.allocation != nullptr) {
        VirtualFree(g.allocation, 0, MEM_RELEASE);
        g.allocation = nullptr;
    }
    g.installed = false;
    g.state = State{};
    veyra::log::info("ampere-mfg", "RTX 30 sm_86 unlock rolled back (runtime image restored)");
}

AmpereMfgUnlock::State AmpereMfgUnlock::snapshot() {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    return g.state;
}

bool AmpereMfgUnlock::applied() {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    return g.installed;
}

} // namespace veyra::ngx
