#pragma once

// PresentSink (Playbook P6.3): a Win32 window plus a DXGI flip-model
// (FLIP_DISCARD by default, optional FLIP_SEQUENTIAL, 3-buffer) swapchain used as the display output of the
// Veyra engine. The normal path NEVER reads back: the engine blits the
// finished frame (Feature 18 output, then FG, then UI) into the back buffer
// and presents. Resize recreates the buffers; vsync can be disabled for
// tearing-capable contexts.

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

#include "veyra/Result.h"
#include "veyra/gfx/XessPresenter.h"
#include "veyra/gfx/FsrFgPresenter.h"

namespace veyra::gfx {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

class PresentSink {
public:
    static bool hdrDisplayActive(HWND);
    PresentSink() = default;
    ~PresentSink();

    PresentSink(const PresentSink&) = delete;
    PresentSink& operator=(const PresentSink&) = delete;

    struct Desc {
        uint32_t width = 1280;
        uint32_t height = 720;
        bool vsync = true;
        bool waitable = false;
        bool xess = false;
        // AMD FSR frame generation: the provider creates the proxy swapchain.
        bool fsr = false;
        // Working (render) extent for the AMD provider's maxRenderSize; 0 keeps
        // it equal to the swapchain extent.
        uint32_t renderWidth = 0;
        uint32_t renderHeight = 0;
        bool captureCompatible = false;
        bool hdr = false;
        bool hdr10 = false;
        // Requested frame-generation multiplier (2 = one generated frame).
        // Only the XeSS path consumes it today; >2 requires the provider unlock.
        uint32_t fgMultiplier = 1;
        // Probe runs create their own window class name per process.
        std::wstring title = L"Veyra";
        HWND targetWindow = nullptr; // borrowed UI-owned child HWND; never destroyed by sink
    };

    // Creates a Win32 window (not shown as foreground; background probe
    // friendly) and the flip-model swapchain on the given device/queue.
    bool initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
                    const Desc& desc, Status& status);

    // Handles WM_SIZE; returns true when the swapchain buffers were resized
    // and the caller must recreate sized resources.
    bool processMessages(bool& windowClosed);

    // Returns the current back buffer (transitioned state is the caller's
    // responsibility; the buffer is in COMMON/PRESENT state on acquire).
    ID3D12Resource* currentBackBuffer(uint32_t* acquiredIndex = nullptr);

    // Presents the current back buffer. Returns false on device-lost class
    // failures (caller must trigger recovery).
    bool present(Status& status);
    struct PresentTiming {
        double beforeMs=0,callMs=0,bufferMs=0,afterMs=0;
        HRESULT result=S_OK;
    };
    const PresentTiming& lastPresentTiming()const{return presentTiming_;}
    bool configurePacing(bool enabled,bool vsync);
    bool presentationReady();
    bool pacingActive()const{return pacing_;}

    void resize(uint32_t width, uint32_t height);

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    // Actual swapchain buffer extent (equals width/height unless the DWM
    // scaling fallback is active after a resize).
    uint32_t bufferWidth() const { return bufferExtentW_; }
    uint32_t bufferHeight() const { return bufferExtentH_; }
    uint64_t presentCount() const { return presentCount_; }          // SUCCEEDED only
    uint64_t attemptedPresentCount() const { return attemptedPresentCount_; }
    uint64_t failedPresentCount() const { return failedPresentCount_; }
    bool tearingSupported() const { return tearingSupported_; }
    HWND hwnd() const { return hwnd_; }
    IDXGISwapChain3* swapChain() const { return swapChain_.Get(); }
    XessPresenter* xess() const { return xess_.get(); }
    bool xessFailed() const { return xessFailed_; }
    FsrFgPresenter* fsr() const { return fsr_.get(); }
    bool fsrFailed() const { return fsrFailed_; }

    void shutdown();

private:
    static LRESULT CALLBACK wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool refetchBackBuffers();
    bool waitForQueueIdle();
    void recreateSwapChain(UINT flags);

    HWND hwnd_ = nullptr;
    bool windowClassRegistered_ = false;
    Desc desc_{};
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint64_t presentCount_ = 0;
    uint64_t attemptedPresentCount_ = 0;
    uint64_t failedPresentCount_ = 0;
    uint32_t scBufferWidth_ = 0;
    uint32_t scBufferHeight_ = 0;
    uint32_t bufferExtentW_ = 0;
    uint32_t bufferExtentH_ = 0;
    bool tearingSupported_ = false;
    UINT swapChainFlags_ = 0;
    bool pendingResize_ = false;
    bool xessFailed_ = false;
    bool fsrFailed_ = false;
    ID3D12Device* device_ = nullptr;
    ID3D12CommandQueue* queue_ = nullptr;
    ComPtr<IDXGIFactory2> factory_;
    ComPtr<IDXGISwapChain3> swapChain_;
    std::unique_ptr<XessPresenter> xess_;
    std::unique_ptr<FsrFgPresenter> fsr_;
    ComPtr<ID3D12Resource> backBuffers_[3];
    UINT backBufferIndex_ = 0;
    bool closed_ = false;
    bool shutdownCalled_ = false;
    HANDLE latencyHandle_=nullptr;
    bool pacing_=false,capacityAcquired_=false;
    PresentTiming presentTiming_{};
};

} // namespace veyra::gfx
