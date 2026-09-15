#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/pipeline/HdrToneMap.h"
// EnhanceGraph implementation - the real GPU chain migrated from
// tools/player_probe main.cpp (R3.2). Ordering constraints preserved from the
// injected-layer era evidence: committed resources and NGX/NVOF objects are
// created BEFORE descriptor views; FG warm-up evaluate precedes views; static
// views are created last. Per-frame execution uses the shared command slot
// ring (NR evaluates on a fresh list - snippet constraint).
#include "veyra/pipeline/EnhanceGraph.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <thread>

#include "veyra/Log.h"
#include "veyra/Result.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/ngx/DlssFgBackend.h"
#include "veyra/ngx/DlssNrParameters.h"
#include "veyra/ngx/DlssNrRuntimeAdapter.h"
#include "veyra/ngx/DlssSrBackend.h"
#include "veyra/ngx/VideoSrBackend.h"
#include "veyra/ngx/NgxCoreHost.h"
#include "veyra/ngx/NgxParameters.h"
#include "veyra/ngx/NvOfSession.h"
#include "veyra/guidance/AmdOpticalFlow.h"
#include "veyra/guidance/GpuDisOpticalFlow.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext_d3d12va.h>
#include <libswscale/swscale.h>
}

namespace veyra::pipeline {
using diagnostics::GpuStage;

namespace {

constexpr int64_t usPerSecond = 1000000;
float uintBits(uint32_t v) { return std::bit_cast<float>(v); }

} // namespace

EnhanceGraph::EnhanceGraph(gfx::D3D12DeviceContext& context, gfx::CommandSlotRing& ring)
    : context_(context)
    , ring_(ring)
{
}

EnhanceGraph::~EnhanceGraph()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// initialize: exact ordering of the proven probe sequence.
// ---------------------------------------------------------------------------
bool EnhanceGraph::initialize(const EnhanceGraphDesc& desc)
{
    failedBackend_=engine::FailedBackend::Infrastructure;
    if(!desc.protection.validate().empty())return false;
    if(desc.srMotionProbe){
        const auto d=desc.srMotionProbe->GetDesc();ComPtr<ID3D12Device> device;
        if(!desc.enableSr||desc.noFeatures||desc.noNgx||d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Width!=desc.workWidth||d.Height!=desc.workHeight||d.Format!=DXGI_FORMAT_R16G16_FLOAT||d.DepthOrArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)||FAILED(desc.srMotionProbe->GetDevice(IID_PPV_ARGS(&device)))||device.Get()!=context_.device()){
            veyra::log::error("graph","invalid diagnostic SR motion resource/feature/device contract");return false;
        }
        veyra::log::info("graph","diagnostic SR motion override active; no product entry point enables this");
    }
    desc_ = desc;tracker_={};prevValid_=false;fgHistorySkipped_=false;cadence_.reset();scene_.reset();previousLuma_.clear();
    if(desc.videoSrQuality>4)return false;
    srcW_ = desc.sourceWidth;
    srcH_ = desc.sourceHeight;
    workW_ = desc.workWidth;
    workH_ = desc.workHeight;
    nrW_=desc.nrWidth?desc.nrWidth:workW_; nrH_=desc.nrHeight?desc.nrHeight:workH_;
    if((desc.flowWidth==0)!=(desc.flowHeight==0)){veyra::log::error("resolution","partial flow extent is invalid");return false;}
    nvofW_ = desc.flowWidth?desc.flowWidth:srcW_;
    nvofH_ = desc.flowHeight?desc.flowHeight:srcH_;
    if(!Extent{srcW_,srcH_}.valid()||!Extent{workW_,workH_}.valid()){
        veyra::log::error("resolution","source/output exceeds D3D12 texture dimensions or is empty");return false;
    }
    if(!Extent{nrW_,nrH_}.valid()||nrW_>workW_||nrH_>workH_){veyra::log::error("resolution","invalid NR extent");return false;}
    if(!Extent{nvofW_,nvofH_}.valid()||nvofW_>srcW_||nvofH_>srcH_){veyra::log::error("resolution","invalid source-space flow extent");return false;}
    diagnostics::DiagnosticEvent initDiagnostic;initDiagnostic.stage="initialize";initDiagnostic.identity={epoch_+1,desc.settingsRevision,0};initDiagnostic.resolution.source={srcW_,srcH_};initDiagnostic.resolution.base=initDiagnostic.resolution.fg=initDiagnostic.resolution.output={workW_,workH_};initDiagnostic.resolution.nr={nrW_,nrH_};initDiagnostic.resolution.flow={nvofW_,nvofH_};initDiagnostic.flowApplied=std::to_string(unsigned(desc.flowQuality));initDiagnostic.runtimeHash="unverified-user-replaceable";Logger::diagnosticContext(initDiagnostic);
    veyra::log::info("resolution",std::format("source={}x{} base={}x{} nr={}x{} flow={}x{} fg={}x{} output={}x{}",srcW_,srcH_,workW_,workH_,nrW_,nrH_,nvofW_,nvofH_,workW_,workH_,workW_,workH_));
    veyra::log::info("pipeline-order",desc.nrBeforeSr?"NR -> residual(source) -> SR -> FG (experimental preview)":"SR -> NR -> residual -> FG");
    if(desc.hdrOutput&&!desc.hdrInput){veyra::log::error("hdr","HDR output requires an explicit HDR input contract");return false;}
    srEnabled_ = desc.enableSr && (srcW_ != workW_ || srcH_ != workH_);
    nrEnabled_ = desc.enableNr && !desc.noFeatures && !desc.noNgx;
    fgEnabled_ = desc.enableFg && !desc.noNgx && !desc.stillImage && desc.frameGenerationBackend!=engine::FrameGenerationBackend::XeSS;
    nvofStandalone_ = desc.enableNvofStandalone && !desc.noFeatures;
    if(desc.opticalFlowBackend==engine::OpticalFlowBackend::AmdFidelityFx&&desc.amdFlowHalfResolution){nvofW_=std::max(1u,nvofW_/2);nvofH_=std::max(1u,nvofH_/2);}
    uint64_t budget=0,usage=0;
    const uint64_t estimate=uint64_t(workW_)*workH_*(fgEnabled_?100:76)+uint64_t(nrW_)*nrH_*32+uint64_t(srcW_)*srcH_*32;
    if(context_.videoMemoryInfo(budget,usage)){
        veyra::log::info("memory",std::format("graph lower-bound={}MiB budget={}MiB usage={}MiB; SDK allocations additional",estimate>>20,budget>>20,usage>>20));
        if(usage>=budget||estimate>budget-usage){veyra::log::error("memory","insufficient adapter budget for requested graph textures");return false;}
    }
    Status st = Status::Ok;

    lumaPitch_ = (static_cast<size_t>(srcW_)*(desc_.wideYuvInput()?2:1) + 255) & ~size_t(255);
    chromaPitch_ = lumaPitch_;
    lumaSize_ = lumaPitch_ * srcH_;
    chromaSize_ = chromaPitch_ * ((srcH_ + 1) / 2);
    dPitch_ = (static_cast<size_t>(workW_) * 4 + 255) & ~size_t(255);
    const size_t dSize = dPitch_ * workH_;
    nv12Buf_.resize(lumaSize_ + chromaSize_);

    if(!gpuTimer_.initialize(context_.device(),context_.directQueue()))veyra::log::warn("gpu-timestamp","GPU timing unavailable");
    if (!createResources()) return false;
    if (!initZeroAndDepthTextures()) return false;
    if (!initNvof()) { failedBackend_=engine::FailedBackend::OpticalFlow; return false; }
    if(nrEnabled_&&GetEnvironmentVariableW(L"VEYRA_TEST_NR_INIT_FAILURE",nullptr,0)){
        failedBackend_=engine::FailedBackend::Nr;
        veyra::log::error("backend-recovery-test","test-only NR initialization rejection before SDK call; not a hardware failure");return false;
    }
    if (!initNgxFeatures()) return false;
    failedBackend_=engine::FailedBackend::Infrastructure;
    if (!createComputePasses()) return false;

    initialized_ = true;
    failedBackend_=engine::FailedBackend::None;
    veyra::log::info("graph", std::format("initialized src={}x{} work={}x{} sr={} nr={} fg={} nvof={}",
        srcW_, srcH_, workW_, workH_, srEnabled_ ? 1 : 0, nrEnabled_ ? 1 : 0,
        fgEnabled_ ? 1 : 0, (nvof_ && nvof_->initialized()) ? 1 : 0));
    return true;
}

bool EnhanceGraph::createResources()
{
    fgDisableInit_=makeUploadBuffer(context_.device(),4);
    if(!fgDisableInit_)return false;
    void* initial=nullptr;if(FAILED(fgDisableInit_->Map(0,nullptr,&initial)))return false;
    *static_cast<uint32_t*>(initial)=1;fgDisableInit_->Unmap(0,nullptr);
    for(unsigned i=0;i<kGeneratedPoolSlots;++i){
        D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=4;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        HRESULT hr=context_.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&fgDisable_[i]));
        if(FAILED(hr)){veyra::log::error("fg-status",std::format("allocate UAV hr=0x{:X}",unsigned(hr)));return false;}
        hp.Type=D3D12_HEAP_TYPE_READBACK;bd.Flags=D3D12_RESOURCE_FLAG_NONE;
        hr=context_.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&fgDisableReadback_[i]));
        if(FAILED(hr)){veyra::log::error("fg-status",std::format("allocate readback hr=0x{:X}",unsigned(hr)));return false;}
    }
    // NV12 CPU upload keeps UPLOAD-heap BUFFERS (upload-heap textures are
    // size-limited) with persistent mapping, but the GPU ingestion is now a
    // PLACED_FOOTPRINT CopyTextureRegion instead of the retired RAW-buffer
    // SRV dispatch - the RAW SRV was the one descriptor kind that tripped the
    // injected layer (device removal / blocked NVOF; r33b evidence 2026-09-06).
    upLuma_[0] = makeUploadBuffer(context_.device(), lumaSize_);
    upLuma_[1] = makeUploadBuffer(context_.device(), lumaSize_);
    upChroma_[0] = makeUploadBuffer(context_.device(), chromaSize_);
    upChroma_[1] = makeUploadBuffer(context_.device(), chromaSize_);
    upDepth_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    upZeroDepth_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    upZeroMotion_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    lumaTex_ = makeTexture(context_.device(), srcW_, srcH_, desc_.wideYuvInput()?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM, true);
    chromaTex_ = makeTexture(context_.device(), (srcW_+1) / 2, (srcH_+1) / 2, desc_.wideYuvInput()?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM, true);
    if(desc_.rgbInput||desc_.yuy2Input){
        if(desc_.yuy2Input&&(srcW_%2||desc_.rgbInput))return false;
        const unsigned packedWidth=desc_.yuy2Input?srcW_/2:srcW_;
        rgbPitch_=(size_t(packedWidth)*4+255)&~size_t(255);
        rgbTex_=makeTexture(context_.device(),packedWidth,srcH_,DXGI_FORMAT_R8G8B8A8_UNORM,false);
        if(!rgbTex_)return false;
        for(unsigned i=0;i<2;++i){upRgb_[i]=makeUploadBuffer(context_.device(),rgbPitch_*srcH_);
            if(!upRgb_[i]||FAILED(upRgb_[i]->Map(0,nullptr,reinterpret_cast<void**>(&mappedRgb_[i]))))return false;
        }
    }
    srcRgba_ = makeTexture(context_.device(), srcW_, srcH_, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    if(srEnabled_&&desc_.videoSrQuality){videoSrInput_=makeTexture(context_.device(),srcW_,srcH_,DXGI_FORMAT_R8G8B8A8_UNORM,true);videoSrOutput_=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R8G8B8A8_UNORM,true);if(!videoSrInput_||!videoSrOutput_)return false;}
    const bool directBase=!srEnabled_&&srcW_==workW_&&srcH_==workH_;
    workRgba_=directBase?srcRgba_:makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    for(unsigned i=0;i<2;++i){sourceReferences_[i]=makeTexture(context_.device(),srcW_,srcH_,DXGI_FORMAT_R16G16B16A16_FLOAT,false);baseReferences_[i]=directBase?sourceReferences_[i]:makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16B16A16_FLOAT,false);if(!sourceReferences_[i]||!baseReferences_[i])return false;}
    nrInput_=(desc_.nrBeforeSr&&nrW_==srcW_&&nrH_==srcH_)?srcRgba_:(nrW_==workW_&&nrH_==workH_)?workRgba_:makeTexture(context_.device(),nrW_,nrH_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    residualRgba_=makeTexture(context_.device(),desc_.nrBeforeSr?srcW_:workW_,desc_.nrBeforeSr?srcH_:workH_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    nrFlow_=makeTexture(context_.device(),nrW_,nrH_,DXGI_FORMAT_R16G16_FLOAT,true);
    baseFlow_=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16_FLOAT,true);
    if(xessEnabled())for(auto& motion:presentMotion_){motion=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16_FLOAT,false);if(!motion)return false;}
    if(!nrInput_||!residualRgba_||!nrFlow_||!baseFlow_)return false;
    proxyTex_ = makeTexture(context_.device(), nrW_, nrH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    neuralTex_ = makeTexture(context_.device(), nrW_, nrH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    finalRgba_ = makeTexture(context_.device(), nrW_, nrH_, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    videoFrame_[0] = makeTexture(context_.device(), workW_, workH_, outputFormat(), true);
    videoFrame_[1] = makeTexture(context_.device(), workW_, workH_, outputFormat(), true);
    confTex_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_R8_UNORM, true);
    flowTex_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_R16G16_FLOAT, true);
    depthTex_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R32_FLOAT, false);
    // Valid placeholder descriptors keep disabled FG inexpensive. Enabling it
    // is a graph rebuild, so no in-flight descriptor is resized in place.
    for(auto& frame:genFrame_){frame=makeTexture(context_.device(),fgEnabled_?workW_:1,fgEnabled_?workH_:1,outputFormat(),true);if(!frame)return false;}
    nrZeroMotion_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R16G16_FLOAT, false);
    nrZeroDepth_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R32_FLOAT, false);
    rawW_ = (nvofW_ + nvofGrid_ - 1) / nvofGrid_;
    rawH_ = (nvofH_ + nvofGrid_ - 1) / nvofGrid_;
    nvofRawTex_ = makeTexture(context_.device(), rawW_, rawH_, DXGI_FORMAT_R16G16_SINT, false);
    nvofCostTex_ = makeTexture(context_.device(), rawW_, rawH_, DXGI_FORMAT_R8_UINT, false);
    nvofInA_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_B8G8R8A8_UNORM, true);
    nvofInB_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_B8G8R8A8_UNORM, true);

    if (!upLuma_[0] || !upLuma_[1] || !upChroma_[0] || !upChroma_[1] ||
        !upDepth_ || !upZeroDepth_ || !upZeroMotion_ ||
        !lumaTex_ || !chromaTex_ || !srcRgba_ || !workRgba_ || !proxyTex_ ||
        !neuralTex_ || !finalRgba_ || !videoFrame_[0] || !videoFrame_[1] ||
        !flowTex_ || !depthTex_ || !genFrame_[0] || !genFrame_[1] ||
        !nrZeroMotion_ || !nrZeroDepth_ || !nvofRawTex_ || !nvofCostTex_ ||
        !nvofInA_ || !nvofInB_ || !confTex_) {
        veyra::log::error("graph", "resource allocation failed");
        return false;
    }

    // Persistent mapping of the NV12 upload ring (before any views).
    bool uploadsMapped = true;
    for (int i = 0; i < 2; ++i) {
        if (FAILED(upLuma_[i]->Map(0, nullptr, reinterpret_cast<void**>(&mappedLuma_[i]))) ||
            FAILED(upChroma_[i]->Map(0, nullptr, reinterpret_cast<void**>(&mappedChroma_[i])))) {
            uploadsMapped = false;
        }
    }
    if (!uploadsMapped) {
        veyra::log::error("graph", "NV12 upload persistent map failed");
        return false;
    }
    veyra::log::info("graph", "NV12 upload ring persistently mapped (2 buffer sets)");
    return true;
}

