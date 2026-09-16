#include "veyra/gfx/PresentSink.h"

#include <windows.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>

#include <format>

#include "veyra/Log.h"

namespace veyra::gfx {

namespace {

const wchar_t* kWindowClassName = L"VeyraPresentSink";


} // namespace

bool PresentSink::hdrDisplayActive(HWND window){
    const auto monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
    ComPtr<IDXGIFactory1> factory;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return false;
    for(UINT a=0;;++a){ComPtr<IDXGIAdapter1> adapter;if(factory->EnumAdapters1(a,&adapter)==DXGI_ERROR_NOT_FOUND)break;if(!adapter)break;
        for(UINT i=0;;++i){ComPtr<IDXGIOutput> output;if(adapter->EnumOutputs(i,&output)==DXGI_ERROR_NOT_FOUND)break;if(!output)break;
            ComPtr<IDXGIOutput6> advanced;if(FAILED(output.As(&advanced)))continue;DXGI_OUTPUT_DESC1 desc{};
            if(SUCCEEDED(advanced->GetDesc1(&desc))&&desc.Monitor==monitor)return desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        }
    }return false;
}

PresentSink::~PresentSink()
{
    shutdown();
}

LRESULT CALLBACK PresentSink::wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        // Buffer recreation is driven by the engine loop (resize()).
        return DefWindowProcW(hwnd, msg, wp, lp);
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool PresentSink::initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
                             const Desc& desc, Status& status)
{
    device_ = device;
    shutdownCalled_ = false;
    desc_ = desc;
    width_ = desc.width;
    height_ = desc.height;

    // Register a window class once per process.
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = &PresentSink::wndProcThunk;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClassRegistered_ = RegisterClassExW(&wc) != 0;
    if (!windowClassRegistered_) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            log::error("present", std::format("RegisterClassExW failed err={}", err));
            status = Status::WindowFailure;
            return false;
        }
    }

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    AdjustWindowRect(&rect, style, FALSE);
    hwnd_ = desc.targetWindow ? desc.targetWindow : CreateWindowExW(WS_EX_OVERLAPPEDWINDOW, kWindowClassName, desc_.title.c_str(),
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) {
        log::error("present", std::format("CreateWindowExW failed err={}", GetLastError()));
        status = Status::WindowFailure;
        return false;
    }
    if (!desc.targetWindow) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);

    // Factory with tearing awareness.
    UINT factoryFlags = 0;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)))) {
        log::error("present", "CreateDXGIFactory2 failed");
        status = Status::WindowFailure;
        return false;
    }
    factory_ = factory;
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory_.As(&factory5))) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing, sizeof(allowTearing)))) {
            tearingSupported_ = allowTearing != FALSE;
        }
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = width_;
    scd.Height = height_;
    scd.Format = desc.hdr10?DXGI_FORMAT_R10G10B10A2_UNORM:desc.hdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 3;
    // Opt-in capture experiment; actual capture support depends on the
    // external capture API. Neither mode changes the pixel format.
    scd.SwapEffect = desc.captureCompatible ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    // Backend switch bookkeeping. A retained AMD proxy and a live XeSS wrapper
    // each hold the window's single flip-model swapchain; the DXGI swapchain
    // only dies once every reference (including the provider's) is gone, and
    // a second CreateSwapChainForHwnd for the same window fails while it
    // lives. Tear down whichever backend this session leaves before the next
    // one initializes, and never pass a non-empty ComPtr::GetAddressOf() to
    // an initializer (that overwrites without releasing and leaks the old
    // reference, keeping the slot occupied for the process lifetime).
    if(fsr_&&!desc.fsr&&desc.xess){
        // Deliberately keep the proxy alive. Verified on this machine: once
        // the FidelityFX proxy has wrapped the window, destroying it (even in
        // the documented order, with a drained queue and the final COM
        // reference released) leaves the window unable to host ANY later
        // swapchain - XeSS's create and native create both fail, and the
        // provider cannot recreate its own proxy either (ERROR_RUNTIME_ERROR).
        // Retaining the proxy keeps the session playable; generation is
        // switched off while the proxy presents plain frames.
        fsr_->disableGeneration();
    }
    if(xess_&&!desc.xess){
        log::info("present","releasing the XeSS swapchain");
        waitForQueueIdle();
        xess_.reset();
        for(auto& b:backBuffers_)b.Reset();
        swapChain_.Reset();
    }
    if(desc.fsr&&!fsr_){
        fsr_=std::make_unique<FsrFgPresenter>();
        IDXGISwapChain4* proxy=nullptr;
        const uint32_t renderW=desc.renderWidth?desc.renderWidth:scd.Width;
        const uint32_t renderH=desc.renderHeight?desc.renderHeight:scd.Height;
        if(!fsr_->initialize(device,queue,factory_.Get(),hwnd_,scd,renderW,renderH,&proxy,desc.fgMultiplier)){
            // Like XeSS: a missing or incompatible local runtime must never
            // prevent basic playback.
            log::warn("present", "AMD FSR frame generation unavailable; falling back to native presentation");
            fsr_.reset();
        }
    }
    if(fsr_){
        if(!desc.fsr)fsr_->disableGeneration();
        IDXGISwapChain4* proxy=fsr_->swapchainHandle();
        ComPtr<IDXGISwapChain3> proxied;
        if(proxy==nullptr||FAILED(proxy->QueryInterface(IID_PPV_ARGS(&proxied)))){
            log::error("present", "retained FSR proxy swapchain is no longer usable");
            status = Status::WindowFailure;
            return false;
        }
        swapChain_=proxied;
        log::info("present", std::format("using the retained AMD proxy swapchain (frame generation {})", desc.fsr?"on":"off"));
    }
    if(desc.xess){
        // The XeSS initializer writes through the pointer: release any
        // existing reference first so the previous swapchain (native or a
        // proxy that was kept) is actually released instead of leaked.
        if(xess_){
            // Re-initializing the same backend (a settings change): the old
            // wrapper owns the window's swapchain slot until it is destroyed.
            for(auto& b:backBuffers_)b.Reset();
            swapChain_.Reset();
            xess_.reset();
        }
        for(auto& b:backBuffers_)b.Reset();
        swapChain_.Reset();
        xess_=std::make_unique<XessPresenter>();
        if(!xess_->initialize(device,queue,factory_.Get(),hwnd_,scd,swapChain_.GetAddressOf(),desc.fgMultiplier)){
            // XeSS is an optional experimental presenter. A missing or
            // incompatible local runtime must not prevent basic playback.
            log::warn("present", "XeSS FG initialization failed; falling back to native presentation");
            // Keep a retained AMD proxy when one exists: the provider keeps
            // the HWND's DXGI swapchain alive for the process lifetime, so a
            // fresh CreateSwapChainForHwnd for the same window is known to
            // fail and the proxy still presents plain frames.
            if(!fsr_)swapChain_.Reset();
            xess_.reset();
        }
    }
    if (!swapChain_) {
    if (FAILED(factory_->CreateSwapChainForHwnd(queue, hwnd_, &scd, nullptr, nullptr, &swapChain1))) {
        log::error("present", "CreateSwapChainForHwnd failed");
        status = Status::WindowFailure;
        return false;
    }
    if (FAILED(swapChain1.As(&swapChain_))) {
        log::error("present", "QI IDXGISwapChain3 failed");
        status = Status::WindowFailure;
        return false;
    }
    }
    const auto space=desc.hdr10?DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:desc.hdr?DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    UINT support=0;const auto check=swapChain_->CheckColorSpaceSupport(space,&support);
    if(FAILED(check)||!(support&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)||FAILED(swapChain_->SetColorSpace1(space))){log::error("present","Requested output color space is unavailable");status=Status::WindowFailure;return false;}
    log::info("present",desc.hdr10?"output=HDR10 RGB10 PQ/BT2020":desc.hdr?"output=scRGB FP16 (1=80 nits)":"output=SDR RGB G22");
    // Block ALT+ENTER; the engine owns mode changes.
    (void)factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
    queue_ = queue;

    if (!refetchBackBuffers()) {
        status = Status::WindowFailure;
        return false;
    }
    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
    scBufferWidth_ = width_;
    scBufferHeight_ = height_;
    bufferExtentW_ = width_;
    bufferExtentH_ = height_;

    DXGI_SWAP_CHAIN_DESC1 actual{};
    const HRESULT descResult=swapChain_->GetDesc1(&actual);
    const bool flip=actual.SwapEffect==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL||actual.SwapEffect==DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if(FAILED(descResult)||!flip||actual.Format!=scd.Format||actual.BufferCount!=3||
       actual.Width!=scd.Width||actual.Height!=scd.Height||actual.SampleDesc.Count!=1||actual.SampleDesc.Quality!=0){
        log::error("present",std::format("swapchain contract rejected hr=0x{:X} requestedMode={} actualMode={} format={} buffers={} extent={}x{} samples={}/{}",unsigned(descResult),unsigned(scd.SwapEffect),unsigned(actual.SwapEffect),unsigned(actual.Format),actual.BufferCount,actual.Width,actual.Height,actual.SampleDesc.Count,actual.SampleDesc.Quality));
        status=Status::WindowFailure;return false;
    }
    if(actual.SwapEffect!=scd.SwapEffect)log::warn("present",std::format("swapchain mode negotiated requested={} actual={} (compatible flip model)",unsigned(scd.SwapEffect),unsigned(actual.SwapEffect)));
    swapChainFlags_=actual.Flags;
    tearingSupported_=tearingSupported_&&(actual.Flags&DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)!=0;
    log::info("present", std::format("present-sink: window {}x{} swapEffect={} buffers=3 vsync={} tearing={} captureCompatible={} (capture not verified)",
        width_, height_, actual.SwapEffect==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL?"flip-sequential":"flip-discard", desc_.vsync ? 1 : 0, tearingSupported_ ? 1 : 0, desc.captureCompatible));
    return true;
}

