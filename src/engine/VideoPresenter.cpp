#include "veyra/engine/VideoPresenter.h"
#include "veyra/engine/PresentationGeometry.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
namespace veyra::engine {
bool VideoPresenter::beginSourceInput(){
    auto* xess=sink_.xess();if(!xess)return true;
    // Waiting/filtered inputs keep the same preparation ID. Do not sleep again
    // until one accepted source has been submitted to the enhancement graph.
    if(!xessInputId_)xessInputId_=xess->beginInput();
    return xessInputId_!=0;
}
bool VideoPresenter::beginSourceProcessing(){
    auto* xess=sink_.xess();return !xess||(beginSourceInput()&&xess->beginProcessing(xessInputId_));
}
void VideoPresenter::sourceProcessed(pipeline::FrameIdentity identity){
    if(!xessInputId_)return;
    xessWork_[xessWorkPosition_]={identity,xessInputId_};
    xessWorkPosition_=(xessWorkPosition_+1)%xessWork_.size();xessInputId_=0;
}
bool VideoPresenter::open(gfx::D3D12DeviceContext& ctx,HWND window,pipeline::EnhanceGraph& graph,bool captureCompatible) {
    close();
    viewWidth_=viewHeight_=0;bufferMonitor_=nullptr;monitorWidth_=monitorHeight_=0;
    ++generation_;
    Status st=Status::Ok;
    if(graph.fgEnabled()&&!graph.xessEnabled()&&!graph.fsrEnabled()){
        D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        HRESULT hr=ctx.device()->CreateCommandQueue(&desc,IID_PPV_ARGS(&presentationQueue_));
        if(SUCCEEDED(hr))hr=ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&presentationFence_));
        if(FAILED(hr)){veyra::log::error("present",std::format("DLSS presentation queue/fence hr=0x{:X}",unsigned(hr)));close();return false;}
        presentationEvent_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!presentationEvent_||!presentationRing_.initialize(ctx.device(),presentationQueue_.Get(),presentationFence_.Get(),presentationEvent_,3,st)){close();return false;}
        veyra::log::info("present","DLSS presentation uses independent queue/fence, 3 command slots; shared textures use producer/consumer GPU fences");
    }
    auto* queue=presentationQueue_?presentationQueue_.Get():ctx.directQueue();
    gpuTimer_.initialize(ctx.device(),queue);window_=window;RECT rc{};GetClientRect(window,&rc);
    gfx::PresentSink::Desc d;d.targetWindow=window;d.width=std::max(1L,rc.right);d.height=std::max(1L,rc.bottom);d.vsync=false;
    d.waitable=!GetEnvironmentVariableW(L"VEYRA_TEST_LEGACY_SWAPCHAIN",nullptr,0);
    d.hdr=graph.hdrOutput();d.hdr10=graph.hdr10Output();d.xess=graph.xessEnabled();d.fsr=graph.fsrEnabled();d.renderWidth=graph.workWidth();d.renderHeight=graph.workHeight();d.captureCompatible=captureCompatible;d.fgMultiplier=graph.fgMultiplier();lastXessFrame_={};lastXessIdentity_={};xessWasEnabled_=false;lastFsrFrame_={};lastFsrIdentity_={};fsrWasEnabled_=false;
    if(d.xess){
        bufferMonitor_=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(bufferMonitor_,&monitor)){
            monitorWidth_=monitor.rcMonitor.right-monitor.rcMonitor.left;monitorHeight_=monitor.rcMonitor.bottom-monitor.rcMonitor.top;
            d.width=std::max(d.width,monitorWidth_);d.height=std::max(d.height,monitorHeight_);
        }
        veyra::log::info("present",std::format("XeSS retained buffers {}x{} client={}x{}; window scaling avoids provider rebuild",d.width,d.height,rc.right,rc.bottom));
    }
    if(!sink_.initialize(ctx.device(),queue,d,st))return false;
    std::vector<uint8_t> vs,ps;
    // SRV layout: 0..1 video frames, 2..11 generated frames (2 parities x 5
    // subframes, 6X), 12..15 comparison references.
    if(!pipeline::loadShaderBytes("PresentBlit_vs.dxil",vs)||!pipeline::loadShaderBytes("PresentBlit_ps.dxil",ps)||!pass_.create(ctx.device(),vs,ps,16,graph.outputFormat()))return false;
    if(sink_.xess()){
        std::vector<uint8_t> cs;
        if(!pipeline::loadShaderBytes("PresentMotion.dxil",cs)||!presentMotionPass_.create(ctx.device(),cs,4,1,1,16))return false;
        const auto heap=presentMotionPass_.heap->GetCPUDescriptorHandleForHeapStart();
        for(unsigned i=0;i<2;++i){
            presentMotion_[i]=pipeline::makeTexture(ctx.device(),graph.workWidth(),graph.workHeight(),DXGI_FORMAT_R16G16_FLOAT,true);
            if(!presentMotion_[i])return false;
            presentMotion_[i]->SetName(L"Veyra XeSS display motion");
            pipeline::makeSrv(ctx.device(),graph.presentMotion(i),DXGI_FORMAT_R16G16_FLOAT,{heap.ptr+size_t(i*2)*presentMotionPass_.increment});
            pipeline::makeUav(ctx.device(),presentMotion_[i].Get(),DXGI_FORMAT_R16G16_FLOAT,{heap.ptr+size_t(i*2+1)*presentMotionPass_.increment});
        }
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=3;
    if(FAILED(ctx.device()->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs_))))return false;
    inc_=ctx.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);refresh(ctx.device());
    for(unsigned i=0;i<12;++i)pipeline::makeSrv(ctx.device(),i<2?graph.videoFrameResource(i):graph.generatedFrameResource(i-2),graph.outputFormat(),{pass_.heap->GetCPUDescriptorHandleForHeapStart().ptr+size_t(i)*pass_.increment});
    for(unsigned i=0;i<4;++i)pipeline::makeSrv(ctx.device(),i<2?graph.sourceReference(i):graph.baseReference(i-2),DXGI_FORMAT_R16G16B16A16_FLOAT,{pass_.heap->GetCPUDescriptorHandleForHeapStart().ptr+size_t(i+12)*pass_.increment});
    return true;
}
void VideoPresenter::refresh(ID3D12Device* device){for(unsigned i=0;i<3;++i){Microsoft::WRL::ComPtr<ID3D12Resource> bb;if(SUCCEEDED(sink_.swapChain()->GetBuffer(i,IID_PPV_ARGS(&bb))))device->CreateRenderTargetView(bb.Get(),nullptr,{rtvs_->GetCPUDescriptorHandleForHeapStart().ptr+size_t(i)*inc_});}}
bool VideoPresenter::present(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& sharedRing,pipeline::EnhanceGraph& graph,unsigned slot,bool generated,bool referencesValid,int comparison,bool baseReference,float split,pipeline::FrameIdentity identity,PreviewView view) {
    auto& ring=presentationQueue_?presentationRing_:sharedRing;
    auto* fence=presentationFence_?presentationFence_.Get():ctx.fence();
    const auto presentStart=std::chrono::steady_clock::now();
    const auto slotWaitStart=ring.cpuWaitMilliseconds();
    xessFailed_=false;fsrFailed_=false;
    RECT rc{};GetClientRect(window_,&rc);if(rc.right<1||rc.bottom<1)return true;
    const auto now=std::chrono::steady_clock::now();
    bool resized=false;
    const auto deferUntil=uint64_t(uintptr_t(GetPropW(window_,L"Veyra.ResizeDeferUntil")));
    if(viewWidth_!=unsigned(rc.right)||viewHeight_!=unsigned(rc.bottom)){
        viewWidth_=rc.right;viewHeight_=rc.bottom;xessWasEnabled_=fsrWasEnabled_=false;
    }
    unsigned targetWidth=rc.right,targetHeight=rc.bottom;
    if(sink_.xess()){
        const auto monitor=MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST);
        if(monitor!=bufferMonitor_){
            bufferMonitor_=monitor;MONITORINFO info{sizeof(info)};
            if(GetMonitorInfoW(monitor,&info)){monitorWidth_=info.rcMonitor.right-info.rcMonitor.left;monitorHeight_=info.rcMonitor.bottom-info.rcMonitor.top;}
        }
        // Never shrink the provider's working set on ordinary window changes.
        // Grow only when the client or a new display needs more output pixels.
        targetWidth=std::max({targetWidth,sink_.width(),monitorWidth_});
        targetHeight=std::max({targetHeight,sink_.height(),monitorHeight_});
    }
    // The UI keeps pumping messages during the native move loop. Retain and
    // stretch current buffers there instead of repeatedly draining the GPU
    // and rebuilding provider resources at monitor/DPI boundaries.
    const bool windowChanging=GetPropW(window_,L"Veyra.InteractiveMove")||GetPropW(window_,L"Veyra.DpiTransition");
    if(!windowChanging&&(targetWidth!=sink_.width()||targetHeight!=sink_.height())&&GetTickCount64()>=deferUntil&&now-lastResize_>=std::chrono::milliseconds(100)) {
        resized=true;
        if(!ring.drainQueue())return false;sink_.resize(targetWidth,targetHeight);
        // A capture hook may temporarily retain a DXGI buffer. Keep the old
        // valid buffers and let DXGI scale them until a later resize succeeds.
        if(!sink_.currentBackBuffer()||FAILED(ctx.device()->GetDeviceRemovedReason()))return false;
        refresh(ctx.device());lastResize_=now;xessWasEnabled_=false;
    }
    const auto resizeEnd=std::chrono::steady_clock::now();
    if(auto* xess=sink_.xess()){
        uint32_t preparedId=0;
        for(auto& work:xessWork_)if(work.id&&work.identity==identity){preparedId=work.id;work.id=0;break;}
        if(!xess->beginFrame(preparedId)){xessFailed_=true;return false;}
    }
    const auto beginEnd=std::chrono::steady_clock::now();
    if(presentationQueue_){
        const HRESULT hr=presentationQueue_->Wait(ctx.fence(),graph.presentationReadyFence(slot,generated));
        if(FAILED(hr)){veyra::log::error("present",std::format("producer handoff wait hr=0x{:X}",unsigned(hr)));return false;}
    }
    Status st=Status::Ok;uint32_t commandSlot=0;auto* list=ring.acquireNext(commandSlot,st);if(!list)return false;
    gpuTimer_.frame(identity.sourceFrameId?identity:pipeline::FrameIdentity{0,0,submittedCount()+1},fence);gpuTimer_.mark(list,diagnostics::GpuStage::Blit);
    uint32_t backBufferIndex=0;
    auto* source=generated?graph.generatedFrameResource(slot):graph.videoFrameResource(slot);auto* bb=sink_.currentBackBuffer(&backBufferIndex);if(!bb)return false;
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for(auto& b:barriers){b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
    barriers[0].Transition.pResource=source;barriers[0].Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.pResource=bb;barriers[1].Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;list->ResourceBarrier(2,barriers);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtvs_->GetCPUDescriptorHandleForHeapStart().ptr+size_t(backBufferIndex)*inc_};
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
        dims[2]=samplingFlags+(graph.hdr10Output()?4:graph.hdrOutput()?0:1)+(graph.videoHdrActive()?8:0);list->SetGraphicsRoot32BitConstants(0,8,dims,0);list->SetGraphicsRootDescriptorTable(1,{pass_.heap->GetGPUDescriptorHandleForHeapStart().ptr+size_t(12+slot+(baseReference?2:0))*pass_.increment});
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
    const RECT fgRect=presentationRegion(graph.workWidth(),graph.workHeight(),rc.right,rc.bottom,sink_.bufferWidth(),sink_.bufferHeight(),view);
    const bool regionVisible=fgRect.right>fgRect.left&&fgRect.bottom>fgRect.top;
    if(auto* xess=sink_.xess()){
        if(GetEnvironmentVariableW(L"VEYRA_TEST_XESS_PRESENT_FAILURE",nullptr,0)>0){
            veyra::log::warn("backend-test","Injected XeSS tagging failure with recorded commands");xessFailed_=true;return false;
        }
        // A history break is not a user disable: the graph supplies initialized
        // zero motion in this case. Keep the current color in XeSS history so
        // the next continuous frame can interpolate without another warmup.
        const bool enabled=regionVisible&&!generated&&!comparison&&graph.presentMotion(slot)&&graph.presentDepth()&&identity.sourceFrameId!=lastXessIdentity_.sourceFrameId&&!xessGenerationSuppressed_;
        const bool reset=!graph.presentMotionValid(slot)||!xessWasEnabled_||identity.epoch!=lastXessIdentity_.epoch||identity.settingsRevision!=lastXessIdentity_.settingsRevision||graph.motionPreviousSource(slot)!=lastXessIdentity_.sourceFrameId;
        // Present-to-present time includes provider waits. Feeding it back as
        // render time can lengthen the next burst. Intel documents zero as the
        // supported value when an independent frame-time estimate is absent.
        constexpr float elapsed=0.0f;
        auto* motion=graph.presentMotion(slot);
        auto* depth=graph.presentDepth();
        if(enabled){
            // Stable full-buffer interpolation avoids provider letterbox
            // reallocations. Guidance must follow the same displayed domain.
            auto* mapped=presentMotion_[slot].Get();
            pipeline::StateTracker states;
            states.transition(list,motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            states.transition(list,mapped,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const auto previous=reset?view:previousXessView_;
            const float mapping[16]={float(graph.workWidth()),float(graph.workHeight()),float(rc.right),float(rc.bottom),
                view.zoom,view.centerX,view.centerY,reset?1.0f:0.0f,
                previous.zoom,previous.centerX,previous.centerY,0,
                float(reset?rc.right:previousXessWidth_),float(reset?rc.bottom:previousXessHeight_),0,0};
            const auto heap=presentMotionPass_.heap->GetGPUDescriptorHandleForHeapStart().ptr;
            presentMotionPass_.bind(list,mapping,heap+size_t(slot*2)*presentMotionPass_.increment,heap+size_t(slot*2+1)*presentMotionPass_.increment);
            list->Dispatch((graph.workWidth()+7)/8,(graph.workHeight()+7)/8,1);
            states.transition(list,mapped,D3D12_RESOURCE_STATE_COMMON);
            states.transition(list,motion,D3D12_RESOURCE_STATE_COMMON);
            // No graph work or resource reuse may occur between tagging and
            // Present. XeSS transitions from/to COMMON on this same queue.
            // This timer covers application commands, not provider execution.
            gpuTimer_.mark(list,diagnostics::GpuStage::FgBatch);
            const RECT fullBuffer{0,0,LONG(sink_.bufferWidth()),LONG(sink_.bufferHeight())};
            if(!xess->tag(list,bb,mapped,depth,fullBuffer,true,reset,elapsed)){xessFailed_=true;return false;}
            gpuTimer_.mark(list,diagnostics::GpuStage::FgBatch,true);
        }else if(!xess->tag(list,bb,motion,depth,fgRect,false,reset,elapsed)){xessFailed_=true;return false;}
        lastXessFrame_=now;lastXessIdentity_=identity;xessWasEnabled_=enabled;
        previousXessView_=view;previousXessWidth_=rc.right;previousXessHeight_=rc.bottom;
    }
    if(auto* fsr=sink_.fsr()){
        const bool enabled=regionVisible&&!generated&&!comparison&&graph.presentMotionValid(slot)&&identity.sourceFrameId!=lastFsrIdentity_.sourceFrameId;
        const bool reset=!fsrWasEnabled_||identity.epoch!=lastFsrIdentity_.epoch||identity.settingsRevision!=lastFsrIdentity_.settingsRevision||graph.motionPreviousSource(slot)!=lastFsrIdentity_.sourceFrameId;
        const float elapsed=lastFsrFrame_==std::chrono::steady_clock::time_point{}?0.0f:float(std::chrono::duration<double,std::milli>(now-lastFsrFrame_).count());
        // The AMD presenter degrades inside the provider (generation off, plain
        // presentation continues) instead of failing the frame: tearing the
        // swapchain down would only cost the session a restart.
        if(enabled)gpuTimer_.mark(list,diagnostics::GpuStage::FgBatch);
        if(!fsr->tag(list,bb,graph.presentMotion(slot),graph.presentDepth(),fgRect,enabled,reset,elapsed))fsrFailed_=true;
        if(enabled)gpuTimer_.mark(list,diagnostics::GpuStage::FgBatch,true);
        if(sink_.fsr()->failed())fsrFailed_=true;
        lastFsrFrame_=now;lastFsrIdentity_=identity;fsrWasEnabled_=enabled;
    }
    gpuTimer_.mark(list,diagnostics::GpuStage::Blit,true);gpuTimer_.resolve(list);
    if(!ring.submitAndSignal(commandSlot))return false;gpuTimer_.submitted(ring.lastSignaledValue());lastBuffer_=backBufferIndex;hasPresented_=true;
    if(presentationQueue_)graph.presentationSubmitted(slot,fence,ring.lastSignaledValue());
    const auto dxgiStart=std::chrono::steady_clock::now();
    reflex_.mark(reflexFrame_,3);reflex_.mark(reflexFrame_,4);
    const bool presented=sink_.present(st);
    reflex_.mark(reflexFrame_,5);reflexFrame_=0;
    const auto presentEnd=std::chrono::steady_clock::now();
    const auto ms=[](auto d){return std::chrono::duration<double,std::milli>(d).count();};
    const bool slow=ms(presentEnd-presentStart)>=80.0;
    if(slow||presentEnd>=nextCostLog_){
        nextCostLog_=presentEnd+std::chrono::seconds(1);
        const auto& timing=sink_.lastPresentTiming();
        veyra::log::info("present-cost",std::format("generated={} totalMs={:.3f} recordSubmitMs={:.3f} slotWaitMs={:.3f} dxgiMs={:.3f} slow={} source={} epoch={} backend={} resized={} resizeMs={:.3f} beginMs={:.3f} commandsMs={:.3f} beforeMs={:.3f} presentCallMs={:.3f} bufferMs={:.3f} afterMs={:.3f} hr=0x{:X}",generated,
            std::chrono::duration<double,std::milli>(presentEnd-presentStart).count(),
            std::chrono::duration<double,std::milli>(dxgiStart-presentStart).count(),ring.cpuWaitMilliseconds()-slotWaitStart,
            ms(presentEnd-dxgiStart),slow,identity.sourceFrameId,identity.epoch,sink_.xess()?"XeSS":sink_.fsr()?"FSR":"DXGI",resized,
            ms(resizeEnd-presentStart),ms(beginEnd-resizeEnd),ms(dxgiStart-beginEnd),timing.beforeMs,timing.callMs,timing.bufferMs,timing.afterMs,unsigned(timing.result)));
    }
    xessFailed_=sink_.xessFailed();fsrFailed_=fsrFailed_||sink_.fsrFailed();return presented;
}
bool VideoPresenter::readPresentedFrameForTest(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,sink::RgbaImage& image){
    if(presentationQueue_&&!presentationRing_.drainQueue())return false;
    if(!hasPresented_||!sink_.swapChain()||!ring.drainQueue())return false;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    return SUCCEEDED(sink_.swapChain()->GetBuffer(lastBuffer_,IID_PPV_ARGS(&buffer)))&&sink::readRgba8(ctx,ring,buffer.Get(),image);
}
void VideoPresenter::close(){
    xessInputId_=0;xessWork_={};xessWorkPosition_=0;
    reflex_.close();reflexFrame_=0;
    if(presentationRing_.initialized())presentationRing_.drainQueue();
    hasPresented_=false;gpuTimer_.close();rtvs_.Reset();pass_={};sink_.shutdown();
    presentMotionPass_={};presentMotion_={};previousXessWidth_=previousXessHeight_=0;
    presentationRing_.shutdown();presentationFence_.Reset();presentationQueue_.Reset();
    if(presentationEvent_){CloseHandle(presentationEvent_);presentationEvent_=nullptr;}
}
PresentationSettings VideoPresenter::configurePresentation(gfx::D3D12DeviceContext& ctx,PresentationSettings requested,bool fg,std::wstring& status){
    const bool reflexDisabled=reflex_.disable();reflexFrame_=0;
    auto effective=requested;
    if(!reflexDisabled){effective.enabled=false;sink_.configurePacing(false,false);status=L"Reflex 驱动状态撤销失败；应用等待已停用，请关闭视频后重试";return effective;}
    if(!requested.enabled){const bool restored=sink_.configurePacing(false,false);status=restored?L"帧同步已关闭":L"应用等待已关闭，但显示队列恢复失败；请关闭视频后重试";return effective;}
    if(xessActive()||fsrActive()){effective.enabled=false;sink_.configurePacing(false,false);status=L"当前由补帧提供方调度，帧同步选项暂不生效";return effective;}
    if(!sink_.configurePacing(true,requested.display!=DisplaySync::Tearing)){effective.enabled=false;status=L"显示队列控制不可用，已回退原呈现方式";return effective;}
    if(requested.mode==PacingMode::Reflex){
        // Generated outputs need separately validated out-of-band markers.
        if(fg||!reflex_.enable(ctx.device())){effective.mode=PacingMode::LowQueue;status=fg?L"补帧运行：Reflex 暂用低排队（保留补帧倍率）":L"Reflex 初始化失败，已回退低排队";return effective;}
    }
    status=effective.mode==PacingMode::Reflex?L"NVIDIA Reflex · 实验":effective.mode==PacingMode::Even?L"均匀呈现":L"低排队";
    if(requested.display==DisplaySync::Automatic)status+=L" · 自动：垂直同步（VRR 状态未知）";
    else if(requested.display==DisplaySync::Vsync)status+=L" · 垂直同步";
    else status+=L" · 允许撕裂";
    return effective;
}
Microsoft::WRL::ComPtr<ID3D12Resource> VideoPresenter::presentedResourceForTest() {
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    if(presentationQueue_&&!presentationRing_.drainQueue())return buffer;
    if(hasPresented_&&sink_.swapChain())sink_.swapChain()->GetBuffer(lastBuffer_,IID_PPV_ARGS(&buffer));
    return buffer;
}
}
