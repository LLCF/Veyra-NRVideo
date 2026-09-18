#include "veyra/gfx/FsrSrBackend.h"

#include "veyra/Log.h"
#include "veyra/FileIdentity.h"
#include "veyra/RuntimePaths.h"

#include <filesystem>
#include <string>

#ifdef VEYRA_HAS_FSR
#include <ffx_api_loader.h>
#include <ffx_upscale.h>
#include <dx12/ffx_api_dx12.h>
#endif

namespace veyra::gfx {

#ifdef VEYRA_HAS_FSR
namespace {

FfxApiSurfaceFormat surfaceFormatFromDxgi(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R11G11B10_FLOAT: return FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
    case DXGI_FORMAT_R16G16_FLOAT: return FFX_API_SURFACE_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32_FLOAT: return FFX_API_SURFACE_FORMAT_R32_FLOAT;
    default: return FFX_API_SURFACE_FORMAT_UNKNOWN;
    }
}

FfxApiResource makeResource(ID3D12Resource* resource, uint32_t state) {
    FfxApiResource api{};
    if (resource == nullptr) return api;
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    api.resource = resource;
    api.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    api.description.format = surfaceFormatFromDxgi(desc.Format);
    api.description.width = uint32_t(desc.Width);
    api.description.height = desc.Height;
    api.description.depth = desc.DepthOrArraySize;
    api.description.mipCount = desc.MipLevels;
    api.description.flags = 0;
    api.description.usage = FFX_API_RESOURCE_USAGE_READ_ONLY;
    if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0) {
        api.description.usage = FfxApiResourceUsage(api.description.usage | FFX_API_RESOURCE_USAGE_UAV);
    }
    api.state = state;
    return api;
}

const char* returnName(ffxReturnCode_t code) {
    switch (code) {
    case FFX_API_RETURN_OK: return "OK";
    case FFX_API_RETURN_ERROR: return "ERROR";
    case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE: return "ERROR_UNKNOWN_DESCTYPE";
    case FFX_API_RETURN_ERROR_RUNTIME_ERROR: return "ERROR_RUNTIME_ERROR";
    case FFX_API_RETURN_NO_PROVIDER: return "NO_PROVIDER";
    case FFX_API_RETURN_ERROR_MEMORY: return "ERROR_MEMORY";
    case FFX_API_RETURN_ERROR_PARAMETER: return "ERROR_PARAMETER";
    case FFX_API_RETURN_PROVIDER_NO_SUPPORT_NEW_DESCTYPE: return "PROVIDER_NO_SUPPORT_NEW_DESCTYPE";
    default: return "OTHER";
    }
}

void fsrMessage(uint32_t type, const wchar_t* message) {
    if (message == nullptr) return;
    const int length = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
    std::string text(size_t(length > 0 ? length : 0), '\0');
    if (length > 0) { WideCharToMultiByte(CP_UTF8, 0, message, -1, text.data(), length, nullptr, nullptr); text.pop_back(); }
    if (type == FFX_API_MESSAGE_TYPE_ERROR) log::error("fsr-sr", text);
    else log::warn("fsr-sr", text);
}

// The loader resolves providers by base name through the normal search order,
// so the provider DLL is pre-loaded by absolute path and the legacy DLL
// directory is pointed at the same folder for the duration of the calls.
struct DirectoryGuard {
    wchar_t previous[MAX_PATH]{};
    DWORD previousLength = 0;
    DirectoryGuard(const std::filesystem::path& directory) {
        previousLength = GetDllDirectoryW(MAX_PATH, previous);
        SetDllDirectoryW(directory.c_str());
    }
    ~DirectoryGuard() { SetDllDirectoryW(previousLength > 0 ? previous : nullptr); }
};

} // namespace
#endif