bool PresentSink::refetchBackBuffers()
{
    for (auto& b : backBuffers_) b.Reset();
    for (UINT i = 0; i < 3; ++i) {
        if (FAILED(swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])))) {
            log::error("present", std::format("GetBuffer({}) failed", i));
            return false;
        }
    }
    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
    return true;
}

void PresentSink::recreateSwapChain(UINT flags)
{
    (void)flags;
    // Not used anymore: ResizeBuffers failure is a hard error surfaced to
    // the engine (P0.6). Kept as a no-op stub for link compatibility.
}

bool PresentSink::processMessages(bool& windowClosed)
{
    windowClosed = false;
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            windowClosed = true;
            closed_ = true;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

ID3D12Resource* PresentSink::currentBackBuffer()
{
    return backBuffers_[backBufferIndex_].Get();
}

bool PresentSink::present(Status& status)
{
    xessFailed_=false;
    fsrFailed_=false;
    const UINT syncInterval = desc_.vsync ? 1 : 0;
    const UINT flags = (!desc_.vsync && tearingSupported_) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    ++attemptedPresentCount_;
    if(xess_&&!xess_->beforePresent()){xessFailed_=true;status=Status::WindowFailure;return false;}
    const HRESULT hr = swapChain_->Present(syncInterval, flags);
    if (SUCCEEDED(hr)) {
        ++presentCount_;
        backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
        if(xess_&&!xess_->afterPresent()){xessFailed_=true;status=Status::WindowFailure;return false;}
        if(fsr_)fsr_->afterPresent();
        return true;
    }
    ++failedPresentCount_;
    const HRESULT removed = device_ != nullptr ? device_->GetDeviceRemovedReason() : S_OK;
    log::error("present", std::format(
        "Present FAILED hr=0x{:X} sync={} flags=0x{:X} attempted={} failed={} removedReason=0x{:X}",
        static_cast<unsigned>(hr), syncInterval, flags,
        static_cast<unsigned long long>(attemptedPresentCount_),
        static_cast<unsigned long long>(failedPresentCount_),
        static_cast<unsigned>(removed)));
    // Device-lost class failures: dump DRED evidence (breadcrumbs + page
    // fault) before reporting the failure to the engine.
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
        removed != S_OK) {
        ComPtr<ID3D12DeviceRemovedExtendedData> dred;
        if (device_ != nullptr && SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&dred)))) {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT crumbs{};
            if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&crumbs))) {
                UINT count = 0;
                for (const D3D12_AUTO_BREADCRUMB_NODE* n = crumbs.pHeadAutoBreadcrumbNode;
                     n != nullptr && count < 8; n = n->pNext, ++count) {
                    log::error("present", std::format(
                        "DRED breadcrumb #{}: lastOp={} of {} on cmdList={} cmdQueue={}",
                        count, n->pLastBreadcrumbValue ? static_cast<int>(*n->pLastBreadcrumbValue) : -1,
                        n->BreadcrumbCount, n->pCommandList != nullptr ? "ptr" : "null",
                        n->pCommandQueue != nullptr ? "ptr" : "null"));
                }
                if (crumbs.pHeadAutoBreadcrumbNode == nullptr) {
                    log::error("present", "DRED breadcrumbs: none (CPU-side removal)");
                }
            }
            D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
            if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&fault))) {
                log::error("present", std::format("DRED pageFaultVA=0x{:X}",
                    static_cast<unsigned long long>(fault.PageFaultVA)));
            }
        }
        status = Status::DeviceFailure;
        return false;
    }
    status = Status::WindowFailure;
    return false;
}

