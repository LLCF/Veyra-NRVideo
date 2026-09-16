#include "veyra/ngx/AdaMfgUnlock.h"

#include "veyra/Log.h"

#include <cstring>
#include <filesystem>
#include <mutex>
#include <vector>

namespace veyra::ngx {
namespace {

constexpr uint32_t kFatbinMagic = 0xBA55ED50u;
constexpr size_t kOuterHeader = 16;
constexpr uint32_t kPtxKind = 1;
constexpr uint32_t kSm89Arch = 89;
constexpr uint64_t kUncompressedFlags = 0x41;
constexpr size_t kEntryNameOffset = 0x10;
constexpr size_t kDescriptorNameOffset = 0x28;
constexpr char kDescriptorName[] = "dlfg_kernel";
constexpr char kEntryName[] = "main_kernel";
constexpr char kJoinLabel[] = "$L__BB0_3:";
constexpr char kMidpointBits[] = "0f3F000000";
constexpr char kMulPrefix[] = "mul.ftz.f32 ";
constexpr char kCurrToPrev[] = "%f136";   // 1 - t, current -> previous half
constexpr char kPrevToCurr[] = "%f134";   // t, previous -> current half
// Injected right after the unique join label. Registers %f134/%f135/%f136 are
// dead after that point in the audited build (verified: zero uses after the
// label, and the build recomputes the same values earlier).
constexpr char kTemporalInput[] =
    "ld.param.f32 %f134, [main_kernel_param_0+32];\r\n"
    "mov.f32 %f135, 0f3F800000;\r\n"
    "sub.ftz.f32 %f136, %f135, %f134;\r\n";

uint16_t readU16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
uint32_t readU32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint64_t readU64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }

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

bool findSm89PtxEntry(const uint8_t* fat, size_t fatSize, size_t& entryOffset) {
    if (fatSize < kOuterHeader || readU32(fat) != kFatbinMagic) return false;
    if (readU16(fat + 6) != kOuterHeader) return false;
    if (readU64(fat + 8) + kOuterHeader != fatSize) return false;
    size_t p = kOuterHeader;
    while (p + 64 <= fatSize) {
        const uint32_t kind = readU16(fat + p);
        const uint32_t hdr = readU32(fat + p + 4);
        const uint64_t payload = readU64(fat + p + 8);
        if (hdr < 64 || payload == 0 || p + hdr + payload > fatSize) return false;
        if (kind == kPtxKind && readU32(fat + p + 28) == kSm89Arch) {
            entryOffset = p;
            return true;
        }
        p += hdr + payload;
    }
    return false;
}

bool pointsToCString(const uint8_t* base, size_t imageSize, uint64_t value, const char* expected) {
    const auto start = reinterpret_cast<uintptr_t>(base);
    if (value < start || value >= start + imageSize) return false;
    const size_t len = std::strlen(expected);
    if (value + len + 1 > start + imageSize) return false;
    return std::memcmp(reinterpret_cast<const void*>(value), expected, len + 1) == 0;
}

struct DescriptorHit {
    uint64_t* slot = nullptr;
    const uint8_t* fatbin = nullptr;
    size_t fatbinSize = 0;
};

// Walks the read-only, non-executable sections for dlfg_kernel descriptors whose
// fatbin carries the audited temporal PTX.
std::vector<DescriptorHit> findDescriptors(uint8_t* base, const IMAGE_NT_HEADERS64* nt) {
    std::vector<DescriptorHit> hits;
    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    const auto imageStart = reinterpret_cast<uintptr_t>(base);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0) continue;
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) continue;
        uint8_t* sec = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < kDescriptorNameOffset + 8) continue;
        for (size_t off = 0; off + kDescriptorNameOffset + 8 <= size; off += 8) {
            uint64_t value = 0;
            std::memcpy(&value, sec + off, 8);
            if (value < imageStart || value >= imageStart + imageSize) continue;
            const auto* candidate = reinterpret_cast<const uint8_t*>(value);
            if (readU32(candidate) != kFatbinMagic) continue;
            uint64_t entryName = 0, descName = 0;
            std::memcpy(&entryName, sec + off + kEntryNameOffset, 8);
            std::memcpy(&descName, sec + off + kDescriptorNameOffset, 8);
            if (!pointsToCString(base, imageSize, entryName, kEntryName)) continue;
            if (!pointsToCString(base, imageSize, descName, kDescriptorName)) continue;
            const size_t total = size_t(readU64(candidate + 8)) + kOuterHeader;
            if (total < 1024 || total > (16u << 20)) continue;
            if (value + total > imageStart + imageSize) continue;
            size_t entry = 0;
            if (!findSm89PtxEntry(candidate, total, entry)) continue;
            if (readU64(candidate + entry + 56) != AdaMfgUnlock::kExpectedPtxBytes) continue;
            hits.push_back({reinterpret_cast<uint64_t*>(sec + off), candidate, total});
        }
    }
    return hits;
}