struct FsrSrBackend::Impl {
    bool available = false;
    uint64_t evaluateCount = 0;
    uint64_t failureCount = 0;
    std::string providerVersion;
    bool experimental411 = false;
#ifdef VEYRA_HAS_FSR
    HMODULE loader = nullptr;
    HMODULE provider = nullptr;
    using SetSlot = uint32_t (__cdecl*)(ffxContext*, uint32_t, uint32_t);
    SetSlot setSlot = nullptr;
    ffxFunctions functions{};
    ffxContext context = nullptr;
    uint32_t maxRenderWidth = 0, maxRenderHeight = 0;
    uint32_t maxUpscaleWidth = 0, maxUpscaleHeight = 0;
#endif
};

FsrSrBackend::FsrSrBackend() : p_(std::make_unique<Impl>()) {}

FsrSrBackend::~FsrSrBackend() { release(); }

bool FsrSrBackend::initialize(ID3D12Device* device, uint32_t maxRenderWidth, uint32_t maxRenderHeight,
                              uint32_t maxUpscaleWidth, uint32_t maxUpscaleHeight, bool hdr) {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (p.available) return true;
    const auto root = runtime::localDataDirectory() / "amd" / "fidelityfx";
    auto loaderPath = std::filesystem::absolute(root / "amd_fidelityfx_loader_dx12.dll");
    wchar_t experimentalPath[32768]{};
    const auto pathLength = GetEnvironmentVariableW(L"VEYRA_FSR41_PROVIDER", experimentalPath, 32768);
    p.experimental411 = pathLength != 0;
    if (p.experimental411) {
        if (pathLength >= 32768 || !std::filesystem::path(experimentalPath).is_absolute() || hdr) {
            log::error("fsr-sr", "experimental 4.1.1 requires an absolute provider path and SDR input");
            return false;
        }
        loaderPath = experimentalPath;
        log::warn("fsr-sr", std::format("EXPERIMENTAL FSR 4.1.1 INT8 requested: {}; zero jitter, estimated flow, constant depth", loaderPath.string()));
    }
    const auto providerPath = std::filesystem::absolute(root / "amd_fidelityfx_upscaler_dx12.dll");
    if (!std::filesystem::exists(loaderPath)) {
        log::warn("fsr-sr", std::format("loader missing at {}", loaderPath.string()));
        return false;
    }
    if (p.experimental411) {
        FileIdentity identity; IdentityError error{};
        computeFileIdentity(loaderPath.wstring(), identity, error);
        log::info("fsr-sr", std::format("experimental provider identity sha256={} bytes={} signatureValid={} trust=0x{:08X} identityStatus={}",
            identity.sha256Upper, identity.sizeBytes, identity.signatureValid, identity.winTrustError, identityErrorString(error)));
    }
    std::unique_ptr<DirectoryGuard> guard;
    if (!p.experimental411) guard = std::make_unique<DirectoryGuard>(root);
    p.loader = LoadLibraryExW(loaderPath.c_str(), nullptr,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (p.loader == nullptr) {
        log::error("fsr-sr", std::format("loader load failed win32={}", GetLastError()));
        return false;
    }
    if (!p.experimental411 && std::filesystem::exists(providerPath) &&
        (p.provider = LoadLibraryExW(providerPath.c_str(), nullptr,
                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) == nullptr) {
        log::warn("fsr-sr", std::format("provider preload failed win32={}", GetLastError()));
    }
    ffxLoadFunctions(&p.functions, p.loader);
    if (p.experimental411) {
        p.setSlot = reinterpret_cast<Impl::SetSlot>(GetProcAddress(p.loader, "veyraFsr411SetSlot"));
        if (!p.setSlot) { log::error("fsr-sr", "experimental provider lacks caller-slot/reset ABI 1"); return false; }
    }
    if (p.functions.CreateContext == nullptr || p.functions.DestroyContext == nullptr ||
        p.functions.Dispatch == nullptr || p.functions.Query == nullptr) {
        log::error("fsr-sr", "loader is missing the FidelityFX API entry points");
        return false;
    }
    {
        ffxQueryDescGetVersions versions{};
        versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versions.device = device;
        uint64_t count = 0;
        versions.outputCount = &count;
        const auto countResult = p.functions.Query(nullptr, &versions.header);
        uint64_t ids[8]{};
        const char* names[8]{};
        uint64_t capacity = 8;
        versions.outputCount = &capacity;
        versions.versionIds = ids;
        versions.versionNames = names;
        const auto listResult = countResult == FFX_API_RETURN_OK
            ? p.functions.Query(nullptr, &versions.header) : countResult;
        if (listResult == FFX_API_RETURN_OK && capacity > 0 && names[0] != nullptr) {
            p.providerVersion = names[0];
        }
        log::info("fsr-sr", std::format("providers count={} selected={}", capacity,
                                        p.providerVersion.empty() ? "none" : p.providerVersion));
        constexpr uint64_t researchId = (0xF5A5CA1Eull << 32) | (4ull << 22) | (1ull << 12) | 1;
        if (p.experimental411 && (listResult != FFX_API_RETURN_OK || capacity != 1 || ids[0] != researchId ||
                                  p.providerVersion != "4.1.1-int8-reimpl-tu117")) {
            log::error("fsr-sr", "experimental provider identity query mismatch; no implicit FSR3 fallback");
            return false;
        }
    }

    ffxCreateBackendDX12Desc backendDesc{};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.device = device;
    ffxCreateContextDescUpscaleVersion versionDesc{};
    versionDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
    versionDesc.version = FFX_UPSCALER_VERSION;
    ffxCreateContextDescUpscale upscaleDesc{};
    upscaleDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    upscaleDesc.header.pNext = &backendDesc.header;
    backendDesc.header.pNext = p.experimental411 ? nullptr : &versionDesc.header;
    upscaleDesc.flags = hdr ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0u;
    upscaleDesc.maxRenderSize = {maxRenderWidth, maxRenderHeight};
    upscaleDesc.maxUpscaleSize = {maxUpscaleWidth, maxUpscaleHeight};
    upscaleDesc.fpMessage = fsrMessage;
    auto result = p.functions.CreateContext(&p.context, &upscaleDesc.header, nullptr);
    if (result != FFX_API_RETURN_OK || p.context == nullptr) {
        log::warn("fsr-sr", std::format("context with version desc failed result={}; retrying without it",
                                        returnName(result)));
        backendDesc.header.pNext = nullptr;
        result = p.functions.CreateContext(&p.context, &upscaleDesc.header, nullptr);
    }
    if (result != FFX_API_RETURN_OK || p.context == nullptr) {
        log::error("fsr-sr", std::format("context creation failed result={} (0x{:X})", returnName(result),
                                         unsigned(result)));
        return false;
    }
    p.maxRenderWidth = maxRenderWidth;
    p.maxRenderHeight = maxRenderHeight;
    p.maxUpscaleWidth = maxUpscaleWidth;
    p.maxUpscaleHeight = maxUpscaleHeight;
    p.available = true;
    log::info("fsr-sr", std::format("provider={} maxRender={}x{} maxUpscale={}x{} hdr={}",
                                    p.providerVersion.empty() ? "unknown" : p.providerVersion,
                                    maxRenderWidth, maxRenderHeight, maxUpscaleWidth, maxUpscaleHeight, hdr));
    return true;
#else
    (void)device; (void)maxRenderWidth; (void)maxRenderHeight; (void)maxUpscaleWidth; (void)maxUpscaleHeight; (void)hdr;
    log::error("fsr-sr", "AMD FidelityFX SDK headers were unavailable at build time");
    return false;
#endif
}

bool FsrSrBackend::evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* motion,
                            ID3D12Resource* depth, ID3D12Resource* output, uint32_t renderWidth, uint32_t renderHeight,
                            uint32_t upscaleWidth, uint32_t upscaleHeight, bool reset, float frameTimeMs,
                            uint32_t commandSlot) {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (!p.available || list == nullptr || color == nullptr || motion == nullptr || depth == nullptr ||
        output == nullptr) {
        return false;
    }
    if (renderWidth > p.maxRenderWidth || renderHeight > p.maxRenderHeight ||
        upscaleWidth > p.maxUpscaleWidth || upscaleHeight > p.maxUpscaleHeight) {
        log::error("fsr-sr", std::format("extent {}x{} -> {}x{} exceeds the created ceiling", renderWidth,
                                         renderHeight, upscaleWidth, upscaleHeight));
        ++p.failureCount;
        return false;
    }
    ffxDispatchDescUpscale dispatch{};
    dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    dispatch.commandList = list;
    // Declared states are the states the caller actually set: a mismatch here
    // is what produced runtime errors in the frame-generation path.
    dispatch.color = makeResource(color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.motionVectors = makeResource(motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.depth = makeResource(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.exposure = {};
    dispatch.reactive = {};
    dispatch.transparencyAndComposition = {};
    dispatch.output = makeResource(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.jitterOffset = {0.0f, 0.0f};
    // Guidance motion is render-extent pixel space, so the factor that converts
    // it to UV inside the provider is 1:1 (same rule as the frame generator).
    dispatch.motionVectorScale = {1.0f, 1.0f};
    dispatch.renderSize = {renderWidth, renderHeight};
    dispatch.upscaleSize = {upscaleWidth, upscaleHeight};
    dispatch.enableSharpening = false;
    dispatch.sharpness = 0.0f;
    dispatch.frameTimeDelta = frameTimeMs > 0.0f ? frameTimeMs : 16.6f;
    dispatch.preExposure = 1.0f;
    dispatch.reset = reset;
    dispatch.cameraNear = 0.1f;
    dispatch.cameraFar = 1000.0f;
    dispatch.cameraFovAngleVertical = 1.0f;
    dispatch.viewSpaceToMetersFactor = 0.0f;
    dispatch.flags = 0;
    if (p.experimental411 && (commandSlot >= 6 || !p.setSlot(&p.context, commandSlot, 1))) {
        log::error("fsr-sr", "experimental dispatch requires a caller-owned, fence-retired command slot (0..5)");
        ++p.failureCount;
        return false;
    }
    const auto result = p.functions.Dispatch(&p.context, &dispatch.header);
    if (result != FFX_API_RETURN_OK) {
        log::warn("fsr-sr", std::format("dispatch result={} render={}x{} upscale={}x{} reset={}", returnName(result),
                                        renderWidth, renderHeight, upscaleWidth, upscaleHeight, reset));
        ++p.failureCount;
        return false;
    }
    ++p.evaluateCount;
    return true;
#else
    (void)list; (void)color; (void)motion; (void)depth; (void)output; (void)renderWidth; (void)renderHeight;
    (void)upscaleWidth; (void)upscaleHeight; (void)reset; (void)frameTimeMs; (void)commandSlot;
    return false;
#endif
}

void FsrSrBackend::release() {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (p.context != nullptr) {
        const auto result = p.functions.DestroyContext(&p.context, nullptr);
        log::info("fsr-sr", std::format("context destroyed result={} evaluations={} failures={}", returnName(result),
                                        p.evaluateCount, p.failureCount));
        p.context = nullptr;
    }
    if (p.loader != nullptr) {
        FreeLibrary(p.loader);
        p.loader = nullptr;
    }
    if (p.provider) { FreeLibrary(p.provider); p.provider = nullptr; }
    p.setSlot = nullptr;
    p.available = false;
#endif
}

bool FsrSrBackend::created() const { return p_->available; }
bool FsrSrBackend::experimental411() const { return p_->experimental411; }
uint64_t FsrSrBackend::evaluateCount() const { return p_->evaluateCount; }
uint64_t FsrSrBackend::failureCount() const { return p_->failureCount; }
const char* FsrSrBackend::providerVersion() const { return p_->providerVersion.c_str(); }

} // namespace veyra::gfx