bool PresentSink::waitForQueueIdle()
{
    if (queue_ == nullptr || device_ == nullptr) return true;
    ComPtr<ID3D12Fence> idleFence;
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idleFence)))) return false;
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (ev == nullptr) return false;
    bool completed=false;
    const HRESULT signal = queue_->Signal(idleFence.Get(), 1);
    const HRESULT wait = SUCCEEDED(signal) ? idleFence->SetEventOnCompletion(1, ev) : signal;
    if (SUCCEEDED(wait)) {
        const DWORD result = WaitForSingleObject(ev, 5000);
        completed=result==WAIT_OBJECT_0;
        if (result != WAIT_OBJECT_0) log::warn("present", std::format("queue idle wait result={}", result));
    } else {
        log::warn("present", std::format("queue idle setup failed hr=0x{:X}", static_cast<unsigned>(wait)));
    }
    CloseHandle(ev);
    return completed;
}

void PresentSink::resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;
    if (width == width_ && height == height_) return;
    const UINT flags = swapChainFlags_;
    // DXGI spec compliance before ResizeBuffers: wait for outstanding GPU
    // work on the presenting queue, then release ALL back-buffer references.
    if (!waitForQueueIdle()) return;
    for (auto& b : backBuffers_) b.Reset();
    HRESULT hr = swapChain_->ResizeBuffers(3, width, height,
        desc_.hdr10?DXGI_FORMAT_R10G10B10A2_UNORM:desc_.hdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM, flags);
    if (FAILED(hr)) {
        log::error("present", std::format("ResizeBuffers FAILED hr=0x{:X} (queue idle, buffers released)",
            static_cast<unsigned>(hr)));
        refetchBackBuffers();
        return;
    }
    if (!refetchBackBuffers()) return;
    width_ = width;
    height_ = height;
    scBufferWidth_ = width_;
    scBufferHeight_ = height_;
    bufferExtentW_ = width_;
    bufferExtentH_ = height_;
    pendingResize_ = true;
    log::info("present", std::format("present-sink: resized to {}x{}", width_, height_));
}

