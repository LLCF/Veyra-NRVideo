#include "veyra/engine/VideoPresenter.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
namespace veyra::engine {
bool VideoPresenter::open(gfx::D3D12DeviceContext& ctx,HWND window,pipeline::EnhanceGraph& graph,bool captureCompatible) {
    gpuTimer_.initialize(ctx.device(),ctx.directQueue());window_=window;RECT rc{};GetClientRect(window,&rc);
    gfx::PresentSink::Desc d;d.targetWindow=window;d.width=std::max(1L,rc.right);d.height=std::max(1L,rc.bottom);d.vsync=false;
    d.hdr=graph.hdrOutput();d.hdr10=graph.hdr10Output();d.xess=graph.xessEnabled();d.fsr=graph.fsrEnabled();d.renderWidth=graph.workWidth();d.renderHeight=graph.workHeight();d.captureCompatible=captureCompatible;d.fgMultiplier=graph.fgMultiplier();lastXessFrame_={};lastXessIdentity_={};xessWasEnabled_=false;lastFsrFrame_={};lastFsrIdentity_={};fsrWasEnabled_=false;
    Status st=Status::Ok;if(!sink_.initialize(ctx.device(),ctx.directQueue(),d,st))return false;
    std::vector<uint8_t> vs,ps;
    // SRV layout: 0..1 video frames, 2..11 generated frames (2 parities x 5
    // subframes, 6X), 12..15 comparison references.
    if(!pipeline::loadShaderBytes("PresentBlit_vs.dxil",vs)||!pipeline::loadShaderBytes("PresentBlit_ps.dxil",ps)||!pass_.create(ctx.device(),vs,ps,16,graph.outputFormat()))return false;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=3;
    if(FAILED(ctx.device()->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs_))))return false;
    inc_=ctx.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);refresh(ctx.device());
    for(unsigned i=0;i<12;++i)pipeline::makeSrv(ctx.device(),i<2?graph.videoFrameResource(i):graph.generatedFrameResource(i-2),graph.outputFormat(),{pass_.heap->GetCPUDescriptorHandleForHeapStart().ptr+size_t(i)*pass_.increment});
    for(unsigned i=0;i<4;++i)pipeline::makeSrv(ctx.device(),i<2?graph.sourceReference(i):graph.baseReference(i-2),DXGI_FORMAT_R16G16B16A16_FLOAT,{pass_.heap->GetCPUDescriptorHandleForHeapStart().ptr+size_t(i+12)*pass_.increment});
    return true;
}
void VideoPresenter::refresh(ID3D12Device* device){for(unsigned i=0;i<3;++i){Microsoft::WRL::ComPtr<ID3D12Resource> bb;if(SUCCEEDED(sink_.swapChain()->GetBuffer(i,IID_PPV_ARGS(&bb))))device->CreateRenderTargetView(bb.Get(),nullptr,{rtvs_->GetCPUDescriptorHandleForHeapStart().ptr+size_t(i)*inc_});}}
bool VideoPresenter::present(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,pipeline::EnhanceGraph& graph,unsigned slot,bool generated,bool referencesValid,int comparison,bool baseReference,float split,pipeline::FrameIdentity identity,PreviewView view) {
    xessFailed_=false;fsrFailed_=false;
    RECT rc{};GetClientRect(window_,&rc);if(rc.right<1||rc.bottom<1)return true;
    const auto now=std::chrono::steady_clock::now();
    const auto deferUntil=uint64_t(uintptr_t(GetPropW(window_,L"Veyra.ResizeDeferUntil")));
    if((unsigned(rc.right)!=sink_.width()||unsigned(rc.bottom)!=sink_.height())&&GetTickCount64()>=deferUntil&&now-lastResize_>=std::chrono::milliseconds(100)) {
        if(!ring.drainQueue())return false;sink_.resize(rc.right,rc.bottom);
        // A capture hook may temporarily retain a DXGI buffer. Keep the old
        // valid buffers and let DXGI scale them until a later resize succeeds.
        if(!sink_.currentBackBuffer()||FAILED(ctx.device()->GetDeviceRemovedReason()))return false;
        refresh(ctx.device());lastResize_=now;xessWasEnabled_=false;
    }
    if(sink_.xess()&&!sink_.xess()->beginFrame()){xessFailed_=true;return false;}
    Status st=Status::Ok;uint32_t commandSlot=0;auto* list=ring.acquireNext(commandSlot,st);if(!list)return false;
    gpuTimer_.frame(identity.sourceFrameId?identity:pipeline::FrameIdentity{0,0,submittedCount()+1},ctx.fence());gpuTimer_.mark(list,diagnostics::GpuStage::Blit);
    auto* source=generated?graph.generatedFrameResource(slot):graph.videoFrameResource(slot);auto* bb=sink_.currentBackBuffer();if(!bb)return false;
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for(auto& b:barriers){b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
    barriers[0].Transition.pResource=source;barriers[0].Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.pResource=bb;barriers[1].Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;list->ResourceBarrier(2,barriers);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtvs_->GetCPUDescriptorHandleForHeapStart().ptr+size_t(sink_.swapChain()->GetCurrentBackBufferIndex())*inc_};
    const float black[4]={0,0,0,1};list->ClearRenderTargetView(rtv,black,0,nullptr);list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    // DXGI stretches the retained buffer while the native workspace unfolds.
    // Compute contain in CURRENT client coordinates, then map back to buffer
    // coordinates so that the onscreen image keeps its aspect throughout.
    const float scale=std::min(float(rc.right)/graph.workWidth(),float(rc.bottom)/graph.workHeight());
    D3D12_VIEWPORT viewport{0,0,float(sink_.bufferWidth()),float(sink_.bufferHeight()),0,1};
    D3D12_RECT rect{0,0,LONG(sink_.bufferWidth()),LONG(sink_.bufferHeight())};list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);
    ID3D12DescriptorHeap* heaps[]={pass_.heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetGraphicsRootSignature(pass_.rootSig.Get());list->SetPipelineState(pass_.pso.Get());
    const float samplingFlags=graph.highQualityPresentation()?2.0f:0.0f;
    float dims[8]={float(graph.workWidth()),float(graph.workHeight()),samplingFlags,view.zoom,float(rc.right),float(rc.bottom),view.centerX,view.centerY};list->SetGraphicsRoot32BitConstants(0,8,dims,0);
    list->SetGraphicsRootDescriptorTable(1,{pass_.heap->GetGPUDescriptorHandleForHeapStart().ptr+size_t(slot+(generated?2:0))*pass_.increment});
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
    if(comparison&&!generated&&referencesValid){
        auto* reference=baseReference?graph.baseReference(slot):graph.sourceReference(slot);
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=reference;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;b.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;list->ResourceBarrier(1,&b);
        dims[2]=samplingFlags+(graph.hdr10Output()?4:graph.hdrOutput()?0:1);list->SetGraphicsRoot32BitConstants(0,8,dims,0);list->SetGraphicsRootDescriptorTable(1,{pass_.heap->GetGPUDescriptorHandleForHeapStart().ptr+size_t(12+slot+(baseReference?2:0))*pass_.increment});
        if(comparison==2)rect.right=LONG(std::clamp((rc.right*.5f+(std::clamp(split,0.0f,1.0f)-view.centerX)*graph.workWidth()*scale*view.zoom)*sink_.bufferWidth()/rc.right,0.0f,float(sink_.bufferWidth())));list->RSSetScissorRects(1,&rect);list->DrawInstanced(3,1,0,0);
        std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
        if(veyra::log::verboseFrameLogs())veyra::log::info("comparison",std::format("real-frame source={} epoch={} revision={} reference={} mode={} split={} (same leased frame)",identity.sourceFrameId,identity.epoch,identity.settingsRevision,baseReference?"base":"input",comparison,split));
    }
    for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(2,barriers);
    // Interpolation region for the present-sink FG backends (XeSS-FG, FSR-FG).
    // It must follow the PresentBlit view mapping (contain * zoom, view
    // center) rather than assuming the default centered view: the previous
    // exact `view == PreviewView{}` guard disabled generation forever after
    // any wheel zoom or drag, even when the user zoomed back out. The rect is
    // clipped to the window and aligned to even coordinates for the providers.
    const float winW=float(sink_.bufferWidth()),winH=float(sink_.bufferHeight());
    const float imgW=float(graph.workWidth()),imgH=float(graph.workHeight());
    const float viewZoom=view.zoom>0.0f?view.zoom:1.0f;
    const float viewFit=std::min(winW/imgW,winH/imgH)*viewZoom;
    const float imageLeft=winW*0.5f-view.centerX*imgW*viewFit;
    const float imageTop=winH*0.5f-view.centerY*imgH*viewFit;
    LONG fgLeft=std::max(0L,LONG(imageLeft))&~1L;
    LONG fgTop=std::max(0L,LONG(imageTop))&~1L;
    LONG fgRight=std::min(LONG(winW+0.5f),LONG(imageLeft+imgW*viewFit+0.5f))&~1L;
    LONG fgBottom=std::min(LONG(winH+0.5f),LONG(imageTop+imgH*viewFit+0.5f))&~1L;
    fgRight=std::max(fgLeft+2L,fgRight);fgBottom=std::max(fgTop+2L,fgBottom);
    const RECT fgRect{fgLeft,fgTop,fgRight,fgBottom};
    if(auto* xess=sink_.xess()){
        if(GetEnvironmentVariableW(L"VEYRA_TEST_XESS_PRESENT_FAILURE",nullptr,0)>0){
            veyra::log::warn("backend-test","Injected XeSS tagging failure with recorded commands");xessFailed_=true;return false;
        }
        const bool enabled=!generated&&!comparison&&graph.presentMotionValid(slot)&&identity.sourceFrameId!=lastXessIdentity_.sourceFrameId&&!xessGenerationSuppressed_;
        const bool reset=!xessWasEnabled_||identity.epoch!=lastXessIdentity_.epoch||identity.settingsRevision!=lastXessIdentity_.settingsRevision||graph.motionPreviousSource(slot)!=lastXessIdentity_.sourceFrameId;
        const float elapsed=lastXessFrame_==std::chrono::steady_clock::time_point{}?0.0f:float(std::chrono::duration<double,std::milli>(now-lastXessFrame_).count());
        auto* motion=graph.presentMotion(slot);
        auto* depth=graph.presentDepth();
        if(enabled){
            // XeSS-FG ONLY_NOW records its input copies on this same command
            // list. Keep the Veyra state contract explicit on both sides.
            ID3D12Resource* resources[]={bb,motion,depth};
            D3D12_RESOURCE_BARRIER toCopy[3]{};
            for(unsigned i=0;i<3;++i){
                toCopy[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toCopy[i].Transition.pResource=resources[i];
                toCopy[i].Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                toCopy[i].Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;
                toCopy[i].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
            }
            list->ResourceBarrier(3,toCopy);
            if(!xess->tag(list,bb,motion,depth,fgRect,true,reset,elapsed)){xessFailed_=true;return false;}
            for(auto& barrier:toCopy)std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
            list->ResourceBarrier(3,toCopy);
        }else if(!xess->tag(list,bb,motion,depth,fgRect,false,reset,elapsed)){xessFailed_=true;return false;}
        lastXessFrame_=now;lastXessIdentity_=identity;xessWasEnabled_=enabled;
    }
    if(auto* fsr=sink_.fsr()){
        const bool enabled=!generated&&!comparison&&graph.presentMotionValid(slot)&&identity.sourceFrameId!=lastFsrIdentity_.sourceFrameId;
        const bool reset=!fsrWasEnabled_||identity.epoch!=lastFsrIdentity_.epoch||identity.settingsRevision!=lastFsrIdentity_.settingsRevision||graph.motionPreviousSource(slot)!=lastFsrIdentity_.sourceFrameId;
        const float elapsed=lastFsrFrame_==std::chrono::steady_clock::time_point{}?0.0f:float(std::chrono::duration<double,std::milli>(now-lastFsrFrame_).count());
        // The AMD presenter degrades inside the provider (generation off, plain
        // presentation continues) instead of failing the frame: tearing the
        // swapchain down would only cost the session a restart.
        if(!fsr->tag(list,bb,graph.presentMotion(slot),graph.presentDepth(),fgRect,enabled,reset,elapsed))fsrFailed_=true;
        if(sink_.fsr()->failed())fsrFailed_=true;
        lastFsrFrame_=now;lastFsrIdentity_=identity;fsrWasEnabled_=enabled;
    }
    gpuTimer_.mark(list,diagnostics::GpuStage::Blit,true);gpuTimer_.resolve(list);
    if(!ring.submitAndSignal(commandSlot))return false;gpuTimer_.submitted(ring.lastSignaledValue());lastBuffer_=sink_.swapChain()->GetCurrentBackBufferIndex();hasPresented_=true;
    const bool presented=sink_.present(st);xessFailed_=sink_.xessFailed();fsrFailed_=fsrFailed_||sink_.fsrFailed();return presented;
}
bool VideoPresenter::readPresentedFrameForTest(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,sink::RgbaImage& image){
    if(!hasPresented_||!sink_.swapChain()||!ring.drainQueue())return false;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    return SUCCEEDED(sink_.swapChain()->GetBuffer(lastBuffer_,IID_PPV_ARGS(&buffer)))&&sink::readRgba8(ctx,ring,buffer.Get(),image);
}
void VideoPresenter::close(){hasPresented_=false;gpuTimer_.close();rtvs_.Reset();pass_={};sink_.shutdown();}
Microsoft::WRL::ComPtr<ID3D12Resource> VideoPresenter::presentedResourceForTest() const {
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    if(hasPresented_&&sink_.swapChain())sink_.swapChain()->GetBuffer(lastBuffer_,IID_PPV_ARGS(&buffer));
    return buffer;
}
}
