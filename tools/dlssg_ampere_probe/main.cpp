// DLSS-G RTX 30 (sm_86) unlock probe (diagnostic only, never shipped).
//
// Exercises veyra_ngx::AmpereMfgUnlock against the local nvngx_dlssg.dll
// without involving NGX: identity, structural scan, then an
// apply / read-back / rollback cycle. No 30-series card is required; the
// hardware behaviour still has to be verified on RTX 30.
//
// Usage: veyra_dlssg_ampere_probe [path-to-nvngx_dlssg.dll] [--apply-test]
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "veyra/ngx/AmpereMfgUnlock.h"

namespace {

std::string toUtf8(const std::wstring& value) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string text(size_t(length > 0 ? length - 1 : 0), '\0');
    if (length > 1) WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, text.data(), length, nullptr, nullptr);
    return text;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::filesystem::path modulePath;
    bool applyTest = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--apply-test") == 0) applyTest = true;
        else modulePath = argv[i];
    }
    if (modulePath.empty()) {
        modulePath = std::filesystem::current_path() / L"runtime_local/nvidia/nvngx_dlssg.dll";
    }
    std::printf("[ampere-mfg] module=%s\n", toUtf8(modulePath.wstring()).c_str());
    if (!std::filesystem::exists(modulePath)) {
        std::printf("[ampere-mfg] module not found\n");
        return 2;
    }

    HMODULE module = LoadLibraryExW(modulePath.wstring().c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        std::printf("[ampere-mfg] LoadLibraryExW failed win32=%lu\n", GetLastError());
        return 3;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
    std::printf("[ampere-mfg] TimeDateStamp=0x%08X SizeOfImage=0x%08X (audited 0x%08X/0x%08X)\n",
                nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage,
                veyra::ngx::AmpereMfgUnlock::kKnownTimeDateStamp,
                veyra::ngx::AmpereMfgUnlock::kKnownSizeOfImage);

    const auto scan = veyra::ngx::AmpereMfgUnlock::scan(module);
    std::printf("[ampere-mfg] scan: valid=%d identity=%d fileSize=%llu\n",
                scan.moduleValid ? 1 : 0, scan.identityMatched ? 1 : 0,
                static_cast<unsigned long long>(std::filesystem::file_size(modulePath)));
    std::printf("[ampere-mfg] scan detail: %s\n", scan.detail.c_str());

    const bool structureOk =
        scan.moduleValid && scan.identityMatched &&
        scan.slotRuns == veyra::ngx::AmpereMfgUnlock::kExpectedSlotRuns &&
        scan.slotPointers == veyra::ngx::AmpereMfgUnlock::kExpectedSlotRuns *
                                 veyra::ngx::AmpereMfgUnlock::kExpectedSlotsPerRun &&
        scan.programFatbins == veyra::ngx::AmpereMfgUnlock::kExpectedProgramFatbins &&
        scan.networkFatbins == veyra::ngx::AmpereMfgUnlock::kExpectedRdataNetworkFatbins &&
        scan.auxFatbins == veyra::ngx::AmpereMfgUnlock::kExpectedAuxFatbins &&
        scan.leaSites == veyra::ngx::AmpereMfgUnlock::kExpectedLeaSites &&
        scan.archGateSites >= veyra::ngx::AmpereMfgUnlock::kMinArchGateSites &&
        scan.archGateSites <= veyra::ngx::AmpereMfgUnlock::kMaxArchGateSites &&
        scan.temporalSlotUnique;
    if (!structureOk) {
        std::printf("[ampere-mfg] VERDICT structure does not match the audited build; unlock would refuse (correct behaviour)\n");
        FreeLibrary(module);
        return 1;
    }

    if (!applyTest) {
        std::printf("[ampere-mfg] VERDICT structure verified (no patch applied; pass --apply-test to exercise it)\n");
        FreeLibrary(module);
        return 0;
    }

    const auto applied = veyra::ngx::AmpereMfgUnlock::apply(module);
    std::printf("[ampere-mfg] apply: applied=%d archGates=%d(%llu) fatbins=%d programs=%llu network=%llu aux=%llu leaSites=%llu detail=%s\n",
                applied.applied ? 1 : 0, applied.archGatesPatched ? 1 : 0,
                static_cast<unsigned long long>(applied.archGateSites),
                applied.fatbinsRedirected ? 1 : 0,
                static_cast<unsigned long long>(applied.programFatbins),
                static_cast<unsigned long long>(applied.networkFatbins),
                static_cast<unsigned long long>(applied.auxFatbins),
                static_cast<unsigned long long>(applied.leaSites),
                toUtf8(applied.detail).c_str());

    // Read-back: after the redirection a rescan must no longer see the sm_89
    // structure (the live fatbins are sm_86 singles).
    const auto afterApply = veyra::ngx::AmpereMfgUnlock::scan(module);
    std::printf("[ampere-mfg] post-apply scan: programFatbins=%llu networkFatbins=%llu auxFatbins=%llu leaSites=%llu gateSites=%llu detail=%s\n",
                static_cast<unsigned long long>(afterApply.programFatbins),
                static_cast<unsigned long long>(afterApply.networkFatbins),
                static_cast<unsigned long long>(afterApply.auxFatbins),
                static_cast<unsigned long long>(afterApply.leaSites),
                static_cast<unsigned long long>(afterApply.archGateSites),
                afterApply.detail.c_str());

    veyra::ngx::AmpereMfgUnlock::release();
    const auto afterRelease = veyra::ngx::AmpereMfgUnlock::scan(module);
    std::printf("[ampere-mfg] post-release scan: programFatbins=%llu networkFatbins=%llu auxFatbins=%llu leaSites=%llu gateSites=%llu\n",
                static_cast<unsigned long long>(afterRelease.programFatbins),
                static_cast<unsigned long long>(afterRelease.networkFatbins),
                static_cast<unsigned long long>(afterRelease.auxFatbins),
                static_cast<unsigned long long>(afterRelease.leaSites),
                static_cast<unsigned long long>(afterRelease.archGateSites));
    const bool restored =
        afterRelease.programFatbins == scan.programFatbins &&
        afterRelease.networkFatbins == scan.networkFatbins &&
        afterRelease.auxFatbins == scan.auxFatbins &&
        afterRelease.leaSites == scan.leaSites &&
        afterRelease.archGateSites == scan.archGateSites;
    std::printf("[ampere-mfg] applied=%d readBack=%d restored=%d\n",
                applied.applied ? 1 : 0,
                afterApply.programFatbins == 0 ? 1 : 0, restored ? 1 : 0);
    FreeLibrary(module);
    if (applied.applied && afterApply.programFatbins == 0 && restored) {
        std::printf("[ampere-mfg] VERDICT patch mechanics verified in memory and fully rolled back\n");
        return 0;
    }
    std::printf("[ampere-mfg] VERDICT patch cycle incomplete\n");
    return 1;
}