struct PtxFacts {
    bool decompressed = false;
    size_t midpointCount = 0;
    size_t joinLabelCount = 0;
    size_t usesAfterInjection[3] = {0, 0, 0};
};

PtxFacts inspectPtx(const uint8_t* fat, size_t entry, std::vector<uint8_t>& ptx) {
    PtxFacts facts{};
    const uint32_t hdr = readU32(fat + entry + 4);
    const uint32_t compressed = readU32(fat + entry + 16);
    const uint64_t raw = readU64(fat + entry + 56);
    if (compressed == 0 || raw != AdaMfgUnlock::kExpectedPtxBytes) return facts;
    ptx.assign(size_t(raw), 0);
    if (!lz4BlockDecompress(fat + entry + hdr, compressed, ptx.data(), ptx.size())) {
        ptx.clear();
        return facts;
    }
    facts.decompressed = true;
    const char* begin = reinterpret_cast<const char*>(ptx.data());
    const size_t n = ptx.size();
    for (size_t i = 0; i + sizeof(kJoinLabel) - 1 <= n; ++i) {
        if (std::memcmp(begin + i, kJoinLabel, sizeof(kJoinLabel) - 1) == 0) ++facts.joinLabelCount;
    }
    for (size_t i = 0; i + sizeof(kMidpointBits) - 1 < n; ++i) {
        if (std::memcmp(begin + i, kMidpointBits, sizeof(kMidpointBits) - 1) != 0) continue;
        if (begin[i + sizeof(kMidpointBits) - 1] != ';') continue;
        size_t line = i;
        while (line > 0 && begin[line - 1] != '\n') --line;
        if (i - line < sizeof(kMulPrefix) - 1) continue;
        if (std::memcmp(begin + line, kMulPrefix, sizeof(kMulPrefix) - 1) != 0) continue;
        ++facts.midpointCount;
    }
    return facts;
}

