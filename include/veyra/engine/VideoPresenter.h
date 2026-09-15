#pragma once
#include "veyra/diagnostics/GpuTimer.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/engine/PreviewView.h"
#include <chrono>
namespace veyra::gfx { class D3D12DeviceContext; class CommandSlotRing; }
namespace veyra::pipeline { class EnhanceGraph; }
namespace veyra::sink { struct RgbaImage; }
namespace veyra::engine {
class VideoPresenter {
public:
    bool open(gfx::D3D12DeviceContext&, HWND, pipeline::EnhanceGraph&, bool captureCompatible=false);
    bool present(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,pipeline::EnhanceGraph&,unsigned slot,bool generated,bool referencesValid=true,int comparison=0,bool baseReference=false,float split=.5f,pipeline::FrameIdentity identity={},PreviewView view={});
    void close();
    // Explicit integration-test capture only; never called by playback/export.
    bool readPresentedFrameForTest(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,sink::RgbaImage&);
    // Test-only borrowed buffer reference for lossless HDR readback. Caller
    // drains the queue and releases this before resize/close; no playback use.
    Microsoft::WRL::ComPtr<ID3D12Resource> presentedResourceForTest() const;
    uint64_t submittedCount() const {return sink_.presentCount();}
    uint64_t xessGeneratedCount() const {return sink_.xess()?sink_.xess()->generatedCount():0;}
    uint64_t xessPresentedCount() const {return sink_.xess()?sink_.xess()->presentedCount():0;}
    bool xessActive() const {return sink_.xess()!=nullptr;}
    bool xessFailed() const {return xessFailed_;}
    uint64_t fsrGeneratedCount() const {return sink_.fsr()?sink_.fsr()->generatedCount():0;}
    uint64_t fsrPresentedCount() const {return sink_.fsr()?sink_.fsr()->presentedCount():0;}
    bool fsrActive() const {return sink_.fsr()!=nullptr;}
    bool fsrFailed() const {return fsrFailed_;}
    // Provider-reported generated frames per real frame (1 = 2X); 0 when the
    // AMD runtime is unavailable.
    uint32_t fsrMaxGeneratedFrames() const {return sink_.fsr()?sink_.fsr()->maxGeneratedFrames():0;}
    // Sustained under-rate may suppress SDK-owned XeSS-FG generation over a
    // stable interval (xefgSwapChainSetEnabled); re-enabling goes through the
    // per-frame history reset, never a per-frame toggle.
    void setXessGenerationSuppressed(bool v){xessGenerationSuppressed_=v;}
    bool xessGenerationSuppressed() const {return xessGenerationSuppressed_;}
diagnostics::GpuSample blitTiming(ID3D12Fence* f,uint64_t revision=0,uint64_t epoch=0){gpuTimer_.collect(f);if((revision&&gpuTimer_.last().identity.settingsRevision!=revision)||(epoch&&gpuTimer_.last().identity.epoch!=epoch)){diagnostics::GpuSample pending;pending.state=diagnostics::SampleState::Pending;return pending;}return gpuTimer_.last().gpu[size_t(diagnostics::GpuStage::Blit)];}
std::vector<diagnostics::GpuFrameTiming> takeGpuTimings(ID3D12Fence* fence){gpuTimer_.collect(fence);return gpuTimer_.takeCompleted();}
void recordGpuTimings(){gpuTimer_.recordCompleted();}
private:
    diagnostics::GpuTimer gpuTimer_;
    gfx::PresentSink sink_;
    pipeline::GraphicsPass pass_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvs_;
    unsigned inc_=0;
    unsigned lastBuffer_=0;bool hasPresented_=false;
    HWND window_=nullptr;
    std::chrono::steady_clock::time_point lastResize_{};
    std::chrono::steady_clock::time_point lastXessFrame_{};
    pipeline::FrameIdentity lastXessIdentity_{};
    bool xessWasEnabled_=false;
    bool xessFailed_=false;
    bool xessGenerationSuppressed_=false;
    std::chrono::steady_clock::time_point lastFsrFrame_{};
    pipeline::FrameIdentity lastFsrIdentity_{};
    bool fsrWasEnabled_=false;
    bool fsrFailed_=false;
    void refresh(ID3D12Device*);
};
}