void PresentSink::shutdown()
{
    // Dependency order (s10, proven by the 0x87D matrix): backbuffers ->
    // swapchain -> window -> factory. The window must OUTLIVE the swapchain:
    // with the D3D12 debug layer active, releasing a flip swapchain whose
    // target HWND is already destroyed raises exception 0x87D in
    // KERNELBASE (WER event 1000, observed t10-L1 2026-09-04). The swapchain
    // must also be released BEFORE the D3D12 queue it presents on.
    if (shutdownCalled_) return; // second call (destructor) has nothing left
    shutdownCalled_ = true;
    // XeSS owns a proxy swapchain and may have copied tagged inputs on the
    // presenting queue. Its shutdown contract requires that work to finish.
    waitForQueueIdle();
    log::info("present", "sink-shutdown: sub-step backbuffers-release");
    for (auto& b : backBuffers_) b.Reset();
    log::info("present", "sink-shutdown: sub-step swapchain-release");
    if (swapChain_ != nullptr) {
        swapChain_->AddRef();
        const ULONG rc = swapChain_->Release();
        log::info("present", std::format("sink-shutdown: swapchain pre-release refcount={} (2 = only our ComPtr + probe)", rc));
    }
    swapChain_.Reset();
    xess_.reset();
    // The AMD proxy context is deliberately NOT destroyed here. Verified on
    // this machine: once the FidelityFX proxy wrapped the window, destroying
    // it (documented order, drained queue, final COM reference released)
    // leaves the window unable to host any later swapchain - XeSS and native
    // creation both fail and the provider cannot recreate its own proxy
    // (ERROR_RUNTIME_ERROR). Retaining the proxy keeps FSR sessions playable
    // across settings changes; it is torn down at process/engine teardown,
    // where the provider's own destructor crash was fixed by flushing its
    // presentation queue and clearing the destroyed context pointers.
    log::info("present", "sink-shutdown: sub-step window-destroy-last");
    if (hwnd_ != nullptr && !desc_.targetWindow) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
        }
    }
    log::info("present", "sink-shutdown: sub-step factory-release");
    factory_.Reset();

    device_ = nullptr;
    log::info("present", "sink-shutdown: complete");
}

} // namespace veyra::gfx