bool EnhanceGraph::initZeroAndDepthTextures()
{
    // Depth constants uploaded once (copies are safe before views exist).
    uint8_t* d = nullptr; uint8_t* zd = nullptr; uint8_t* zm = nullptr;
    upDepth_->Map(0, nullptr, reinterpret_cast<void**>(&d));
    upZeroDepth_->Map(0, nullptr, reinterpret_cast<void**>(&zd));
    upZeroMotion_->Map(0, nullptr, reinterpret_cast<void**>(&zm));
    for (uint32_t y = 0; y < workH_; ++y) {
        float* dRow = reinterpret_cast<float*>(d + y * dPitch_);
        float* zdRow = reinterpret_cast<float*>(zd + y * dPitch_);
        uint16_t* zmRow = reinterpret_cast<uint16_t*>(zm + y * dPitch_);
        for (uint32_t x = 0; x < workW_; ++x) {
            dRow[x] = 0.9f;  // explicit constant far depth (video content)
            zdRow[x] = 0.5f; // NR zero-depth explicit fallback
            zmRow[x * 2] = 0; zmRow[x * 2 + 1] = 0;
        }
    }
    upDepth_->Unmap(0, nullptr);
    upZeroDepth_->Unmap(0, nullptr);
    upZeroMotion_->Unmap(0, nullptr);

    Status st = Status::Ok;
    ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
    if (list == nullptr) return false;
    auto uploadTex = [&](ID3D12Resource* tex, const ComPtr<ID3D12Resource>& up, DXGI_FORMAT fmt) {
        D3D12_RESOURCE_BARRIER b{};
        b.Transition.pResource = tex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = tex;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = up.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = fmt;
        src.PlacedFootprint.Footprint.Width = workW_;
        src.PlacedFootprint.Footprint.Height = workH_;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(dPitch_);
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER back{};
        back.Transition.pResource = tex;
        back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        back.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        back.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &back);
    };
    uploadTex(depthTex_.Get(), upDepth_, DXGI_FORMAT_R32_FLOAT);
    uploadTex(nrZeroDepth_.Get(), upZeroDepth_, DXGI_FORMAT_R32_FLOAT);
    uploadTex(nrZeroMotion_.Get(), upZeroMotion_, DXGI_FORMAT_R16G16_FLOAT);
    list->Close();
    ID3D12CommandList* lists[] = { list };
    context_.directQueue()->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> initFence;
    context_.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&initFence));
    context_.directQueue()->Signal(initFence.Get(), 1);
    if (nvofOutEvent_ == nullptr) {
        nvofOutEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    initFence->SetEventOnCompletion(1, nvofOutEvent_);
    WaitForSingleObject(nvofOutEvent_, 5000);
    veyra::log::info("graph", "depth/zero guidance textures initialized");
    return true;
}

bool EnhanceGraph::initNvof()
{
    if(desc_.stillImage){mvecSource_="single-image (no temporal motion)";return true;}
    if (desc_.noFeatures || !(nvofStandalone_ || xessEnabled() || (fgEnabled_&&desc_.frameGenerationBackend==engine::FrameGenerationBackend::Dlss) || (srEnabled_&&!desc_.videoSrQuality))) {
        veyra::log::info("graph", "VEYRA_NO_FEATURES: NVOF session skipped");
        return true;
    }
    if(desc_.opticalFlowBackend==engine::OpticalFlowBackend::GpuDis){
        gpuDis_=std::make_unique<guidance::GpuDisOpticalFlow>();
        if(!gpuDis_->initialize(context_.device(),nvofW_,nvofH_))return false;
        mvecSource_="gpu-dis FAST + bidirectional/photometric confidence";
        return true;
    }
    if(desc_.opticalFlowBackend==engine::OpticalFlowBackend::AmdFidelityFx){
        amdOf_=std::make_unique<guidance::AmdOpticalFlow>();
        if(!amdOf_->initialize(context_.device(),nvofW_,nvofH_))return false;
        nvofRawTex_=amdOf_->vectors();rawW_=amdOf_->width();rawH_=amdOf_->height();selectedGrid_=8;
        mvecSource_="amd-fidelityfx + photometric confidence";
        return true;
    }
    nvof_ = std::make_unique<ngx::NvOfSession>();
    if (nvofOutFence_ == nullptr) {
        if (FAILED(context_.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&nvofOutFence_)))) {
            veyra::log::error("graph", "NVOF out fence creation failed");
            return false;
        }
    }
    if (nvofOutEvent_ == nullptr) {
        nvofOutEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    ngx::NvOfSession::Desc nd{};
    nd.width = nvofW_; nd.height = nvofH_;
    nd.inFence = context_.fence();
    nd.outFence = nvofOutFence_.Get();
    nd.gridSize = 4;nd.quality=uint32_t(desc_.flowQuality);
    // s6 contract: B8G8R8A8 inputs, raw R16G16_SINT + R8_UINT cost at grid
    // extent, passed explicitly; allocation failure = fail closed.
    Status st = Status::Ok;
    if (!nvof_->initialize(context_.device(), nvofInA_.Get(), nvofInB_.Get(),
            nvofRawTex_.Get(), nvofCostTex_.Get(), nd, st)) {
        veyra::log::error("graph", "NVOF init failed");
        return false;
    }
    selectedGrid_ = nvof_->caps().selectedGrid;
    return true;
}