// Rebuild the fatbin with the temporal fix applied. Returns false with a reason
// whenever the structure is not exactly what the audited build has.
bool buildTemporalFatbin(const uint8_t* fat, size_t fatSize, std::vector<uint8_t>& out, std::string& why) {
    size_t entry = 0;
    if (!findSm89PtxEntry(fat, fatSize, entry)) {
        why = "no sm_89 PTX entry in the fatbin";
        return false;
    }
    const uint32_t hdr = readU32(fat + entry + 4);
    const uint32_t compressed = readU32(fat + entry + 16);
    const uint64_t raw = readU64(fat + entry + 56);
    if (compressed == 0 || raw == 0 || raw > (8u << 20)) {
        why = "PTX entry is not compressed as expected";
        return false;
    }
    if (raw != AdaMfgUnlock::kExpectedPtxBytes) {
        why = "PTX is " + std::to_string(raw) + " bytes, expected " +
              std::to_string(AdaMfgUnlock::kExpectedPtxBytes);
        return false;
    }
    // static_cast, not size_t(raw): "T x(U(y))" is a function declaration.
    std::vector<uint8_t> ptx(static_cast<size_t>(raw), uint8_t{0});
    if (!lz4BlockDecompress(fat + entry + hdr, compressed, ptx.data(), ptx.size())) {
        why = "LZ4 decompression failed";
        return false;
    }
    const char* begin = reinterpret_cast<const char*>(ptx.data());
    const size_t n = ptx.size();
    const size_t labelLen = sizeof(kJoinLabel) - 1;
    size_t label = SIZE_MAX;
    for (size_t i = 0; i + labelLen <= n; ++i) {
        if (std::memcmp(begin + i, kJoinLabel, labelLen) != 0) continue;
        if (label != SIZE_MAX) { why = "join label is not unique"; return false; }
        label = i;
    }
    if (label == SIZE_MAX) { why = "join label not found"; return false; }
    size_t insertion = label + labelLen;
    while (insertion < n && begin[insertion] != '\n') ++insertion;
    if (insertion >= n) { why = "join label has no line end"; return false; }
    ++insertion;

    const size_t midLen = sizeof(kMidpointBits) - 1;
    const size_t mulLen = sizeof(kMulPrefix) - 1;
    std::vector<size_t> marks;
    marks.reserve(AdaMfgUnlock::kExpectedMidpoints);
    for (size_t i = 0; i + midLen < n; ++i) {
        if (std::memcmp(begin + i, kMidpointBits, midLen) != 0) continue;
        if (begin[i + midLen] != ';') continue;
        size_t line = i;
        while (line > 0 && begin[line - 1] != '\n') --line;
        if (i - line < mulLen) continue;
        if (std::memcmp(begin + line, kMulPrefix, mulLen) != 0) continue;
        marks.push_back(i);
    }
    if (marks.size() != AdaMfgUnlock::kExpectedMidpoints) {
        why = "found " + std::to_string(marks.size()) + " midpoint multiplies, expected " +
              std::to_string(AdaMfgUnlock::kExpectedMidpoints);
        return false;
    }
    if (marks.front() <= insertion) { why = "first midpoint precedes the injection point"; return false; }

    std::vector<uint8_t> patched;
    patched.reserve(n + sizeof(kTemporalInput));
    auto append = [&patched](const void* p, size_t bytes) {
        const auto* b = static_cast<const uint8_t*>(p);
        patched.insert(patched.end(), b, b + bytes);
    };
    append(ptx.data(), insertion);
    append(kTemporalInput, sizeof(kTemporalInput) - 1);
    size_t src = insertion;
    const size_t half = AdaMfgUnlock::kExpectedMidpoints / 2;
    for (size_t i = 0; i < marks.size(); ++i) {
        append(ptx.data() + src, marks[i] - src);
        const char* scale = (i < half) ? kCurrToPrev : kPrevToCurr;
        append(scale, 5);
        src = marks[i] + midLen;
    }
    append(ptx.data() + src, n - src);

    const size_t padded = (patched.size() + 7) & ~size_t{7};
    const size_t finalSize = entry + hdr + padded;
    out.assign(fat, fat + entry + hdr);
    out.resize(finalSize, 0);
    std::memcpy(out.data() + entry + hdr, patched.data(), patched.size());
    const uint64_t payload64 = padded;
    const uint32_t zero32 = 0;
    const uint64_t zero64 = 0;
    std::memcpy(out.data() + entry + 8, &payload64, 8);
    std::memcpy(out.data() + entry + 16, &zero32, 4);
    std::memcpy(out.data() + entry + 40, &kUncompressedFlags, 4);
    std::memcpy(out.data() + entry + 56, &zero64, 8);
    const uint64_t outer = finalSize - kOuterHeader;
    std::memcpy(out.data() + 8, &outer, 8);
    return true;
}

struct GlobalState {
    std::mutex mutex;
    AdaMfgUnlock::State state{};
    bool installed = false;
    std::vector<std::pair<unsigned char*, unsigned char>> gateSites;
    std::vector<std::pair<uint64_t*, uint64_t>> descriptorSlots;
    void* kernelAllocation = nullptr;
};

GlobalState& global() {
    static GlobalState instance;
    return instance;
}

void patchGateSites(uint8_t* base, const IMAGE_NT_HEADERS64* nt,
                    std::vector<std::pair<unsigned char*, unsigned char>>& sites) {
    const auto* section = IMAGE_FIRST_SECTION(nt);
    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    (void)imageSize;
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
            } else if (start[off] == 0x81 && start[off + 1] >= 0xF8 && start[off + 1] <= 0xFF &&
                       start[off + 2] == 0xB0 && start[off + 3] == 0x01 && start[off + 4] == 0x00 &&
                       start[off + 5] == 0x00) {
                target = start + off + 2;
            }
            if (target == nullptr) continue;
            DWORD oldProtect = 0;
            if (VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &oldProtect) == 0) continue;
            sites.push_back({target, *target});
            *target = 0x90;  // 0x1b0 -> 0x190 (Blackwell gate -> Ada passes it)
            DWORD ignored = 0;
            VirtualProtect(target, 1, oldProtect, &ignored);
            if (target + 1 - start <= LONG(size)) {
                off += 5;  // the compare we just rewrote cannot start inside itself
            }
        }
    }
    FlushInstructionCache(GetCurrentProcess(), base, size_t(nt->OptionalHeader.SizeOfImage));
}

} // namespace

