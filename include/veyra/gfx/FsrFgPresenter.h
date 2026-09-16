#pragma once

// AMD FidelityFX Super Resolution frame generation behind the Veyra present
// path. The provider creates (and owns) a proxy IDXGISwapChain4 for our HWND;
// the engine renders into that proxy and calls Present on it, and the provider
// inserts interpolated frames according to the per-frame dispatch we record.
//
// Evidence and limits (local RTX 5070, FSR SDK 2.3.0, provider 3.1.7/3.1.6):
// docs/FRAMEGEN_FSR_DOLBY_STATUS_2026-09-16.md and tools/fsr_probe. The 3.1.x
// provider delivers exactly one generated frame per presented frame, so the
// requested multiplier is capped at 2X for this provider.

#include <d3d12.h>
#include <dxgi1_6.h>

#include <cstdint>
#include <memory>

namespace veyra::gfx {

class FsrFgPresenter {
public:
    FsrFgPresenter();
    ~FsrFgPresenter();

    FsrFgPresenter(const FsrFgPresenter&) = delete;
    FsrFgPresenter& operator=(const FsrFgPresenter&) = delete;

    // Creates the AMD proxy swapchain for the window. On success `swapchain`
    // receives a BORROWED pointer: the provider owns one reference and releases
    // it when this object destroys its context, so a caller that wants to keep
    // the swapchain must take its own reference (QueryInterface/AddRef) and
    // release it before the presenter is destroyed.
    bool initialize(ID3D12Device* device, ID3D12CommandQueue* queue, IDXGIFactory2* factory, HWND window,
                    const DXGI_SWAP_CHAIN_DESC1& desc, uint32_t renderWidth, uint32_t renderHeight,
                    IDXGISwapChain4** swapchain, uint32_t fgMultiplier);

    // Records one frame of frame-generation work on the caller's command list:
    // prepare dispatch (motion + depth), per-frame configure and the
    // interpolation dispatch. `motion` is the guidance motion in working-extent
    // pixels (previous = current + motion), `depth` is the guidance depth.
    bool tag(ID3D12GraphicsCommandList* list, ID3D12Resource* backBuffer, ID3D12Resource* motion,
             ID3D12Resource* depth, RECT region, bool enabled, bool reset, float elapsedMs);

    // Frame ids must advance by exactly one per presented frame, including the
    // frames that carry no generation work.
    void afterPresent();

    uint64_t generatedCount() const;
    uint64_t presentedCount() const;
    // Generated frames per real frame the provider delivers (1 = 2X) once a
    // context exists; 0 while unavailable.
    uint32_t maxGeneratedFrames() const;
    bool available() const;
    bool failed() const;
    const char* providerVersion() const;
    // Borrowed proxy swapchain (valid while this object lives).
    IDXGISwapChain4* swapchainHandle() const;
    // Turns generation off while the proxy keeps presenting real frames. Used
    // when the session stops using AMD frame generation but the swapchain has
    // to stay: the provider retains the DXGI swapchain, so a second swapchain
    // cannot be created for the same HWND in this process.
    void disableGeneration();

    // Tears the provider contexts down and releases the proxy swapchain so a
    // later session can create a new swapchain for the same window (the
    // window's flip-model slot is only freed once every reference, including
    // the provider's, is gone). Idempotent; the destructor reuses it.
    void shutdown();

    // Implementation state; public because the provider's presentation
    // callback (a free function in the translation unit) must reach it.
    struct Impl;

private:
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::gfx
