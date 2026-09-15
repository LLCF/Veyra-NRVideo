// AMD FSR frame-generation probe (diagnostic only, never shipped in the player).
//
// Loads the vendored AMD FidelityFX loader DLL, enumerates the frame-generation
// and upscaling providers and then tries to create the DX12 frame-generation
// proxy swapchain context on the local GPU. The result tells us, with evidence,
// whether the ML frame generation of FSR SDK 2.3.0 can initialise on the host
// adapter (NVIDIA in this project's test machine) or is gated to AMD hardware.
//
// Usage: veyra_fsr_probe [path-to-amd_fidelityfx_loader_dx12.dll]
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "../../third_party_local/amd/FidelityFX-SDK-2.3.0/Kits/FidelityFX/api/include/ffx_api_loader.h"
#include "../../third_party_local/amd/FidelityFX-SDK-2.3.0/Kits/FidelityFX/framegeneration/include/ffx_framegeneration.h"
#include "../../third_party_local/amd/FidelityFX-SDK-2.3.0/Kits/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.h"

using Microsoft::WRL::ComPtr;

namespace {

const char* resultName(ffxReturnCode_t code) {
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

LRESULT CALLBACK probeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void reportVersions(const ffxFunctions& functions, uint64_t createDescType, const char* label, void* device) {
    uint64_t count = 0;
    ffxQueryDescGetVersions query{};
    query.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    query.createDescType = createDescType;
    query.device = device;
    query.outputCount = &count;
    query.versionIds = nullptr;
    query.versionNames = nullptr;
    const auto countResult = functions.Query(nullptr, &query.header);
    std::printf("[fsr-probe] %s versionsQuery=%s count=%llu\n", label, resultName(countResult),
                static_cast<unsigned long long>(count));
    if (countResult != FFX_API_RETURN_OK || count == 0 || count > 32) return;
    std::vector<uint64_t> ids(count, 0);
    std::vector<const char*> names(count, nullptr);
    uint64_t capacity = count;
    query.outputCount = &capacity;
    query.versionIds = ids.data();
    query.versionNames = names.data();
    const auto listResult = functions.Query(nullptr, &query.header);
    std::printf("[fsr-probe] %s versionsList=%s returned=%llu\n", label, resultName(listResult),
                static_cast<unsigned long long>(capacity));
    for (uint64_t i = 0; i < capacity && i < ids.size(); ++i) {
        std::printf("[fsr-probe]   %s[%llu] id=%llu name=%s\n", label, static_cast<unsigned long long>(i),
                    static_cast<unsigned long long>(ids[i]), names[i] ? names[i] : "(null)");
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path loaderPath;
    if (argc > 1) loaderPath = argv[1];
    else loaderPath = std::filesystem::current_path() /
        L"third_party_local/amd/FidelityFX-SDK-2.3.0/Kits/FidelityFX/signedbin/amd_fidelityfx_loader_dx12.dll";
    std::printf("[fsr-probe] loader=%s\n", loaderPath.string().c_str());

    HMODULE loader = LoadLibraryExW(loaderPath.wstring().c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (loader == nullptr) {
        std::printf("[fsr-probe] loader load failed win32=%lu\n", GetLastError());
        return 2;
    }

    ffxFunctions functions{};
    ffxLoadFunctions(&functions, loader);
    if (functions.CreateContext == nullptr || functions.Query == nullptr || functions.DestroyContext == nullptr) {
        std::printf("[fsr-probe] loader is missing the FSR API entry points\n");
        return 3;
    }
    std::printf("[fsr-probe] FSR API entry points resolved\n");

    // Minimal D3D12 host: default adapter, one queue, hidden window, flip-model
    // swapchain descriptor identical in shape to the player's present sink.
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        std::printf("[fsr-probe] CreateDXGIFactory2 failed\n");
        return 4;
    }
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)))) {
        std::printf("[fsr-probe] no high-performance adapter\n");
        return 5;
    }
    DXGI_ADAPTER_DESC1 adapterDesc{};
    adapter->GetDesc1(&adapterDesc);
    std::wprintf(L"[fsr-probe] adapter=%s vendor=0x%04X\n", adapterDesc.Description, adapterDesc.VendorId);

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        std::printf("[fsr-probe] D3D12CreateDevice failed\n");
        return 6;
    }
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) {
        std::printf("[fsr-probe] CreateCommandQueue failed\n");
        return 7;
    }

    // Provider/version inventory runs with the real device attached: some
    // providers only advertise the ML (4.0.x) family when a device is present.
    reportVersions(functions, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12, "framegen-swapchain-forhwnd", device.Get());
    reportVersions(functions, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION, "framegen", device.Get());
    reportVersions(functions, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12, "framegen-swapchain-version", device.Get());
    reportVersions(functions, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION, "framegen-version", device.Get());

    WNDCLASSW wc{};
    wc.lpfnWndProc = probeProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"VeyraFsrProbe";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"FSR probe", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd == nullptr) {
        std::printf("[fsr-probe] CreateWindow failed\n");
        return 8;
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = 1280;
    scd.Height = 720;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 3;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    scd.Scaling = DXGI_SCALING_STRETCH;

    ComPtr<IDXGISwapChain4> swapchain;
    ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    desc.swapchain = swapchain.GetAddressOf();
    desc.hwnd = hwnd;
    desc.desc = &scd;
    desc.fullscreenDesc = nullptr;
    desc.dxgiFactory = factory.Get();
    desc.gameQueue = queue.Get();

    ffxContext context = nullptr;
    const auto createResult = functions.CreateContext(&context, &desc.header, nullptr);
    std::printf("[fsr-probe] CreateContext(framegen swapchain for hwnd) result=%s (0x%llX) context=%p swapchain=%p\n",
                resultName(createResult), static_cast<unsigned long long>(createResult),
                static_cast<void*>(context), static_cast<void*>(swapchain.Get()));
    if (context != nullptr) {
        const auto destroyResult = functions.DestroyContext(&context, nullptr);
        std::printf("[fsr-probe] DestroyContext result=%s\n", resultName(destroyResult));
    }
    DestroyWindow(hwnd);
    FreeLibrary(loader);
    return createResult == FFX_API_RETURN_OK ? 0 : 1;
}