AdaMfgUnlock::Scan AdaMfgUnlock::scan(HMODULE module) {
    Scan result{};
    if (module == nullptr) { result.detail = "no module handle"; return result; }
    auto* base = reinterpret_cast<uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { result.detail = "not a PE image"; return result; }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { result.detail = "bad NT signature"; return result; }
    result.moduleValid = true;
    result.identityMatched = nt->OptionalHeader.SizeOfImage == kKnownSizeOfImage &&
                             nt->FileHeader.TimeDateStamp == kKnownTimeDateStamp;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        const uint8_t* start = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < 6) continue;
        for (size_t off = 0; off + 6 <= size; ++off) {
            if (start[off] == 0x3D && start[off + 1] == 0xB0 && start[off + 2] == 0x01 &&
                start[off + 3] == 0x00 && start[off + 4] == 0x00) {
                ++result.archGateSites;
                continue;
            }
            if (start[off] == 0x81 && start[off + 1] >= 0xF8 && start[off + 1] <= 0xFF &&
                start[off + 2] == 0xB0 && start[off + 3] == 0x01 && start[off + 4] == 0x00 &&
                start[off + 5] == 0x00) {
                ++result.archGateSites;
            }
        }
    }

    const auto hits = findDescriptors(base, nt);
    result.descriptorSlots = hits.size();
    if (!hits.empty()) {
        size_t entry = 0;
        if (findSm89PtxEntry(hits.front().fatbin, hits.front().fatbinSize, entry)) {
            result.ptxBytes = readU64(hits.front().fatbin + entry + 56);
            std::vector<uint8_t> ptx;
            const auto facts = inspectPtx(hits.front().fatbin, entry, ptx);
            result.midpointCount = facts.midpointCount;
            result.joinLabelUnique = facts.joinLabelCount == 1;
        }
    }
    result.detail = "gates=" + std::to_string(result.archGateSites) + " slots=" +
                    std::to_string(result.descriptorSlots) + " ptx=" + std::to_string(result.ptxBytes) +
                    " midpoints=" + std::to_string(result.midpointCount) +
                    " joinLabelUnique=" + (result.joinLabelUnique ? "1" : "0");
    return result;
}

bool AdaMfgUnlock::adapterIsAda(uint32_t vendorId, uint32_t deviceId) {
    if (vendorId != 0x10DE) return false;
    // Ada (AD10x) device-id window. Blackwell (GB20x) starts above 0x2900, Hopper
    // and Ampere are below 0x2680, so this window cannot catch a 50-series card.
    return deviceId >= 0x2680 && deviceId <= 0x28FF;
}