bool EnhanceGraph::initNgxFeatures()
{
    failedBackend_=engine::FailedBackend::NgxCore;
    if (desc_.noNgx || desc_.noFeatures || (!nrEnabled_&&!srEnabled_&&!fgEnabled_)) {
        veyra::log::info("graph", "NGX core/features skipped by disabled-feature configuration");
        return true;
    }
    // Read the local NGX identity (same loader contract as the probes).
    std::ifstream ids(std::wstring(desc_.runtimeAbsPath) + L"\\..\\config\\ngx-local.json",
        std::ios::binary);
    std::string idText((std::istreambuf_iterator<char>(ids)), std::istreambuf_iterator<char>());
    std::string projectId, engineVersion;
    {
        auto scan = [&idText](const char* key) {
            const std::string needle = std::string("\"") + key + "\"";
            size_t p = idText.find(needle);
            if (p == std::string::npos) return std::string();
            p = idText.find('"', idText.find(':', p + needle.size()));
            if (p == std::string::npos) return std::string();
            return idText.substr(p + 1, idText.find('"', p + 1) - p - 1);
        };
        projectId = scan("ngxProjectId");
        engineVersion = scan("engineVersion");
    }
    if (projectId.empty()) {
        veyra::log::error("graph", "ngx-local.json missing");
        return false;
    }

    coreHost_ = std::make_unique<ngx::NgxCoreHost>();
    Status st = Status::Ok;
    if (!coreHost_->initialize(context_.device(), desc_.runtimeAbsPath.c_str(),
            projectId.c_str(), engineVersion.c_str(), st)) {
        return false;
    }

    fgCapsAvailable_ = false;
    fgMultiFrameMax_ = 0;
    if (fgEnabled_ && desc_.frameGenerationBackend==engine::FrameGenerationBackend::Dlss) {
    failedBackend_=engine::FailedBackend::Fg;
    fgBackend_ = std::make_unique<ngx::DlssFgBackend>();
    ngx::DlssFgBackend::Capability fgCaps{};
    const bool fgAvailable = fgBackend_->queryCapability(*coreHost_, fgCaps, st);
    if (!fgAvailable) {
        veyra::log::error("graph", "FG unavailable; fail closed");
        return false;
    }
        // Publish the capability before validating the request so a rejected
        // multiplier still teaches the caller what this GPU supports.
        fgCapsAvailable_ = fgCaps.available;
        fgMultiFrameMax_ = fgCaps.multiFrameCountMax;
        // Test-only capability override: lets the Ada (40-series) ceiling and the
        // capability-driven UI/recovery paths be exercised on a 50-series host
        // without touching the runtime. Never set by the product UI.
        {
            wchar_t overrideText[16]{};
            if(GetEnvironmentVariableW(L"VEYRA_TEST_FG_MULTIFRAME_MAX",overrideText,16)>0){
                const int forced=_wtoi(overrideText);
                if(forced>=0&&forced<=5){
                    fgMultiFrameMax_=forced;fgCaps.multiFrameCountMax=uint32_t(forced);
                    veyra::log::warn("capability",std::format("test-only FG MultiFrameCountMax override={}",forced));
                }
            }
        }
        veyra::log::info("graph", std::format("FG capability available={} multiFrameMax={}",
            fgCaps.available, fgCaps.multiFrameCountMax));
        // Test-only research path for the Ada (40-series) MFG unlock: with
        // VEYRA_TEST_FG_FORCE_MULTIPLIER=1 the reported capability no longer
        // rejects the request, so a 40-series host can show whether the runtime
        // generates real frames (or repeats/black) when the gate is bypassed at
        // the caller. Never reachable from the product UI.
        const bool forceAboveCapability=GetEnvironmentVariableW(L"VEYRA_TEST_FG_FORCE_MULTIPLIER",nullptr,0)>0;
        const bool aboveCapability=desc_.enableFg&&fgCaps.multiFrameCountMax<desc_.fgMultiplier-1;
        if(aboveCapability&&forceAboveCapability){
            veyra::log::warn("capability",std::format("test-only FG multiplier forced above capability request={} maxGeneratedFrames={}",desc_.fgMultiplier,fgCaps.multiFrameCountMax));
        }
        if(desc_.enableFg&&(desc_.fgMultiplier<2||desc_.fgMultiplier>6||(aboveCapability&&!forceAboveCapability))){veyra::log::error("graph",std::format("requested MFG multiplier unsupported request={} maxGeneratedFrames={}",desc_.fgMultiplier,fgCaps.multiFrameCountMax));return false;}
    }

    if(nrEnabled_){
    failedBackend_=engine::FailedBackend::Nr;
    if(desc_.nrRuntime!=engine::NrRuntime::Original&&desc_.nrRuntime!=engine::NrRuntime::Community&&desc_.nrRuntime!=engine::NrRuntime::Ampere)return false;
    auto nrDirectory=std::filesystem::path(desc_.runtimeAbsPath);if(desc_.nrRuntime==engine::NrRuntime::Community)nrDirectory/=L"nr-community";
    if(desc_.nrRuntime==engine::NrRuntime::Ampere)nrDirectory/=L"nr-ampere";
    veyra::log::info("nr-runtime",std::format("selected={} path={}",engine::nrRuntimeName(desc_.nrRuntime),nrDirectory.string()));
    nrAdapter_ = std::make_unique<ngx::DlssNrRuntimeAdapter>();
    if (!nrAdapter_->load(nrDirectory.wstring(), st) ||
        !nrAdapter_->installCallerCompatibility(st) ||
        (desc_.nrRuntime==engine::NrRuntime::Ampere && !nrAdapter_->installAmpereCompatibility(context_.device(), st)) ||
        !nrAdapter_->snippetInitExt(context_.device(), nrDirectory.c_str(), nrResult_, nrSeh_) ||
        nrResult_ != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
        veyra::log::error("graph", std::format("NR snippet init failed 0x{:X}", nrResult_));
        return false;
    }
    }
    failedBackend_=engine::FailedBackend::NgxCore;
    ngxParams_ = coreHost_->allocateParameters(st);
    if (ngxParams_ == nullptr) return false;

    srBackend_ = std::make_unique<ngx::DlssSrBackend>();

    // NR feature create (8.5 create parameter contract).
    if(nrEnabled_) {
        namespace p = ngx::dlssnr;
        ngx::ParameterBlock pb(ngxParams_);
        pb.setU32(p::kWidth, nrW_); pb.setU32(p::kHeight, nrH_);
        pb.setU32(p::kInputWidth, nrW_); pb.setU32(p::kInputHeight, nrH_);
        pb.setU32(p::kOutputWidth, nrW_); pb.setU32(p::kOutputHeight, nrH_);
        pb.setU32(p::kOutputDotWidth, nrW_); pb.setU32(p::kOutputDotHeight, nrH_);
        pb.setU32(p::kUpscaling, 0);
        pb.setF32(p::kScale, 1.0f); pb.setF32(p::kScalingRatio, 1.0f);
        pb.setVoid(p::kComputeScalingRatioCallback,
            reinterpret_cast<void*>(&ngx::DlssNrRuntimeAdapter::scalingRatioCallback));
        pb.setI32(p::kHintRenderPreset, 0);
        pb.setU32(p::kStdWidth, nrW_); pb.setU32(p::kStdHeight, nrH_);
        pb.setI32(p::kPerfQualityValue, 1);
        pb.setU32(p::kCreationNodeMask, 1); pb.setU32(p::kVisibilityNodeMask, 1);
        ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
        if (list == nullptr) { failedBackend_=engine::FailedBackend::Infrastructure; return false; }
        failedBackend_=engine::FailedBackend::Nr;
        if (!nrAdapter_->snippetCreateFeature(list, ngxParams_, &nrHandle_, nrResult_, nrSeh_) ||
            nrResult_ != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
            veyra::log::error("graph", std::format("NR create failed 0x{:X}", nrResult_));
            return false;
        }
        failedBackend_=engine::FailedBackend::Infrastructure;
        if(!ring_.submitAndSignal(0)||!ring_.waitIdle())return false;
    }

    if (srEnabled_ && !desc_.noFeatures) {
        ngx::DlssSrBackend::CreateDesc sd{};
        sd.inputWidth = srcW_; sd.inputHeight = srcH_;
        sd.outputWidth = workW_; sd.outputHeight = workH_;
        sd.perfQuality = 1; sd.enableOutputSubrects = false;
        ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
        if (list == nullptr) { failedBackend_=engine::FailedBackend::Infrastructure; return false; }
        failedBackend_=engine::FailedBackend::Sr;
        if(desc_.videoSrQuality){videoSrBackend_=std::make_unique<ngx::VideoSrBackend>();if(!videoSrBackend_->create(*coreHost_,list))return false;veyra::log::info("video-sr",std::format("selected quality={} input={}x{} output={}x{}",desc_.videoSrQuality,srcW_,srcH_,workW_,workH_));}
        else if (!srBackend_->create(*coreHost_, list, ngxParams_, sd, st) || !srBackend_->created()) {
            veyra::log::error("graph", "SR create failed");
            return false;
        }
        failedBackend_=engine::FailedBackend::Infrastructure;
        if(!ring_.submitAndSignal(0)||!ring_.waitIdle())return false;
    }

    // Still images have no temporal pair and must not depend on FG support.
    // FG create + warm-up evaluate BEFORE descriptor views (the runtime
    // allocates internals at the first evaluate and would fail after views
    // exist on this system).
    if (fgEnabled_ && desc_.frameGenerationBackend==engine::FrameGenerationBackend::Dlss) {
        ngx::DlssFgBackend::CreateDesc fd{};
        fd.width = workW_; fd.height = workH_;
        fd.renderWidth = workW_; fd.renderHeight = workH_;
        fd.backbufferFormat = outputFormat();
        ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
        if (list == nullptr) { failedBackend_=engine::FailedBackend::Infrastructure; return false; }
        failedBackend_=engine::FailedBackend::Fg;
        if (!fgBackend_->create(*coreHost_, list, ngxParams_, fd, st) || !fgBackend_->created()) {
            veyra::log::error("graph", std::format("FG create failed 0x{:X}", fgBackend_->createResult()));
            return false;
        }
        failedBackend_=engine::FailedBackend::Infrastructure;
        if(!ring_.submitAndSignal(0)||!ring_.waitIdle())return false;

        ID3D12GraphicsCommandList* wlist = ring_.acquire(0, st);
        if (wlist == nullptr) return false;
        D3D12_RESOURCE_BARRIER b[4]{};
        for (int i = 0; i < 4; ++i) {
            b[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        b[0].Transition.pResource = videoFrame_[0].Get();
        b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[1].Transition.pResource = nrZeroMotion_.Get();
        b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[2].Transition.pResource = depthTex_.Get();
        b[2].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[3].Transition.pResource = genFrame_[0].Get();
        b[3].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        wlist->ResourceBarrier(4, b);
        ngx::DlssFgBackend::EvalDesc fe{};
        fe.backbuffer = videoFrame_[0].Get();
        fe.depth = depthTex_.Get();
        fe.mvecs = nrZeroMotion_.Get();
        fe.outputInterpolated = genFrame_[0].Get();
        fe.reset = true;fe.hdr=hdr10Output();
        fe.multiFrameCount=desc_.enableFg?desc_.fgMultiplier-1:1;fe.multiFrameIndex=1;
        fe.frameId = 0; // warm-up has its own identity; product IDs start at 1
        fe.mvecScaleX = 1.0f;
        fe.mvecScaleY = 1.0f;
        bool warmOk = true;
        for(uint32_t sub=1;sub<=fe.multiFrameCount;++sub){
            fe.multiFrameIndex=sub;
            warmOk=fgBackend_->evaluate(wlist,ngxParams_,fe,st)&&warmOk;
            D3D12_RESOURCE_BARRIER u{};u.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;u.UAV.pResource=fe.outputInterpolated;wlist->ResourceBarrier(1,&u);
        }
        if(!warmOk){failedBackend_=engine::FailedBackend::Fg;return false;}
        for (int i = 0; i < 4; ++i) b[i].Transition.StateBefore = b[i].Transition.StateAfter;
        b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        b[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        b[3].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        wlist->ResourceBarrier(4, b);
        if (!ring_.submitAndSignal(0) || !ring_.waitIdle()) return false;
        veyra::log::info("graph", std::format("FG warm-up evaluate done ok={} result=0x{:X}",
            warmOk ? 1 : 0, fgBackend_->createResult()));
    }
    return true;
}

bool EnhanceGraph::createComputePasses()
{
    std::vector<uint8_t> cs;
    if(!downsamplePass_.loadShader("NrDownsample.dxil",cs)||!downsamplePass_.create(context_.device(),cs,2,1,1))return false;
    if(!residualPass_.loadShader("NrResidualComposite.dxil",cs)||!residualPass_.create(context_.device(),cs,4,3,1,24))return false;
    if(!flowAdaptPass_.loadShader("FlowAdapt.dxil",cs)||!flowAdaptPass_.create(context_.device(),cs,3,1,1))return false;
    if (!yuvPass_.loadShader("YuvToLinearRgb.dxil", cs) || !yuvPass_.create(context_.device(), cs, 8, 2, 1, 12)) return false;
    if((desc_.rgbInput||desc_.yuy2Input)&&(!rgbPass_.loadShader(desc_.yuy2Input?"Yuy2ToLinear.dxil":"RgbToLinear.dxil",cs)||!rgbPass_.create(context_.device(),cs,8,1,1)))return false;
    if (!encPass_.loadShader("ParityEncode.dxil", cs) || !encPass_.create(context_.device(), cs, 8, 1, 1)) return false;
    if (!decPass_.loadShader("ParityDecode.dxil", cs) || !decPass_.create(context_.device(), cs, 8)) return false;
    if (!blitPass_.loadShader("ScaleBlit.dxil", cs) || !blitPass_.create(context_.device(), cs, 19, 1, 1)) return false;
    // Nv12Upload stays: frame-time CopyTextureRegion is poisoned by the
    // injected layer (SEH in NGX evaluate, r33-final3 evidence); the compute
    // upload is the proven frame-path ingestion on this system.
    if (!uploadPass_.loadShader("Nv12Upload.dxil", cs) || !uploadPass_.create(context_.device(), cs, 4, 1, 2)) return false;
    if (!densifyPass_.loadShader("NvofDensify.dxil", cs) ||
        !densifyPass_.create(context_.device(), cs, 7, 5, 2)) return false;
    if(desc_.hdrOutput&&videoSrInput_&&(!hdrVideoSrPass_.loadShader("HdrVideoSrRestore.dxil",cs)||!hdrVideoSrPass_.create(context_.device(),cs,7,3,1)))return false;
    if (!stager_.initialize(context_.device(), 80)) {
        veyra::log::error("graph", "descriptor stager init failed");
        return false;
    }
    return true;
}

bool EnhanceGraph::createViews()
{
    // Descriptor initialization is DEFAULT ON since the RAW-upload-SRV removal
    // (2026-09-06): texture SRV/UAV views keep the device alive and NVOF
    // executing (r33b-tu: 599/599 executes, real motion/confidence, 0
    // removals) and eliminate the uninitialized-slot dispatches behind GBV
    // id=938. VEYRA_VIEWS_OFF preserves the legacy behavior for A/B.
    const bool viewsOn = GetEnvironmentVariableW(L"VEYRA_VIEWS_OFF", nullptr, 0) == 0;
    const bool viewsTex = viewsOn;
    const bool viewsUav = viewsOn;
    if (!viewsOn) {
        veyra::log::warn("graph", "static views DISABLED (VEYRA_VIEWS_OFF; legacy behavior)");
        return true;
    }
    auto cpu = [this](const ComputePass& p, UINT slot) { return cpuHandleOf(p, slot); };
    auto gpu = [this](const ComputePass& p, UINT slot) { return gpuHandleOf(p, slot); };
    (void)gpu;
    // SRVs must go through the staging heap (CopyDescriptorsSimple into the
    // visible heap): direct CreateShaderResourceView writes into visible
    // heaps trigger the fabricated device-removed on this system (bare
    // stage 9 evidence; the safe path is proven 600/600).
    auto stagedSrv = [this](ID3D12Resource* resource, DXGI_FORMAT fmt,
                            ComputePass& pass, UINT slot) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = fmt;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.PlaneSlice = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;
        stager_.stageSrv(resource, &srv, pass.heap.Get(), slot);
    };

    stagedSrv(srcRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass_, 15);
    stagedSrv(desc_.nrBeforeSr?srcRgba_.Get():workRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,downsamplePass_,0);
    makeUav(context_.device(),nrInput_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpu(downsamplePass_,1));
    stagedSrv(desc_.nrBeforeSr?srcRgba_.Get():workRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,residualPass_,0);
    stagedSrv(nrInput_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,residualPass_,1);
    stagedSrv(finalRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,residualPass_,2);
    makeUav(context_.device(),residualRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpu(residualPass_,3));
    stagedSrv(flowTex_.Get(),DXGI_FORMAT_R16G16_FLOAT,flowAdaptPass_,0);
    makeUav(context_.device(),nrFlow_.Get(),DXGI_FORMAT_R16G16_FLOAT,cpu(flowAdaptPass_,1));
    makeUav(context_.device(),baseFlow_.Get(),DXGI_FORMAT_R16G16_FLOAT,cpu(flowAdaptPass_,2));
    if(desc_.hdrOutput&&videoSrInput_){
        stagedSrv(srcRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,hdrVideoSrPass_,0);
        stagedSrv(videoSrInput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,hdrVideoSrPass_,1);
        stagedSrv(videoSrOutput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,hdrVideoSrPass_,2);
        makeUav(context_.device(),workRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpu(hdrVideoSrPass_,3));
        stagedSrv(residualRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,hdrVideoSrPass_,4);
        stagedSrv(videoSrInput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,hdrVideoSrPass_,5);
        stagedSrv(videoSrOutput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,hdrVideoSrPass_,6);
    }
    // Immutable per-resource views.
    if(desc_.rgbInput||desc_.yuy2Input){
        stagedSrv(rgbTex_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,rgbPass_,0);
        makeUav(context_.device(),srcRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpu(rgbPass_,1));
    }
    if (viewsTex) stagedSrv(lumaTex_.Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM, yuvPass_, 0);
    if (viewsTex) stagedSrv(chromaTex_.Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM, yuvPass_, 1);
    if (viewsUav) makeUav(context_.device(), srcRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(yuvPass_, 2));
    if (viewsTex) stagedSrv(nrInput_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, encPass_, 0);
    if (viewsUav) makeUav(context_.device(), proxyTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, cpu(encPass_, 1));
    if (viewsTex) stagedSrv(nrInput_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, decPass_, 0);
    if (viewsTex) stagedSrv(proxyTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, decPass_, 1);
    if (viewsTex) stagedSrv(neuralTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, decPass_, 2);
    if (viewsUav) makeUav(context_.device(), finalRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(decPass_, 3));
    if (viewsUav) makeUav(context_.device(), lumaTex_.Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM, cpu(uploadPass_, 2));
    if (viewsUav) makeUav(context_.device(), chromaTex_.Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM, cpu(uploadPass_, 3));
    // Software NV12 ingestion uses plane copies; no raw-SRV dispatch.
    // Blit pass layout (all static; per-use offsets chosen at bind time):
    //  0: srcRgba SRV        1: workRgba UAV      (SR bypass / NR-off blit)
    //  2: finalRgba SRV      3/4: videoFrame UAV  (section 5)
    //  5: nvofInB SRV        6: nvofInA UAV       (NVOF A:=B)
    //  7/8: videoFrame SRV   9: nvofInB UAV       (NVOF B:=video)
    // 10: genTex SRV        11/12: videoFrame SRV (presents)
    // 13/14: genTex SRV (slot 2)
    if(videoSrInput_&&viewsUav)makeUav(context_.device(),videoSrInput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,cpu(blitPass_,16));
    if(videoSrOutput_&&viewsTex)stagedSrv(videoSrOutput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,blitPass_,17);
    if (viewsTex) stagedSrv(residualRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,blitPass_,18);
    if (viewsTex) stagedSrv(srcRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass_, 0);
    if (viewsUav) makeUav(context_.device(), workRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(blitPass_, 1));
    if (viewsTex) stagedSrv(nrEnabled_&&!desc_.nrBeforeSr ? residualRgba_.Get() : workRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass_, 2);
    if (viewsUav) makeUav(context_.device(), videoFrame_[0].Get(), outputFormat(), cpu(blitPass_, 3));
    if (viewsUav) makeUav(context_.device(), videoFrame_[1].Get(), outputFormat(), cpu(blitPass_, 4));
    if (viewsTex) stagedSrv(nvofInB_.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, blitPass_, 5);
    if (viewsUav) makeUav(context_.device(), nvofInA_.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, cpu(blitPass_, 6));
    if (viewsTex) stagedSrv(videoFrame_[0].Get(), outputFormat(), blitPass_, 7);
    if (viewsTex) stagedSrv(videoFrame_[1].Get(), outputFormat(), blitPass_, 8);
    if (viewsUav) makeUav(context_.device(), nvofInB_.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, cpu(blitPass_, 9));
    if (viewsTex) stagedSrv(genFrame_[0].Get(), outputFormat(), blitPass_, 10);
    if (viewsTex) stagedSrv(genFrame_[1].Get(), outputFormat(), blitPass_, 13);
    if (viewsTex) stagedSrv(videoFrame_[0].Get(), outputFormat(), blitPass_, 11);
    if (viewsTex) stagedSrv(videoFrame_[1].Get(), outputFormat(), blitPass_, 12);

    // Densify pass views: 0=rawFlow SRV(int2) 1=cost SRV(uint)
    // 2=previous RGB,3=current RGB,4=AMD SCD;5=flow UAV,6=confidence UAV.
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC rf{};
        rf.Format = DXGI_FORMAT_R16G16_SINT;
        rf.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        rf.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        rf.Texture2D.MostDetailedMip = 0;
        rf.Texture2D.MipLevels = 1;
        rf.Texture2D.PlaneSlice = 0;
        rf.Texture2D.ResourceMinLODClamp = 0.0f;
        if (viewsTex) stager_.stageSrv(nvofRawTex_.Get(), &rf, densifyPass_.heap.Get(), 0);
        D3D12_SHADER_RESOURCE_VIEW_DESC rc{};
        rc.Format = DXGI_FORMAT_R8_UINT;
        rc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        rc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        rc.Texture2D.MostDetailedMip = 0;
        rc.Texture2D.MipLevels = 1;
        rc.Texture2D.PlaneSlice = 0;
        rc.Texture2D.ResourceMinLODClamp = 0.0f;
        if (viewsTex) stager_.stageSrv(nvofCostTex_.Get(), &rc, densifyPass_.heap.Get(), 1);
        if(viewsTex){stagedSrv(nvofInA_.Get(),DXGI_FORMAT_B8G8R8A8_UNORM,densifyPass_,2);stagedSrv(nvofInB_.Get(),DXGI_FORMAT_B8G8R8A8_UNORM,densifyPass_,3);}
        if(viewsTex){rc.Format=DXGI_FORMAT_R32_UINT;stager_.stageSrv(amdOf_?amdOf_->sceneChanges():nullptr,&rc,densifyPass_.heap.Get(),4);}
        D3D12_UNORDERED_ACCESS_VIEW_DESC uf{};
        uf.Format = DXGI_FORMAT_R16G16_FLOAT;
        uf.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        context_.device()->CreateUnorderedAccessView(flowTex_.Get(), nullptr, &uf, cpu(densifyPass_, 5));
        D3D12_UNORDERED_ACCESS_VIEW_DESC uc{};
        uc.Format = DXGI_FORMAT_R8_UNORM;
        uc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        context_.device()->CreateUnorderedAccessView(confTex_.Get(), nullptr, &uc, cpu(densifyPass_, 6));
    }
    return true;
}

// ---------------------------------------------------------------------------
// process: the per-frame chain (verbatim from the probe lambda).
// ---------------------------------------------------------------------------
bool EnhanceGraph::process(const AVFrame* frame, double ptsMs, bool reset, FrameOutputs& out, uint64_t sourceFrameId, const ColorDescription* color, bool retainReferences, const FgAdmission& admitFg)
{
    failedBackend_=engine::FailedBackend::Infrastructure;
    out = FrameOutputs{};
    out.ptsMs = ptsMs;
    if(!sourceFrameId)sourceFrameId=realFrameIndex_+1;
    diagnostics::DiagnosticEvent diagnostic;diagnostic.stage="frame";diagnostic.identity={epoch_+uint64_t(reset),desc_.settingsRevision,sourceFrameId};diagnostic.batch=realFrameIndex_+1;
    diagnostic.resolution.source={srcW_,srcH_};diagnostic.resolution.base={workW_,workH_};diagnostic.resolution.nr={nrW_,nrH_};diagnostic.resolution.flow={nvofW_,nvofH_};diagnostic.resolution.fg=diagnostic.resolution.output={workW_,workH_};diagnostic.runtimeHash="unverified-user-replaceable";diagnostic.flowApplied=std::to_string(actualFlowPerf());diagnostic.fallbackReason=mvecSource_;Logger::diagnosticContext(diagnostic);
    static const bool graphOff = GetEnvironmentVariableW(L"VEYRA_GRAPH_OFF", nullptr, 0) != 0;
    if (!frame || !initialized_ || !std::isfinite(ptsMs)) return false;
    // Reject before CPU plane access, swscale, or command-slot acquisition.
    // Source-side resize handling must rebuild the graph before submitting a
    // new extent; all upload sizes/strides still belong to this graph's extent.
    if (frame->width <= 0 || frame->height <= 0 || uint32_t(frame->width) != srcW_ || uint32_t(frame->height) != srcH_ || !av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format))) {
        veyra::log::error("graph", std::format("frame contract rejected: expected={}x{} actual={}x{} format={}", srcW_, srcH_, frame->width, frame->height, frame->format));
        return false;
    }
    const auto resolved=resolveFrameColor(*frame,color?*color:ColorDescription{});
    if(resolved.isHdrPath()&&!desc_.hdrOutput&&!toneMapPeakNits_){
        const auto peak=hdrToneMapPeak(resolved);toneMapPeakNits_=peak.nits;
        log::info("hdr-tone-map",std::format("method=BT2390-luminance sourcePeakNits={} peakSource={} targetPeakNits=203 blackNits=0 gamut=neutral-ray-soft-knee staticPerGraph=1",peak.nits,peak.source));
    }
    if(desc_.hdrInput&&!resolved.isHdrPath()){
        veyra::log::error("hdr","HDR source transfer changed to SDR; reopen/rebuild required");return false;
    }
    if(resolved.matrix==YuvMatrix::BT2020CL){veyra::log::error("graph","BT.2020 constant-luminance requires a dedicated conversion; refusing NCL substitution");return false;}
    if(resolved.isHdrPath()&&(resolved.matrix!=YuvMatrix::BT2020NCL||resolved.primaries!=ColorPrimaries::BT2020)){
        veyra::log::error("hdr","Unsupported PQ/HLG colorimetry: requires signaled/fallback BT2020 NCL and BT2020 primaries");return false;
    }
    if(reset||!realFrameIndex_)veyra::log::info("color",std::format("range={} assumed={} matrix={} assumed={} transfer={} assumed={} display709={} preserveSdrCodes={} workingTransfer={}",int(resolved.range),resolved.rangeAssumed,int(resolved.matrix),resolved.matrixAssumed,int(resolved.transfer),resolved.transferAssumed,resolved.displayReferred709,resolved.preserveSdrCodeValues,workingTransferCode(resolved)));
    if(resolved.isHdrPath()&&(reset||!realFrameIndex_)){
        veyra::log::info("hdr-route",std::format("input={} range={} matrix=BT2020-NCL primaries=BT2020 decode={} working={} output={} toneMap={} hdrReferenceWhiteNits=203 hlgReferencePeakNits=1000; SDR workingTransfer field is unused for PQ/HLG",
            resolved.transfer==TransferFunction::HLG?"HLG":"PQ",resolved.range==ColorRange::Full?"full":"limited",
            resolved.transfer==TransferFunction::HLG?"inverse-OETF+OOTF-gamma1.2":"ST2084-EOTF-absolute-nits",
            desc_.hdrOutput?"linear-BT709-scRGB-1=80nits":"linear-BT709-SDR-relative",
            hdr10Output()?"PQ-BT2020-RGB10":desc_.hdrOutput?"scRGB-FP16":"sRGB-RGB8",
            desc_.hdrOutput?"none":"BT2390-luminance+neutral-ray-gamut-compression"));
    }
    if (resolved.isHdrPath()&&(!desc_.hdrInput||desc_.rgbInput||desc_.yuy2Input)) {
        veyra::log::error("graph", "HDR input requires an explicit YUV HDR contract; RGB/YUY2 HDR ingress is unsupported"); return false;
    }
    if(std::abs(ptsMs)>9e13){veyra::log::error("timeline","PTS outside representable range");return false;}
    if(prevValid_&&(std::llround(ptsMs*10000)-std::llround(prevPtsMs_*10000)<4||ptsMs-prevPtsMs_>1000)){
        out.detectedReset=ResetReason::PtsDiscontinuity;
        reset=true;veyra::log::info("timeline","non-monotonic or discontinuous PTS; atomic history reset");
    }
    if (reset) { prevValid_ = false; scene_.reset(); previousLuma_.clear();cadence_.reset(); }

    const uint32_t parity = static_cast<uint32_t>(realFrameIndex_ % 2);
    if(!realLeases_[parity].expired()||!generatedLeases_[parity].expired()){
        veyra::log::error("frame-pool",std::format("slot={} still leased; refusing overwrite, batch={}",parity,realFrameIndex_+1));return false;
    }
    for(unsigned i=parity;i<kGeneratedPoolSlots;i+=2)if(!generatedLeases_[i].expired()){veyra::log::error("frame-pool","generated subframe still leased; refusing overwrite");return false;}
    if (graphOff) {
        prevPtsMs_ = ptsMs;
        prevValid_ = true;
        ++realFrameIndex_;
        out.realFrameIndex = realFrameIndex_;
        out.videoSlot = parity;
        out.passthrough = true;
        return true;
    }
    uint32_t slot = 0;
    if (uploadFences_[parity] && !context_.waitForFenceValue(uploadFences_[parity])) return false;
    hardwareInputFrames_[parity].reset();
    if(frame->format==AV_PIX_FMT_D3D12){
        auto* retained=av_frame_clone(frame);
        if(!retained)return false;
        hardwareInputFrames_[parity]=std::shared_ptr<AVFrame>(retained,[](AVFrame* value){av_frame_free(&value);});
    }
    Status st = Status::Ok;

    // 1. Source NV12: D3D12VA texture directly (GPU) or CPU upload.
    ID3D12Resource* nv12Texture = nullptr;
    ID3D12GraphicsCommandList* list = ring_.acquireNext(slot, st);
    if (list == nullptr) { veyra::log::error("graph", "ring acquire"); return false; }
    gpuTimer_.frame({epoch_,desc_.settingsRevision,realFrameIndex_+1},context_.fence());gpuTimer_.mark(list,GpuStage::Color);

    // Both CPU source layouts feed the same bounded scene/cadence history.
    auto analyzeLuma = [&](std::vector<uint8_t> sample) {
        std::vector<double> hist(256,0);double sad=0;
        for(auto v:sample)hist[v]+=1.0/sample.size();
        if(previousLuma_.size()==sample.size())for(size_t i=0;i<sample.size();++i)sad+=std::abs(int(sample[i])-int(previousLuma_[i]))/(255.0*sample.size());
        cadence_.observe(ptsMs,sad,previousLuma_.size()==sample.size());
        out.measuredContentRate=cadence_.confirmedRate(desc_.contentRate);
        if(cadence_.conflicts(desc_.contentRate)&&realFrameIndex_%60==0)veyra::log::warn("cadence","requested content-rate identification conflicts with observed motion; preserving source timestamps");
        out.contentDuplicate=desc_.contentRate!=engine::ContentRate::Transport&&previousLuma_.size()==sample.size()&&sad<0.0001;
        const auto analysis=scene_.analyze(realFrameIndex_,hist,sad,static_cast<uint64_t>(std::max(0.0,ptsMs)*1000));
        if(analysis.isSceneCut||analysis.isCadenceBreak){
            if(out.detectedReset==ResetReason::None)out.detectedReset=analysis.isSceneCut?ResetReason::SceneCut:ResetReason::CadenceBreak;
            reset=true;prevValid_=false;if(analysis.isSceneCut)++metrics_.sceneCutCount;
            veyra::log::info("scene",std::format("history boundary frame={} ptsMs={} cutCandidate={} cadenceBreak={} sad={} histogramDistance={}",
                realFrameIndex_,ptsMs,analysis.isSceneCut,analysis.isCadenceBreak,analysis.sadScore,analysis.histogramDistance));
        }
        previousLuma_=std::move(sample);
    };

    if(desc_.rgbInput||desc_.yuy2Input){
        if(desc_.yuy2Input?frame->format!=AV_PIX_FMT_YUYV422:(frame->format!=AV_PIX_FMT_RGBA&&frame->format!=AV_PIX_FMT_BGRA&&frame->format!=AV_PIX_FMT_RGB0&&frame->format!=AV_PIX_FMT_BGR0)){
            veyra::log::error("graph","direct RGB input contract requires packed RGBA/BGRA/RGB0/BGR0 frame");return false;
        }
        if(frame->width!=int(srcW_)||frame->height!=int(srcH_)||!frame->data[0]||std::abs(int64_t(frame->linesize[0]))<int64_t(srcW_)*(desc_.yuy2Input?2:4))return false;
        for(uint32_t y=0;y<srcH_;++y){
            auto* dst=mappedRgb_[parity]+y*rgbPitch_;const auto* src=frame->data[0]+ptrdiff_t(y)*frame->linesize[0];
            if(desc_.yuy2Input||frame->format==AV_PIX_FMT_RGBA)std::memcpy(dst,src,size_t(srcW_)*(desc_.yuy2Input?2:4));
            else for(uint32_t x=0;x<srcW_;++x){
                const bool bgr=frame->format==AV_PIX_FMT_BGRA||frame->format==AV_PIX_FMT_BGR0;
                dst[x*4]=src[x*4+(bgr?2:0)];dst[x*4+1]=src[x*4+1];dst[x*4+2]=src[x*4+(bgr?0:2)];
                dst[x*4+3]=(frame->format==AV_PIX_FMT_BGRA)?src[x*4+3]:255;
            }
        }
        if(!desc_.stillImage){
            std::vector<uint8_t> sample;sample.reserve(64*36);
            const bool bgr=frame->format==AV_PIX_FMT_BGRA||frame->format==AV_PIX_FMT_BGR0;
            for(unsigned y=0;y<36;++y)for(unsigned x=0;x<64;++x){
                const auto* p=frame->data[0]+ptrdiff_t(y*srcH_/36)*frame->linesize[0]+(x*srcW_/64)*(desc_.yuy2Input?2:4);
                sample.push_back(desc_.yuy2Input?p[0]:uint8_t((54*unsigned(p[bgr?2:0])+183*unsigned(p[1])+19*unsigned(p[bgr?0:2])+128)>>8));
            }
            analyzeLuma(std::move(sample));
        }
        tracker_.transition(list,rgbTex_.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=rgbTex_.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource=upRgb_[parity].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,desc_.yuy2Input?srcW_/2:srcW_,srcH_,1,UINT(rgbPitch_)};
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        tracker_.transition(list,rgbTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else if (frame->format == AV_PIX_FMT_D3D12) {
        auto* d3dFrame = reinterpret_cast<AVD3D12VAFrame*>(frame->data[0]);
        if (d3dFrame == nullptr || d3dFrame->texture == nullptr) {
            veyra::log::error("graph", "null d3d12va frame");
            return false;
        }
        nv12Texture = d3dFrame->texture;
        const UINT nv12Slice = static_cast<UINT>(d3dFrame->subresource_index);
        if (d3dFrame->sync_ctx.fence != nullptr) {
            if (FAILED(context_.directQueue()->Wait(
                    d3dFrame->sync_ctx.fence, d3dFrame->sync_ctx.fence_value))) {
                veyra::log::error("graph", "nv12 fence wait");
                return false;
            }
        }
        const bool isArray = nv12Texture->GetDesc().DepthOrArraySize > 1;
        D3D12_SHADER_RESOURCE_VIEW_DESC lumaSrv{};
        lumaSrv.Format = nv12Texture->GetDesc().Format==DXGI_FORMAT_P010?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM;
        lumaSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        lumaSrv.Texture2D.MostDetailedMip = 0;
        lumaSrv.Texture2D.MipLevels = 1;
        lumaSrv.Texture2D.ResourceMinLODClamp = 0.0f;
        if (isArray) {
            lumaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            lumaSrv.Texture2DArray.MostDetailedMip = 0;
            lumaSrv.Texture2DArray.MipLevels = 1;
            lumaSrv.Texture2DArray.FirstArraySlice = nv12Slice;
            lumaSrv.Texture2DArray.ArraySize = 1;
            lumaSrv.Texture2DArray.PlaneSlice = 0;
        } else {
            lumaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            lumaSrv.Texture2D.MostDetailedMip = 0;
            lumaSrv.Texture2D.MipLevels = 1;
            lumaSrv.Texture2D.PlaneSlice = 0;
        }
        // Slots 0/1 remain the CPU-upload views. Hardware views rotate with
        // hardwareInputFrames_/uploadFences_; never overwrite an in-flight SRV.
        stager_.stageSrv(nv12Texture, &lumaSrv, yuvPass_.heap.Get(), 3 + parity * 2);
        D3D12_SHADER_RESOURCE_VIEW_DESC chromaSrv{};
        chromaSrv.Format = nv12Texture->GetDesc().Format==DXGI_FORMAT_P010?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM;
        chromaSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        chromaSrv.Texture2D.MostDetailedMip = 0;
        chromaSrv.Texture2D.MipLevels = 1;
        chromaSrv.Texture2D.ResourceMinLODClamp = 0.0f;
        if (isArray) {
            chromaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            chromaSrv.Texture2DArray.MostDetailedMip = 0;
            chromaSrv.Texture2DArray.MipLevels = 1;
            chromaSrv.Texture2DArray.FirstArraySlice = nv12Slice;
            chromaSrv.Texture2DArray.ArraySize = 1;
            chromaSrv.Texture2DArray.PlaneSlice = 1;
        } else {
            chromaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            chromaSrv.Texture2D.MostDetailedMip = 0;
            chromaSrv.Texture2D.MipLevels = 1;
            chromaSrv.Texture2D.PlaneSlice = 1;
        }
        stager_.stageSrv(nv12Texture, &chromaSrv, yuvPass_.heap.Get(), 4 + parity * 2);
        tracker_.transition(list, nv12Texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else {
        nv12Ctx_ = sws_getCachedContext(nv12Ctx_, frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format),
            frame->width, frame->height, desc_.captureBitDepth==16?AV_PIX_FMT_P016:desc_.wideYuvInput()?AV_PIX_FMT_P010:AV_PIX_FMT_NV12, SWS_POINT,
            nullptr, nullptr, nullptr);
        if (nv12Ctx_ == nullptr) { veyra::log::error("graph", "sws"); return false; }
        const int colorSpace=resolved.matrix==YuvMatrix::BT2020NCL?SWS_CS_BT2020:resolved.matrix==YuvMatrix::BT601?SWS_CS_ITU601:SWS_CS_ITU709;
        const int* coefficients=sws_getCoefficients(colorSpace);
        const int full=resolved.range==ColorRange::Full?1:0;
        if(sws_setColorspaceDetails(nv12Ctx_,coefficients,full,coefficients,full,0,1<<16,1<<16)<0)return false;
        // Planar 8-bit YUV already exposes CPU luma for cadence analysis.
        // Convert directly into the fenced upload slot instead of writing and
        // copying another full NV12 frame. Never sample write-combined memory.
        const bool directUpload=!desc_.wideYuvInput()&&(frame->format==AV_PIX_FMT_YUV420P||frame->format==AV_PIX_FMT_YUVJ420P||frame->format==AV_PIX_FMT_NV12);
        uint8_t* planes[4] = { directUpload?mappedLuma_[parity]:nv12Buf_.data(), directUpload?mappedChroma_[parity]:nv12Buf_.data() + lumaSize_ };
        const int strides[4] = { static_cast<int>(lumaPitch_), static_cast<int>(chromaPitch_) };
        if(sws_scale(nv12Ctx_, frame->data, frame->linesize, 0, frame->height, planes, strides)!=frame->height){veyra::log::error("color","Software plane conversion failed");return false;}
        std::vector<uint8_t> sample;sample.reserve(64*36);
        for(unsigned y=0;y<36;++y)for(unsigned x=0;x<64;++x){const uint8_t v=directUpload?frame->data[0][ptrdiff_t(y*srcH_/36)*frame->linesize[0]+x*srcW_/64]:planes[0][size_t(y*srcH_/36)*lumaPitch_+(x*srcW_/64)*(desc_.wideYuvInput()?2:1)+(desc_.wideYuvInput()?1:0)];sample.push_back(v);}
        analyzeLuma(std::move(sample));
        if(!directUpload){for (uint32_t y = 0; y < srcH_; ++y)
            std::memcpy(mappedLuma_[parity] + y * lumaPitch_, planes[0] + y * lumaPitch_, srcW_*(desc_.wideYuvInput()?2:1));
        for (uint32_t y = 0; y < (srcH_+1) / 2; ++y)
            std::memcpy(mappedChroma_[parity] + y * chromaPitch_, planes[1] + y * chromaPitch_, ((srcW_+1)/2)*(desc_.wideYuvInput()?4:2));}
        auto copyPlane = [&](ID3D12Resource* texture, ID3D12Resource* upload,
                             DXGI_FORMAT format, UINT width, UINT height, UINT pitch) {
            tracker_.transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
            dst.pResource = texture;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.pResource = upload;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Footprint = {format, width, height, 1, pitch};
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        };
        copyPlane(lumaTex_.Get(), upLuma_[parity].Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM,
                  srcW_, srcH_, static_cast<UINT>(lumaPitch_));
        copyPlane(chromaTex_.Get(), upChroma_[parity].Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM,
                  (srcW_+1)/2, (srcH_+1)/2, static_cast<UINT>(chromaPitch_));
        tracker_.transition(list, lumaTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list, chromaTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (desc_.stageMark) desc_.stageMark("upload");

    // 2. YUV -> RGBA16F.
    tracker_.transition(list, srcRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(desc_.rgbInput||desc_.yuy2Input){
        const float c[8]={uintBits(srcW_),uintBits(srcH_),uintBits(workingTransferCode(resolved)),uintBits(resolved.range==ColorRange::Limited?1u:0u),
            resolved.range==ColorRange::Full?0.0f:1.0f,resolved.matrix==YuvMatrix::BT2020NCL?2.0f:resolved.matrix==YuvMatrix::BT601?0.0f:1.0f,resolved.primaries==ColorPrimaries::BT2020?1.0f:0.0f,0};
        rgbPass_.bind(list,c,gpuHandleOf(rgbPass_,0).ptr,gpuHandleOf(rgbPass_,1).ptr);
        list->Dispatch((srcW_+15)/16,(srcH_+15)/16,1);
    }else{
        const float constants[12] = { resolved.range==ColorRange::Full?0.0f:1.0f,
            resolved.matrix==YuvMatrix::BT2020NCL?2.0f:resolved.matrix==YuvMatrix::BT601?0.0f:1.0f,
            resolved.transfer==TransferFunction::HLG?5.0f:resolved.transfer==TransferFunction::PQ?4.0f:float(workingTransferCode(resolved)), (nv12Texture?(nv12Texture->GetDesc().Format==DXGI_FORMAT_P010?1.0f:0.0f):(desc_.captureBitDepth==16?2.0f:desc_.wideYuvInput()?1.0f:0.0f)),
            uintBits(srcW_), uintBits(srcH_), uintBits((desc_.hdrOutput?1u:0u)|(resolved.primaries==ColorPrimaries::BT2020?2u:0u)),
            uintBits(resolved.reconstructChroma?std::max(1u,unsigned(resolved.chromaLocation)):0u),toneMapPeakNits_,203.0f,0,0 };
        yuvPass_.bind(list, constants, gpuHandleOf(yuvPass_, nv12Texture ? 3 + parity * 2 : 0).ptr, gpuHandleOf(yuvPass_, 2).ptr);
        list->Dispatch((srcW_ + 15) / 16, (srcH_ + 15) / 16, 1);
    }
    tracker_.uavBarrier(list, srcRgba_.Get());
    tracker_.transition(list, srcRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (nv12Texture != nullptr) {
        tracker_.transition(list, nv12Texture, D3D12_RESOURCE_STATE_COMMON);
    }
    gpuTimer_.mark(list,GpuStage::Color,true);
    if (desc_.stageMark) desc_.stageMark("yuv");

    // Guidance from original source-space color before SR/NR. Queue waits are GPU-side.
    bool haveFlow = false;
    const bool runMotion = nvofStandalone_ || xessEnabled() || fgEnabled_ || (srEnabled_ && !desc_.videoSrQuality);
    if (runMotion && (gpuDis_ || amdOf_ || (nvof_ && nvof_->initialized()))) {
        const float dims[8] = {uintBits(nvofW_),uintBits(nvofH_),uintBits(nvofW_),uintBits(nvofH_),0,0,0,0};
        if (prevValid_) {
            tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            tracker_.transition(list,nvofInA_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            blitPass_.bind(list,dims,gpuHandleOf(blitPass_,5).ptr,gpuHandleOf(blitPass_,6).ptr);
            list->Dispatch((nvofW_+15)/16,(nvofH_+15)/16,1);
            tracker_.uavBarrier(list,nvofInA_.Get());
            tracker_.transition(list,nvofInA_.Get(),D3D12_RESOURCE_STATE_COMMON);
        }
        tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float encodedDims[8] = {uintBits(srcW_),uintBits(srcH_),uintBits(nvofW_),uintBits(nvofH_),desc_.hdrOutput?3.0f:1.0f,0,0,0};
        blitPass_.bind(list,encodedDims,gpuHandleOf(blitPass_,15).ptr,gpuHandleOf(blitPass_,9).ptr);
        list->Dispatch((nvofW_+15)/16,(nvofH_+15)/16,1);
        tracker_.uavBarrier(list,nvofInB_.Get());
        tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_COMMON);
        if(amdOf_){
            gpuTimer_.mark(list,GpuStage::Flow);
            if(!amdOf_->dispatch(list,nvofInB_.Get(),!prevValid_))return false;
            ++metrics_.amdOfExecuteCount;haveFlow=prevValid_;
            gpuTimer_.mark(list,GpuStage::Flow,true);
        }
        if(gpuDis_ && prevValid_){
            gpuTimer_.mark(list,GpuStage::Flow);
            tracker_.transition(list,flowTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
            tracker_.transition(list,confTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
            // The next ring submission signals this value; the parity upload
            // fence above retires all uses before these descriptors are reused.
            if(!gpuDis_->dispatch(list,context_.directQueue(),context_.fence(),ring_.lastSignaledValue()+1,parity,
                nvofInA_.Get(),nvofInB_.Get(),flowTex_.Get(),confTex_.Get(),previousSource_,sourceFrameId))return false;
            tracker_.transition(list,flowTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ++metrics_.gpuDisExecuteCount;haveFlow=true;
            gpuTimer_.mark(list,GpuStage::Flow,true);
        }
        if (prevValid_ && !gpuDis_) {
            if(!amdOf_){
            gpuTimer_.mark(list,GpuStage::Flow);
            if(!ring_.submitAndSignal(slot))return false;
            haveFlow=nvof_->execute(ring_.lastSignaledValue(),st);
            if(!haveFlow) { ++metrics_.nvofFrameFailures; mvecSource_="zero-motion-fallback (NVOF execute failed)"; }
            else {
                ++metrics_.nvofExecuteCount;
                if(FAILED(context_.directQueue()->Wait(nvofOutFence_.Get(),nvof_->nextOutValue()-1)))return false;
            }
            list=ring_.acquireNext(slot,st);if(!list)return false;
            gpuTimer_.mark(list,GpuStage::Flow,true);
            }
            if(haveFlow) {
                if(amdOf_)tracker_.transition(list,amdOf_->sceneChanges(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,nvofRawTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,nvofCostTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,flowTex_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                tracker_.transition(list,confTex_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                tracker_.transition(list,nvofInA_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                const float dc[8]={uintBits(rawW_),uintBits(rawH_),uintBits(nvofW_),uintBits(nvofH_),
                    uintBits(selectedGrid_),uintBits(0),uintBits(32),uintBits((desc_.validateMotion?3u:0u)|(amdOf_?4u:0u)|(amdOf_&&!amdOf_->warmedUp()?8u:0u))};
                densifyPass_.bind(list,dc,gpuHandleOf(densifyPass_,0).ptr,gpuHandleOf(densifyPass_,5).ptr);
                list->Dispatch((nvofW_+15)/16,(nvofH_+15)/16,1);
                tracker_.uavBarrier(list,flowTex_.Get());tracker_.uavBarrier(list,confTex_.Get());
                tracker_.transition(list,nvofInA_.Get(),D3D12_RESOURCE_STATE_COMMON);
                tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_COMMON);
                tracker_.transition(list,nvofRawTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
                if(amdOf_)tracker_.transition(list,amdOf_->sceneChanges(),D3D12_RESOURCE_STATE_COMMON);
                tracker_.transition(list,nvofCostTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
                tracker_.transition(list,flowTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,confTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
            }
        }
    }

    // 3. SR into workRgba (or 1:1 blit bypass).
    if(haveFlow){
        auto adapt=[&](ID3D12Resource* output,uint32_t w,uint32_t h,uint32_t descriptor){
            tracker_.transition(list,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const float c[8]={uintBits(nvofW_),uintBits(nvofH_),uintBits(w),uintBits(h),0,0,0,0};
            flowAdaptPass_.bind(list,c,gpuHandleOf(flowAdaptPass_,0).ptr,gpuHandleOf(flowAdaptPass_,descriptor).ptr);
            list->Dispatch((w+15)/16,(h+15)/16,1);tracker_.uavBarrier(list,output);tracker_.transition(list,output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        };
        adapt(nrFlow_.Get(),nrW_,nrH_,1);adapt(baseFlow_.Get(),workW_,workH_,2);
    }
    if(reset){++metrics_.resetCount;++epoch_;}
    out.historyReset=reset;
    out.batch.batchId=realFrameIndex_+1;out.batch.identity={epoch_,desc_.settingsRevision,sourceFrameId};
    out.batch.a100ns=static_cast<int64_t>(std::llround(prevPtsMs_*10000));
    out.batch.b100ns=static_cast<int64_t>(std::llround(ptsMs*10000));gpuTimer_.identity(out.batch.identity);
    auto runSr=[&]()->bool{
    if(srEnabled_&&videoSrBackend_){
        gpuTimer_.mark(list,GpuStage::Sr);
        tracker_.transition(list,videoSrInput_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float enc[8]={uintBits(srcW_),uintBits(srcH_),uintBits(srcW_),uintBits(srcH_),desc_.hdrOutput?3.0f:1.0f,0,0,0};
        blitPass_.bind(list,enc,gpuHandleOf(blitPass_,desc_.nrBeforeSr?18:0).ptr,gpuHandleOf(blitPass_,16).ptr);list->Dispatch((srcW_+15)/16,(srcH_+15)/16,1);tracker_.uavBarrier(list,videoSrInput_.Get());
        tracker_.transition(list,videoSrInput_.Get(),D3D12_RESOURCE_STATE_COMMON);tracker_.transition(list,videoSrOutput_.Get(),D3D12_RESOURCE_STATE_COMMON);
        if(!videoSrBackend_->evaluate(list,videoSrInput_.Get(),videoSrOutput_.Get(),desc_.videoSrQuality)){failedBackend_=engine::FailedBackend::Sr;return false;}
        tracker_.uavBarrier(list,videoSrOutput_.Get());tracker_.transition(list,videoSrOutput_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);tracker_.transition(list,workRgba_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float dec[8]={uintBits(workW_),uintBits(workH_),uintBits(workW_),uintBits(workH_),-1,0,0,0};
        if(desc_.hdrOutput){
            tracker_.transition(list,videoSrInput_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            hdrVideoSrPass_.bind(list,dec,gpuHandleOf(hdrVideoSrPass_,desc_.nrBeforeSr?4:0).ptr,gpuHandleOf(hdrVideoSrPass_,3).ptr);
        }else blitPass_.bind(list,dec,gpuHandleOf(blitPass_,17).ptr,gpuHandleOf(blitPass_,1).ptr);list->Dispatch((workW_+15)/16,(workH_+15)/16,1);tracker_.uavBarrier(list,workRgba_.Get());tracker_.transition(list,workRgba_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ++metrics_.srEvaluateCount;gpuTimer_.mark(list,GpuStage::Sr,true);
    } else if (srEnabled_ && srBackend_ && srBackend_->created()) {
        gpuTimer_.mark(list,GpuStage::Sr);
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ngx::DlssSrBackend::EvalDesc ed{};
        ed.color = desc_.nrBeforeSr?residualRgba_.Get():srcRgba_.Get();
        ed.output = workRgba_.Get();
        ed.depth = nrZeroDepth_.Get();
        ed.motionVectors = haveFlow?baseFlow_.Get():nrZeroMotion_.Get();
        if(desc_.srMotionProbe)ed.motionVectors=desc_.srMotionProbe;
        ed.reset = reset;
        if (!srBackend_->evaluate(list, ngxParams_, ed, st)) {
            failedBackend_=engine::FailedBackend::Sr;
            veyra::log::error("graph", "sr evaluate failed");
            return false;
        }
        ++metrics_.srEvaluateCount;gpuTimer_.mark(list,GpuStage::Sr,true);
        tracker_.uavBarrier(list, workRgba_.Get());
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else if(workRgba_.Get()!=srcRgba_.Get()) {
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = {
            uintBits(srcW_), uintBits(srcH_),
            uintBits(workW_), uintBits(workH_), 0, 0, 0, 0 };
        blitPass_.bind(list, constants, gpuHandleOf(blitPass_, 0).ptr, gpuHandleOf(blitPass_, 1).ptr);
        list->Dispatch((workW_ + 15) / 16, (workH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, workRgba_.Get());
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (desc_.stageMark) desc_.stageMark("sr");
    return true;};
    if(!desc_.nrBeforeSr&&!runSr())return false;
    if(desc_.nrBeforeSr&&retainReferences){
        tracker_.transition(list,workRgba_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float c[8]={uintBits(srcW_),uintBits(srcH_),uintBits(workW_),uintBits(workH_),0,0,0,0};
        blitPass_.bind(list,c,gpuHandleOf(blitPass_,0).ptr,gpuHandleOf(blitPass_,1).ptr);list->Dispatch((workW_+15)/16,(workH_+15)/16,1);tracker_.uavBarrier(list,workRgba_.Get());
    }
    if(retainReferences){
        // Comparison references belong to this real-frame lease. Avoid the two
        // full-frame copies when comparison is disabled.
        auto retain=[&](ID3D12Resource* src,ID3D12Resource* dst){tracker_.transition(list,src,D3D12_RESOURCE_STATE_COPY_SOURCE);tracker_.transition(list,dst,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(dst,src);tracker_.transition(list,dst,D3D12_RESOURCE_STATE_COMMON);tracker_.transition(list,src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);};
        retain(srcRgba_.Get(),sourceReferences_[parity].Get());
        if(baseReferences_[parity].Get()!=sourceReferences_[parity].Get())retain(workRgba_.Get(),baseReferences_[parity].Get());
    }


    // 4a. Parity encode.
    if (nrEnabled_ && nrHandle_ != nullptr) {
        if(nrInput_.Get()!=(desc_.nrBeforeSr?srcRgba_.Get():workRgba_.Get())){
        tracker_.transition(list,nrInput_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float down[8]={uintBits(desc_.nrBeforeSr?srcW_:workW_),uintBits(desc_.nrBeforeSr?srcH_:workH_),uintBits(nrW_),uintBits(nrH_),0,0,0,0};
        downsamplePass_.bind(list,down,gpuHandleOf(downsamplePass_,0).ptr,gpuHandleOf(downsamplePass_,1).ptr);
        list->Dispatch((nrW_+15)/16,(nrH_+15)/16,1);tracker_.uavBarrier(list,nrInput_.Get());tracker_.transition(list,nrInput_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        tracker_.transition(list, proxyTex_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = { 1.0f, 1.0f, 1.0f, desc_.hdrOutput?1.0f:0.0f,
            uintBits(nrW_), uintBits(nrH_), 0.0f, 0.0f };
        encPass_.bind(list, constants, gpuHandleOf(encPass_, 0).ptr, gpuHandleOf(encPass_, 1).ptr);
        list->Dispatch((nrW_ + 15) / 16, (nrH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, proxyTex_.Get());
        tracker_.transition(list, proxyTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (desc_.stageMark) desc_.stageMark("encode");

        // 4b. NR evaluate on a FRESH list (snippet constraint).
        if (!ring_.submitAndSignal(slot)) { veyra::log::error("graph", "submit before nr"); return false; }
        ID3D12GraphicsCommandList* nlist = ring_.acquireNext(slot, st);
        if (nlist == nullptr) { veyra::log::error("graph", "acquire nr list"); return false; }
        {
            namespace p = ngx::dlssnr;
            ngx::ParameterBlock pb(ngxParams_);
            pb.setD3D12Resource(p::kColor, proxyTex_.Get());
            pb.setD3D12Resource(p::kOutput, neuralTex_.Get());
            pb.setD3D12Resource(p::kMVec, haveFlow ? nrFlow_.Get() : nrZeroMotion_.Get());
            pb.setD3D12Resource(p::kDepth, nrZeroDepth_.Get());
            pb.setU32(p::kColorSubrectWidth, nrW_); pb.setU32(p::kColorSubrectHeight, nrH_);
            pb.setU32(p::kOutputSubrectWidth, nrW_); pb.setU32(p::kOutputSubrectHeight, nrH_);
            pb.setU32(p::kMVecSubrectWidth, nrW_); pb.setU32(p::kMVecSubrectHeight, nrH_);
            pb.setU32(p::kDepthSubrectWidth, nrW_); pb.setU32(p::kDepthSubrectHeight, nrH_);
            pb.setF32(p::kMVecScaleX, 1.0f); pb.setF32(p::kMVecScaleY, 1.0f);
            pb.setI32(p::kDepthInverted, 1);
            pb.setI32(p::kIndicatorInvertX, 0);
            pb.setI32(p::kIndicatorInvertY, 0);
            pb.setI32(p::kEnabled, 1);
            pb.setI32(p::kReset, reset ? 1 : 0);
            pb.setI32(p::kStyle, desc_.model.style);
            pb.setF32(p::kIntensity, desc_.model.intensity);
            pb.setF32(p::kLocalToneStrength, desc_.model.tone);
            pb.setF32(p::kLocalStructureStrength, desc_.model.structure);
            pb.setF32(p::kSkinStructureStrength, desc_.model.skin);
            pb.setI32(p::kUseAutoMask, desc_.model.autoMask);
            pb.setI32(p::kUICorrection, desc_.model.uiCorrection);
            if(desc_.nrParameterProbe)desc_.nrParameterProbe(ngxParams_,proxyTex_.Get(),neuralTex_.Get(),nrW_,nrH_);
            static bool injectedFailureConsumed=false;
            if(!injectedFailureConsumed&&desc_.model.style==2&&GetEnvironmentVariableW(L"VEYRA_TEST_REJECT_NR_STYLE2",nullptr,0)){
                injectedFailureConsumed=true;veyra::log::error("settings-test","injected NR execution rejection before NGX; rollback exercise, not a hardware failure");return false;
            }
            if(GetEnvironmentVariableW(L"VEYRA_TEST_NR_RUNTIME_FAILURE",nullptr,0)){
                failedBackend_=engine::FailedBackend::Nr;
                veyra::log::error("backend-recovery-test","test-only NR runtime rejection before Evaluate; exercises partially recorded command cleanup, not a hardware failure");return false;
            }
            gpuTimer_.mark(nlist,GpuStage::Nr);
            uint64_t er = 0; uint32_t es = 0;
            if (!nrAdapter_->snippetEvaluateFeature(nlist, nrHandle_, ngxParams_, er, es) ||
                er != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                diagnostic.stage="NR Evaluate";diagnostic.ngx=er;diagnostic.seh=es;Logger::diagnosticContext(diagnostic);veyra::log::error("graph", std::format("NR evaluate failed 0x{:X} seh={}", er, es));
                failedBackend_=engine::FailedBackend::Nr;
                return false;
            }
            gpuTimer_.mark(nlist,GpuStage::Nr,true);
            ++metrics_.nrEvaluateCount;
            if(haveFlow)++metrics_.nrMotionFrames;
            tracker_.uavBarrier(nlist, neuralTex_.Get());
            tracker_.transition(nlist, neuralTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            wchar_t profileFlag[2]{};
            if(GetEnvironmentVariableW(L"VEYRA_PROFILE_SPLIT",profileFlag,2)&&profileFlag[0]==L'1'){
                ring_.tag(slot,"nr-only");
                if(!ring_.submitAndSignal(slot))return false;
                nlist=ring_.acquireNext(slot,st);if(!nlist)return false;
                ring_.tag(slot,"decode-output-fg");
            }
            // 4c. Parity decode.
            tracker_.transition(nlist, finalRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const float constants[8] = { 1.0f, 1.0f, 1.0f, desc_.hdrOutput?1.0f:0.0f,
                uintBits(nrW_), uintBits(nrH_), 0.0f, 0.0f };
            decPass_.bind(nlist, constants, gpuHandleOf(decPass_, 0).ptr, gpuHandleOf(decPass_, 3).ptr);
            nlist->Dispatch((nrW_ + 15) / 16, (nrH_ + 15) / 16, 1);
            tracker_.uavBarrier(nlist, finalRgba_.Get());
            tracker_.transition(nlist, finalRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        list = nlist; // continue recording on the NR list
    }
    if (desc_.stageMark) desc_.stageMark("nr");

    if(nrEnabled_&&nrHandle_){
        gpuTimer_.mark(list,GpuStage::Residual);
        tracker_.transition(list,residualRgba_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const auto& r=desc_.residual;float c[24]={r.total,r.darken,r.brighten,r.color,r.luminance,desc_.protection.enabled?1.0f:0.0f,desc_.protection.featherPixels,desc_.hdrOutput?1.0f:0.0f};
        for(size_t i=0;i<4;++i){const auto q=desc_.protection.regions[i];c[8+i*4]=q.left;c[9+i*4]=q.top;c[10+i*4]=q.right;c[11+i*4]=q.bottom;}
        residualPass_.bind(list,c,gpuHandleOf(residualPass_,0).ptr,gpuHandleOf(residualPass_,3).ptr);
        list->Dispatch(((desc_.nrBeforeSr?srcW_:workW_)+15)/16,((desc_.nrBeforeSr?srcH_:workH_)+15)/16,1);tracker_.uavBarrier(list,residualRgba_.Get());tracker_.transition(list,residualRgba_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);gpuTimer_.mark(list,GpuStage::Residual,true);
    }

    if(desc_.nrBeforeSr&&!runSr())return false;
    // 5. Working frame -> SDR RGBA8 videoFrame[parity] + NVOF chain.
    {
        const UINT srcSlot = 2;
        const UINT uavSlot = 3 + parity;
        tracker_.transition(list, (nrEnabled_ && nrHandle_ != nullptr && !desc_.nrBeforeSr) ? residualRgba_.Get() : workRgba_.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list, videoFrame_[parity].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = {
            uintBits(workW_), uintBits(workH_),
            uintBits(workW_), uintBits(workH_), hdr10Output()?2.0f:desc_.hdrOutput?0.0f:1.0f, 0, 0, 0 };
        blitPass_.bind(list, constants, gpuHandleOf(blitPass_, srcSlot).ptr, gpuHandleOf(blitPass_, uavSlot).ptr);
        list->Dispatch((workW_ + 15) / 16, (workH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, videoFrame_[parity].Get());
        tracker_.transition(list, videoFrame_[parity].Get(), D3D12_RESOURCE_STATE_COMMON);

    }
    if(xessEnabled()){
        auto* input=haveFlow?baseFlow_.Get():nrZeroMotion_.Get();
        tracker_.transition(list,input,D3D12_RESOURCE_STATE_COPY_SOURCE);
        tracker_.transition(list,presentMotion_[parity].Get(),D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(presentMotion_[parity].Get(),input);
        tracker_.transition(list,input,D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(list,presentMotion_[parity].Get(),D3D12_RESOURCE_STATE_COMMON);
        presentMotionValid_[parity]=haveFlow&&!reset;
        motionPreviousSource_[parity]=previousSource_;
    }
    previousSource_=sourceFrameId;
    const bool fgBackendAvailable=fgEnabled_&&(fgBackend_&&fgBackend_->created());
    out.fgCandidates=fgBackendAvailable?desc_.fgMultiplier-1:0;
    const bool runFg=fgBackendAvailable&&(!admitFg||admitFg(out.batch));
    const bool resetFg=reset||!prevValid_||fgHistorySkipped_;
    out.fgRecovery=runFg&&fgHistorySkipped_;
    if(fgBackendAvailable&&!runFg){out.fgSkippedBeforeEval=out.fgCandidates;fgHistorySkipped_=true;}
    if(!runFg)gpuTimer_.resolve(list);
    if (!ring_.submitAndSignal(slot)) return false;
    if(!runFg)gpuTimer_.submitted(ring_.lastSignaledValue());
    out.videoFenceValue = ring_.lastSignaledValue();

    // FG reads enhanced SDR color, not the pre-NR guidance input.
    if (runFg && fgBackend_ && fgBackend_->created()) {
      for(uint32_t sub=1;sub<desc_.fgMultiplier;++sub){
        const uint32_t generatedSlot=parity+(sub-1)*2;
        auto* flist=ring_.acquireNext(slot,st); if(!flist)return false;
        auto* motion=haveFlow?baseFlow_.Get():nrZeroMotion_.Get();
        tracker_.transition(flist,videoFrame_[parity].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(flist,motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(flist,depthTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(flist,genFrame_[generatedSlot].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // One status resource for the whole input-frame pair, just like the
        // unchanged input parameters. Later MFG calls may retain its value.
        if(sub==1){
            tracker_.transition(flist,fgDisable_[parity].Get(),D3D12_RESOURCE_STATE_COPY_DEST);
            flist->CopyBufferRegion(fgDisable_[parity].Get(),0,fgDisableInit_.Get(),0,4);
        }
        tracker_.transition(flist,fgDisable_[parity].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ngx::DlssFgBackend::EvalDesc fe{};
        fe.hdr=hdr10Output();fe.backbuffer=videoFrame_[parity].Get(); fe.depth=depthTex_.Get(); fe.mvecs=motion;
        fe.outputInterpolated=genFrame_[generatedSlot].Get(); fe.reset=resetFg;
        fe.outputDisableInterpolation=fgDisable_[parity].Get();
        fe.multiFrameCount=desc_.fgMultiplier-1;fe.multiFrameIndex=sub;
        fe.frameId=realFrameIndex_+1;
        // FG consumer adapter: current->previous pixel flow -> normalized reverse flow.
        fe.mvecScaleX=haveFlow?-1.0f/static_cast<float>(workW_):1.0f;
        fe.mvecScaleY=haveFlow?-1.0f/static_cast<float>(workH_):1.0f;
        if(sub==1)gpuTimer_.mark(flist,GpuStage::FgBatch);
        const auto timingStage=static_cast<GpuStage>(unsigned(GpuStage::Fg1)+sub-1);gpuTimer_.mark(flist,timingStage);
        if(!fgBackend_->evaluate(flist,ngxParams_,fe,st)){failedBackend_=engine::FailedBackend::Fg;return false;}
        ++out.fgEvaluated;
        gpuTimer_.mark(flist,timingStage,true);if(sub==desc_.fgMultiplier-1)gpuTimer_.mark(flist,GpuStage::FgBatch,true);
        tracker_.uavBarrier(flist,genFrame_[generatedSlot].Get());
        tracker_.uavBarrier(flist,fgDisable_[parity].Get());
        tracker_.transition(flist,fgDisable_[parity].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
        flist->CopyBufferRegion(fgDisableReadback_[generatedSlot].Get(),0,fgDisable_[parity].Get(),0,4);
        tracker_.transition(flist,videoFrame_[parity].Get(),D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(flist,genFrame_[generatedSlot].Get(),D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(flist,depthTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
        if(sub==desc_.fgMultiplier-1)gpuTimer_.resolve(flist);
        if(!ring_.submitAndSignal(slot))return false;
        if(sub==desc_.fgMultiplier-1)gpuTimer_.submitted(ring_.lastSignaledValue());
        out.hasGenerated=!resetFg;
        out.genSlot=parity; out.genFenceValue=ring_.lastSignaledValue();
        out.generatedPtsMs=(prevPtsMs_+ptsMs)*0.5;
        if(out.hasGenerated){
            ++metrics_.fgSubmittedCandidates;
            BatchFrame f;f.identity=out.batch.identity;f.kind=FrameKind::Generated;f.validity=GenerationValidity::Pending;f.subframe=sub;
            f.pts100ns=FrameBatch::interpolate(out.batch.a100ns,out.batch.b100ns,sub,desc_.fgMultiplier);
            f.lease=std::make_shared<FrameLease>();f.lease->texture=genFrame_[generatedSlot];f.lease->slot=generatedSlot;f.lease->readyFence=out.genFenceValue;generatedLeases_[generatedSlot]=f.lease;out.batch.append(std::move(f));
        }
      }
    }
    failedBackend_=engine::FailedBackend::None;
    if(runFg)fgHistorySkipped_=false;
    uploadFences_[parity]=ring_.lastSignaledValue();

    prevPtsMs_ = ptsMs;
    prevValid_ = true;
    ++realFrameIndex_;
    out.realFrameIndex = realFrameIndex_;
    out.videoSlot = parity;
    BatchFrame real;real.identity=out.batch.identity;real.pts100ns=out.batch.b100ns;real.lease=std::make_shared<FrameLease>();real.lease->texture=videoFrame_[parity];real.lease->sourceReference=sourceReferences_[parity];real.lease->baseReference=baseReferences_[parity];real.lease->slot=parity;real.lease->readyFence=out.videoFenceValue;real.lease->referencesValid=retainReferences;realLeases_[parity]=real.lease;out.batch.append(std::move(real));
    if(veyra::log::verboseFrameLogs())veyra::log::info("frame-batch",std::format("batch={} epoch={} revision={} source={} a={} b={} count={} realSlot={} realFence={} genSlot={} genFence={}",out.batch.batchId,epoch_,out.batch.identity.settingsRevision,out.batch.identity.sourceFrameId,out.batch.a100ns,out.batch.b100ns,out.batch.count,parity,out.videoFenceValue,out.genSlot,out.genFenceValue));
    return true;
}

bool EnhanceGraph::resolveFrame(FrameOutputs& out,uint32_t index)
{
    if(index>=out.batch.count)return false;
    auto& frame=out.batch.frames[index];
    if(!frame.lease||context_.fence()->GetCompletedValue()<frame.lease->readyFence)return false;
    if(frame.kind!=FrameKind::Generated||frame.validity!=GenerationValidity::Pending)return true;
    void* data=nullptr;D3D12_RANGE range{0,4};
    HRESULT hr=fgDisableReadback_[frame.lease->slot]->Map(0,&range,&data);
    if(FAILED(hr)){frame.validity=GenerationValidity::Failed;veyra::log::error("fg-status",std::format("Map hr=0x{:X}",unsigned(hr)));return true;}
    const bool disabled=*static_cast<uint8_t*>(data)!=0;
    const bool rejected=disabled||out.contentDuplicate;
    D3D12_RANGE written{0,0};fgDisableReadback_[frame.lease->slot]->Unmap(0,&written);
    frame.validity=rejected?GenerationValidity::Disabled:GenerationValidity::Valid;
    if(rejected)++metrics_.fgDisabledFrames;else ++metrics_.fgGeneratedFrames;
    if(veyra::log::verboseFrameLogs())veyra::log::info("fg-status",std::format("batch={} frame={} epoch={} revision={} subframe={} fence={} disable={} valid={}",out.batch.batchId,out.realFrameIndex,out.batch.identity.epoch,out.batch.identity.settingsRevision,frame.subframe,frame.lease->readyFence,disabled,!rejected));
    return true;
}

bool EnhanceGraph::resolveGeneration(FrameOutputs& out)
{
    bool complete=true,anyValid=false;
    for(uint32_t i=0;i<out.batch.count;++i){
        if(!resolveFrame(out,i))complete=false;
        const auto& frame=out.batch.frames[i];
        anyValid|=frame.kind==FrameKind::Generated&&frame.validity==GenerationValidity::Valid;
    }
    // Warmup evaluates may have no exposed generated leases, but their status
    // and resources must finish before full-batch completion is reported.
    if(context_.fence()->GetCompletedValue()<out.genFenceValue)complete=false;
    if(complete)out.hasGenerated=anyValid;
    return complete;
}

bool EnhanceGraph::applySettings(const engine::EnhancementSettings& s){
    // A non-NVIDIA adapter keeps the user's requested NR setting in the UI
    // but has this NGX-only stage explicitly disabled in the graph. Do not
    // reject unrelated live settings in that degraded, playable state.
    if(s.nr&&desc_.enableNr&&!nrHandle_)return false;
    if(s.opticalFlowBackend!=desc_.opticalFlowBackend||s.amdFlowHalfResolution!=desc_.amdFlowHalfResolution)return false;
    // DLSS allocates multiplier-specific feature/output resources.
    // XeSS has a fixed 2X proxy swapchain contract. Settings callers must
    // rebuild instead of accepting a change that cannot take effect in place.
    if(s.nrRuntime!=desc_.nrRuntime||std::max(2u,s.multiplier)!=desc_.fgMultiplier||s.frameGenerationBackend!=desc_.frameGenerationBackend||s.videoSrQuality!=desc_.videoSrQuality||!s.validate().empty()||(s.multiplier>1&&s.frameGenerationBackend!=engine::FrameGenerationBackend::XeSS&&(!fgCapsAvailable_||s.multiplier-1>uint32_t(fgMultiFrameMax_))))return false;
    desc_.contentRate=s.content;desc_.model=s.model;desc_.residual=s.residual;desc_.protection=s.protection;desc_.settingsRevision=s.revision;
    desc_.fgMultiplier=std::max(2u,s.multiplier);desc_.enableNvofStandalone=s.nr&&!desc_.stillImage;nvofStandalone_=desc_.enableNvofStandalone;
    setNrEnabled(s.nr);setFgEnabled(s.multiplier>1&&s.frameGenerationBackend!=engine::FrameGenerationBackend::XeSS);
    veyra::log::info("settings",std::format("requested revision={} intensity={} tone={} structure={} skin={} style={} autoMask={} UI={} residual={}/{}/{}/{}/{} multiplier={}",s.revision,s.model.intensity,s.model.tone,s.model.structure,s.model.skin,s.model.style,s.model.autoMask,s.model.uiCorrection,s.residual.total,s.residual.darken,s.residual.brighten,s.residual.color,s.residual.luminance,s.multiplier));
    return true;
}
void EnhanceGraph::setNrEnabled(bool on)
{
    nrEnabled_ = on && nrHandle_ != nullptr;
    // Refresh the section-5 blit SRV exactly as the probe did at runtime
    // (direct write into the visible heap - proven safe for this refresh).
    makeSrv(context_.device(),
        nrEnabled_&&!desc_.nrBeforeSr ? residualRgba_.Get() : workRgba_.Get(),
        DXGI_FORMAT_R16G16B16A16_FLOAT, cpuHandleOf(blitPass_, 2));
}

ID3D12Fence* EnhanceGraph::contextFence()const{return context_.fence();}
uint32_t EnhanceGraph::actualFlowPerf()const{return nvof_?nvof_->caps().actualPerfLevel:0;}
bool EnhanceGraph::fgCreated() const
{
    return fgBackend_ && fgBackend_->created();
}

uint64_t EnhanceGraph::lastNvofSignal() const
{
    return (nvof_ && nvof_->initialized()) ? (nvof_->nextOutValue() - 1) : 0;
}

bool EnhanceGraph::nvofSessionInitialized() const
{
    return nvof_ && nvof_->initialized();
}

uint64_t EnhanceGraph::nvofNextOutValue() const
{
    return (nvof_ && nvof_->initialized()) ? nvof_->nextOutValue() : 0;
}

ID3D12Resource* EnhanceGraph::videoFrameResource(uint32_t slot) const
{
    return slot < 2 ? videoFrame_[slot].Get() : nullptr;
}

ID3D12Resource* EnhanceGraph::generatedFrameResource(uint32_t slot) const
{
    return slot < kGeneratedPoolSlots ? genFrame_[slot].Get() : nullptr;
}

// ---------------------------------------------------------------------------
// shutdown: s10 ownership-order teardown of everything the graph owns.
// The NVOF out-fence drain must ALREADY have happened (caller), while the
// ring and fences were alive.
// ---------------------------------------------------------------------------
void EnhanceGraph::shutdown()
{
    if (!initialized_ && !nrAdapter_ && !nvof_ && !srcRgba_) return;
    (void)ring_.drainQueue();(void)ring_.discardRecording();
    for(auto& input:hardwareInputFrames_)input.reset();
    Status st = Status::Ok;
    if (nv12Ctx_ != nullptr) { sws_freeContext(nv12Ctx_); nv12Ctx_ = nullptr; }

    if (nrHandle_ != nullptr) {
        uint64_t rr = 0; uint32_t rs = 0;
        (void)nrAdapter_->snippetReleaseFeature(nrHandle_, rr, rs);
        nrHandle_ = nullptr;
    }
    gpuDis_.reset();
    amdOf_.reset();
    for(auto& motion:presentMotion_)motion.Reset();
    if (fgBackend_) fgBackend_->release();
    if(videoSrBackend_){videoSrBackend_->release();videoSrBackend_.reset();}
    if (srBackend_) srBackend_->release();

    // NVOF teardown in THREE phases (ownership rule proven by t10-L0-r2):
    // a. unregisterAll() while the textures are still alive;
    // b. release the four registered textures (DLL still loaded);
    // c. shutdown() = nvOFDestroy + FreeLibrary.
    if (nvof_ && nvof_->initialized()) {
        veyra::Status us = veyra::Status::Ok;
        if (!nvof_->unregisterAll(us)) {
            veyra::log::error("graph", std::format("nvof unregisterAll failed status={}", static_cast<int>(us)));
        }
    }
    nvofCostTex_.Reset();
    nvofRawTex_.Reset();
    nvofInB_.Reset();
    nvofInA_.Reset();
    if (nvof_) nvof_->shutdown();

    if (nvofOutEvent_ != nullptr) { CloseHandle(nvofOutEvent_); nvofOutEvent_ = nullptr; }
    if (coreHost_ && coreHost_->initialized() && ngxParams_ != nullptr) {
        coreHost_->destroyParameters(ngxParams_);
        ngxParams_ = nullptr;
    }
    if (nrAdapter_) {
        nrAdapter_->restoreCallerCompatibility();
        nrAdapter_->unload();
    }
    if (coreHost_) coreHost_->shutdown();

    // Staged explicit release (scope-end destructors then have nothing left).
    decPass_ = ComputePass{};
    rgbPass_={};rgbTex_.Reset();
    for(unsigned i=0;i<2;++i){if(upRgb_[i]&&mappedRgb_[i])upRgb_[i]->Unmap(0,nullptr);mappedRgb_[i]=nullptr;upRgb_[i].Reset();}
    downsamplePass_={};residualPass_={};flowAdaptPass_={};
    nrInput_.Reset();residualRgba_.Reset();nrFlow_.Reset();baseFlow_.Reset();
    for(unsigned i=0;i<kGeneratedPoolSlots;++i){fgDisable_[i].Reset();fgDisableReadback_[i].Reset();generatedLeases_[i].reset();genFrame_[i].Reset();}for(auto& lease:realLeases_)lease.reset();fgDisableInit_.Reset();
    encPass_ = ComputePass{};
    blitPass_ = ComputePass{};hdrVideoSrPass_={};
    yuvPass_ = ComputePass{};
    toneMapPeakNits_=0;
    densifyPass_ = ComputePass{};
    confTex_.Reset();
    flowTex_.Reset();
    depthTex_.Reset();
    nrZeroMotion_.Reset();
    nrZeroDepth_.Reset();
    genFrame_[0].Reset();
    genFrame_[1].Reset();
    videoFrame_[0].Reset();
    videoFrame_[1].Reset();
    finalRgba_.Reset();
    neuralTex_.Reset();
    proxyTex_.Reset();
    for(auto& r:sourceReferences_)r.Reset();for(auto& r:baseReferences_)r.Reset();workRgba_.Reset();
    srcRgba_.Reset();videoSrInput_.Reset();videoSrOutput_.Reset();
    chromaTex_.Reset();
    lumaTex_.Reset();
    upZeroMotion_.Reset();
    upZeroDepth_.Reset();
    upDepth_.Reset();
    upChroma_[0].Reset();
    upChroma_[1].Reset();
    upLuma_[0].Reset();
    upLuma_[1].Reset();
    nvofOutFence_.Reset();
    nvof_.reset();
    fgBackend_.reset();
    srBackend_.reset();
    nrAdapter_.reset();
    coreHost_.reset();
    gpuTimer_.close();initialized_ = false;
    veyra::log::info("graph", "shutdown complete (ordered)");
    (void)st;
}

} // namespace veyra::pipeline