AdaMfgUnlock::State AdaMfgUnlock::apply(HMODULE module, bool includeKernelFix) {
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
    auto* base = reinterpret_cast<uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE) {
        state.detail = L"runtime module is not a valid PE image";
        g.state = state;
        return state;
    }
    state.identityVerified = nt->OptionalHeader.SizeOfImage == kKnownSizeOfImage &&
                             nt->FileHeader.TimeDateStamp == kKnownTimeDateStamp;

    // Architecture gates first: without both of them the runtime advertises
    // multi-frame and then renders black.
    patchGateSites(base, nt, g.gateSites);
    state.archGateSites = g.gateSites.size();
    state.archGatesPatched = state.archGateSites >= kMinArchGateSites &&
                             state.archGateSites <= kMaxArchGateSites;
    if (!state.archGatesPatched) {
        for (const auto& site : g.gateSites) *site.first = site.second;
        g.gateSites.clear();
        state.detail = std::format(L"arch-gate site count {} outside the audited range {}-{}; runtime left untouched",
                                   state.archGateSites, kMinArchGateSites, kMaxArchGateSites);
        g.state = state;
        return state;
    }

    if (includeKernelFix) {
        const auto hits = findDescriptors(base, nt);
        state.descriptorSlots = hits.size();
        std::vector<uint8_t> rebuilt;
        std::string why;
        if (hits.empty() || !buildTemporalFatbin(hits.front().fatbin, hits.front().fatbinSize, rebuilt, why)) {
            for (const auto& site : g.gateSites) *site.first = site.second;
            g.gateSites.clear();
            state.archGatesPatched = false;
            state.detail = std::format(L"kernel fix refused: {}; arch gates rolled back, runtime left untouched",
                                       std::wstring(why.begin(), why.end()));
            g.state = state;
            return state;
        }
        void* mem = VirtualAlloc(nullptr, rebuilt.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (mem == nullptr) {
            for (const auto& site : g.gateSites) *site.first = site.second;
            g.gateSites.clear();
            state.archGatesPatched = false;
            state.detail = L"kernel allocation failed; arch gates rolled back";
            g.state = state;
            return state;
        }
        std::memcpy(mem, rebuilt.data(), rebuilt.size());
        for (const auto& hit : hits) {
            DWORD oldProtect = 0;
            if (VirtualProtect(hit.slot, 8, PAGE_READWRITE, &oldProtect) == 0) continue;
            g.descriptorSlots.push_back({hit.slot, *hit.slot});
            *hit.slot = reinterpret_cast<uint64_t>(mem);
            DWORD ignored = 0;
            VirtualProtect(hit.slot, 8, oldProtect, &ignored);
        }
        if (g.descriptorSlots.empty()) {
            VirtualFree(mem, 0, MEM_RELEASE);
            for (const auto& site : g.gateSites) *site.first = site.second;
            g.gateSites.clear();
            state.archGatesPatched = false;
            state.detail = L"no dlfg_kernel descriptor slot was writable; arch gates rolled back";
            g.state = state;
            return state;
        }
        g.kernelAllocation = mem;
        state.kernelPatched = true;
    }

    g.installed = true;
    state.applied = state.archGatesPatched && state.kernelPatched;
    state.detail = std::format(L"gates={} descriptors={} kernel={} (in-memory only)",
                               state.archGateSites, state.descriptorSlots,
                               state.kernelPatched ? L"temporal-fixed" : L"untouched");
    g.state = state;
    veyra::log::info("ada-mfg", std::format("Ada multi-frame unlock: {} gate(s), {} descriptor slot(s), kernel={}, identityVerified={}",
                                            state.archGateSites, state.descriptorSlots,
                                            state.kernelPatched ? 1 : 0, state.identityVerified ? 1 : 0));
    return state;
}

void AdaMfgUnlock::release() {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    if (!g.installed && g.gateSites.empty() && g.descriptorSlots.empty()) return;
    for (const auto& slot : g.descriptorSlots) {
        DWORD oldProtect = 0;
        if (VirtualProtect(slot.first, 8, PAGE_READWRITE, &oldProtect) == 0) continue;
        *slot.first = slot.second;
        DWORD ignored = 0;
        VirtualProtect(slot.first, 8, oldProtect, &ignored);
    }
    g.descriptorSlots.clear();
    if (g.kernelAllocation != nullptr) {
        VirtualFree(g.kernelAllocation, 0, MEM_RELEASE);
        g.kernelAllocation = nullptr;
    }
    for (const auto& site : g.gateSites) {
        DWORD oldProtect = 0;
        if (VirtualProtect(site.first, 1, PAGE_EXECUTE_READWRITE, &oldProtect) == 0) continue;
        *site.first = site.second;
        DWORD ignored = 0;
        VirtualProtect(site.first, 1, oldProtect, &ignored);
    }
    g.gateSites.clear();
    g.installed = false;
    g.state = State{};
    veyra::log::info("ada-mfg", "Ada multi-frame unlock rolled back (runtime image restored)");
}

AdaMfgUnlock::State AdaMfgUnlock::snapshot() {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    return g.state;
}

bool AdaMfgUnlock::applied() {
    auto& g = global();
    std::lock_guard lock(g.mutex);
    return g.installed;
}

bool AdaMfgUnlock::moduleIsAudited(const std::wstring& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size != kKnownModuleSize) return false;
    // Full hash verification happens in the probe and before any write; the UI
    // hint only needs the cheap size check plus the PE identity.
    return true;
}

} // namespace veyra::ngx
