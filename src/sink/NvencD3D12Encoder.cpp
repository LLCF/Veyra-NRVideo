#include "veyra/sink/NvencD3D12Encoder.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <ffnvcodec/nvEncodeAPI.h>
#include <array>
#include <bit>
#include <format>
#include <algorithm>
namespace veyra::sink {
using namespace veyra::pipeline;
struct NvencD3D12Encoder::Impl {
    HMODULE dll=nullptr;void* encoder=nullptr;NV_ENCODE_API_FUNCTION_LIST api{};
    gfx::D3D12DeviceContext* ctx=nullptr;gfx::CommandSlotRing* ring=nullptr;EnhanceGraph* graph=nullptr;
    bool hdr=false;NV_ENC_BUFFER_FORMAT inputFormat=NV_ENC_BUFFER_FORMAT_NV12;
    unsigned w=0,h=0;uint64_t submitted=0,completed=0;
    ComPtr<ID3D12Fence> fence;HANDLE event=nullptr;
    ComputePass convert;StateTracker states;ComPtr<ID3D12Resource> y,uv;
    PacketWriter writer;std::vector<uint8_t> sequence;
    struct Slot {ComPtr<ID3D12Resource> input,output;NV_ENC_REGISTERED_PTR registeredIn=nullptr,registeredOut=nullptr;NV_ENC_INPUT_PTR mappedIn=nullptr,mappedOut=nullptr;NV_ENC_INPUT_RESOURCE_D3D12 in{};NV_ENC_OUTPUT_RESOURCE_D3D12 out{};uint64_t value=0;bool pending=false;};
    std::array<Slot,4> slots;
    bool check(NVENCSTATUS code,const char* op){veyra::log::info("nvenc",std::format("{} status={} detail={}",op,int(code),code!=NV_ENC_SUCCESS&&encoder&&api.nvEncGetLastErrorString?api.nvEncGetLastErrorString(encoder):""));return code==NV_ENC_SUCCESS;}
    bool drain(Slot& s){
        if(!s.pending)return true;
        if(fence->GetCompletedValue()<s.value){if(FAILED(fence->SetEventOnCompletion(s.value,event))||WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)return false;}
        NV_ENC_LOCK_BITSTREAM lock{};lock.version=NV_ENC_LOCK_BITSTREAM_VER;lock.outputBitstream=&s.out;lock.doNotWait=0;
        if(!check(api.nvEncLockBitstream(encoder,&lock),"LockBitstream"))return false;
        const bool ok=writer(static_cast<uint8_t*>(lock.bitstreamBufferPtr),lock.bitstreamSizeInBytes,static_cast<int64_t>(lock.outputTimeStamp),lock.pictureType==NV_ENC_PIC_TYPE_IDR);
        const bool unlock=check(api.nvEncUnlockBitstream(encoder,&s.out),"UnlockBitstream");
        s.pending=false;++completed;return ok&&unlock;
    }
};
NvencD3D12Encoder::NvencD3D12Encoder():p_(std::make_unique<Impl>()){}
NvencD3D12Encoder::~NvencD3D12Encoder(){close();}
std::vector<uint8_t> NvencD3D12Encoder::headers()const{return p_->sequence;}
bool NvencD3D12Encoder::open(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,EnhanceGraph& graph,bool hevc,unsigned fpsNum,unsigned fpsDen,PacketWriter writer){
    auto& p=*p_;p.ctx=&ctx;p.ring=&ring;p.graph=&graph;p.w=graph.workWidth();p.h=graph.workHeight();p.writer=std::move(writer);
    wchar_t dir[MAX_PATH]{};GetSystemDirectoryW(dir,MAX_PATH);const auto dllPath=std::wstring(dir)+L"\\nvEncodeAPI64.dll";
    p.dll=LoadLibraryExW(dllPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!p.dll)return false;
    using Create=NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);auto create=reinterpret_cast<Create>(GetProcAddress(p.dll,"NvEncodeAPICreateInstance"));
    p.api.version=NV_ENCODE_API_FUNCTION_LIST_VER;if(!create||!p.check(create(&p.api),"CreateInstance"))return false;
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};open.version=NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;open.apiVersion=NVENCAPI_VERSION;open.device=ctx.device();open.deviceType=NV_ENC_DEVICE_TYPE_DIRECTX;
    // Some systems carry an older nvEncodeAPI64.dll (System32 copy from a
    // mixed driver/tool install) which rejects the compiled 13.1 declaration
    // with NV_ENC_ERR_INVALID_VERSION (status 15) - observed on a user RTX
    // 5060 where every export died here. Older API versions are fully
    // sufficient for our usage (H.264/HEVC, D3D12, low latency), so retry
    // downwards before giving up.
    // Test-only: pretend the compiled declaration was refused, so the fallback
    // ladder below still gets exercised on a healthy driver. Never set by the UI.
    const bool forcedMiss=GetEnvironmentVariableW(L"VEYRA_TEST_NVENC_FIRST_OPEN_FAILS",nullptr,0)>0;
    if(forcedMiss)veyra::log::warn("nvenc","test-only: skipping the first OpenD3D12Session so the apiVersion ladder runs");
    if(forcedMiss||!p.check(p.api.nvEncOpenEncodeSessionEx(&open,&p.encoder),"OpenD3D12Session")) {
        const uint32_t fallbackVersions[]={NVENCAPI_MAJOR_VERSION,13u,12u,11u};
        bool opened=false;uint32_t accepted=0;
        for(const uint32_t major:fallbackVersions){
            if(!forcedMiss&&major==NVENCAPI_MAJOR_VERSION)continue; // already refused above
            open.apiVersion=major;
            const auto result=p.api.nvEncOpenEncodeSessionEx(&open,&p.encoder);
            log::info("nvenc",std::format("OpenD3D12Session retry apiVersion={}.0 status={}",major,unsigned(result)));
            if(result==NV_ENC_SUCCESS){opened=true;accepted=major;break;}
        }
        if(!opened)return false;
        log::info("nvenc",std::format("OpenD3D12Session accepted apiVersion={}.0 (compiled {}.0 was refused)",accepted,NVENCAPI_MAJOR_VERSION));
    }
    p.hdr=graph.hdrOutput();if(p.hdr&&!hevc){veyra::log::error("nvenc","HDR export requires HEVC Main10");return false;}
    p.inputFormat=p.hdr?NV_ENC_BUFFER_FORMAT_YUV420_10BIT:NV_ENC_BUFFER_FORMAT_NV12;
    const GUID codec=hevc?NV_ENC_CODEC_HEVC_GUID:NV_ENC_CODEC_H264_GUID;
    int maxWidth=0,maxHeight=0;
    NV_ENC_CAPS_PARAM caps{};caps.version=NV_ENC_CAPS_PARAM_VER;caps.capsToQuery=NV_ENC_CAPS_WIDTH_MAX;
    if(!p.check(p.api.nvEncGetEncodeCaps(p.encoder,codec,&caps,&maxWidth),"WidthMax"))return false;
    caps.capsToQuery=NV_ENC_CAPS_HEIGHT_MAX;
    if(!p.check(p.api.nvEncGetEncodeCaps(p.encoder,codec,&caps,&maxHeight),"HeightMax"))return false;
    veyra::log::info("nvenc",std::format("codec={} requested={}x{} maximum={}x{}",hevc?"HEVC":"H264",p.w,p.h,maxWidth,maxHeight));
    if(maxWidth<=0||maxHeight<=0||p.w>unsigned(maxWidth)||p.h>unsigned(maxHeight)||(p.w&1)||(p.h&1)){
        veyra::log::error("nvenc","requested dimensions unsupported by selected codec/NV12 input");return false;
    }
    if(p.hdr){int supported=0;caps.capsToQuery=NV_ENC_CAPS_SUPPORT_10BIT_ENCODE;if(!p.check(p.api.nvEncGetEncodeCaps(p.encoder,codec,&caps,&supported),"10bit support")||!supported)return false;}
    NV_ENC_PRESET_CONFIG preset{};preset.version=NV_ENC_PRESET_CONFIG_VER;preset.presetCfg.version=NV_ENC_CONFIG_VER;
    if(!p.check(p.api.nvEncGetEncodePresetConfigEx(p.encoder,codec,NV_ENC_PRESET_P4_GUID,NV_ENC_TUNING_INFO_LOW_LATENCY,&preset),"GetPreset"))return false;
    preset.presetCfg.frameIntervalP=1;preset.presetCfg.gopLength=120;preset.presetCfg.rcParams.rateControlMode=NV_ENC_PARAMS_RC_CONSTQP;
    preset.presetCfg.rcParams.constQP={20,22,22};preset.presetCfg.rcParams.enableLookahead=0;
    if(p.hdr){
        preset.presetCfg.profileGUID=NV_ENC_HEVC_PROFILE_MAIN10_GUID;
        auto& config=preset.presetCfg.encodeCodecConfig.hevcConfig;
        config.inputBitDepth=config.outputBitDepth=NV_ENC_BIT_DEPTH_10;
        auto& vui=config.hevcVUIParameters;
        vui.videoSignalTypePresentFlag=1;vui.videoFormat=NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;vui.videoFullRangeFlag=0;vui.colourDescriptionPresentFlag=1;
        vui.colourPrimaries=NV_ENC_VUI_COLOR_PRIMARIES_BT2020;
        vui.transferCharacteristics=NV_ENC_VUI_TRANSFER_CHARACTERISTIC_SMPTE2084;
        vui.colourMatrix=NV_ENC_VUI_MATRIX_COEFFS_BT2020_NCL;
    }
    NV_ENC_INITIALIZE_PARAMS init{};init.version=NV_ENC_INITIALIZE_PARAMS_VER;init.encodeGUID=codec;init.presetGUID=NV_ENC_PRESET_P4_GUID;init.tuningInfo=NV_ENC_TUNING_INFO_LOW_LATENCY;
    init.encodeWidth=p.w;init.encodeHeight=p.h;init.darWidth=p.w;init.darHeight=p.h;init.frameRateNum=fpsNum;init.frameRateDen=fpsDen;init.enablePTD=1;init.encodeConfig=&preset.presetCfg;init.maxEncodeWidth=p.w;init.maxEncodeHeight=p.h;
    init.bufferFormat=p.inputFormat;
    if(!p.check(p.api.nvEncInitializeEncoder(p.encoder,&init),"InitializeEncoder"))return false;
    p.sequence.resize(4096);uint32_t size=0;NV_ENC_SEQUENCE_PARAM_PAYLOAD seq{};seq.version=NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;seq.inBufferSize=4096;seq.spsppsBuffer=p.sequence.data();seq.outSPSPPSPayloadSize=&size;
    if(!p.check(p.api.nvEncGetSequenceParams(p.encoder,&seq),"GetSequenceParams"))return false;p.sequence.resize(size);
    if(FAILED(ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&p.fence))))return false;p.event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!p.event)return false;
    p.y=makeTexture(ctx.device(),p.w,p.h,p.hdr?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM,true);p.uv=makeTexture(ctx.device(),p.w/2,p.h/2,p.hdr?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM,true);if(!p.y||!p.uv)return false;
    std::vector<uint8_t> cs;if(!p.convert.loadShader("RgbToNv12.dxil",cs)||!p.convert.create(ctx.device(),cs,10,1,2))return false;
    for(unsigned i=0;i<8;++i)makeSrv(ctx.device(),i<2?graph.videoFrameResource(i):graph.generatedFrameResource(i-2),graph.outputFormat(),cpuHandleOf(p.convert,i));
    makeUav(ctx.device(),p.y.Get(),p.hdr?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM,cpuHandleOf(p.convert,8));makeUav(ctx.device(),p.uv.Get(),p.hdr?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM,cpuHandleOf(p.convert,9));
    for(auto& s:p.slots){
        s.input=makeTexture(ctx.device(),p.w,p.h,p.hdr?DXGI_FORMAT_P010:DXGI_FORMAT_NV12,false);if(!s.input)return false;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=(uint64_t(p.w)*p.h*4+4095)&~4095ull;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&s.output))))return false;
        auto reg=[&](ID3D12Resource* resource,bool output,NV_ENC_REGISTERED_PTR& registered,NV_ENC_INPUT_PTR& mapped){NV_ENC_REGISTER_RESOURCE r{};r.version=NV_ENC_REGISTER_RESOURCE_VER;r.resourceType=NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;r.resourceToRegister=resource;r.width=output?static_cast<uint32_t>(bd.Width):p.w;r.height=output?1:p.h;r.bufferFormat=output?NV_ENC_BUFFER_FORMAT_U8:p.inputFormat;r.bufferUsage=output?NV_ENC_OUTPUT_BITSTREAM:NV_ENC_INPUT_IMAGE;
            if(!p.check(p.api.nvEncRegisterResource(p.encoder,&r),output?"RegisterOutput":p.hdr?"RegisterP010":"RegisterNV12"))return false;registered=r.registeredResource;
            NV_ENC_MAP_INPUT_RESOURCE m{};m.version=NV_ENC_MAP_INPUT_RESOURCE_VER;m.registeredResource=registered;if(!p.check(p.api.nvEncMapInputResource(p.encoder,&m),"MapResource"))return false;mapped=m.mappedResource;return true;};
        if(!reg(s.input.Get(),false,s.registeredIn,s.mappedIn)||!reg(s.output.Get(),true,s.registeredOut,s.mappedOut))return false;
        s.in.version=NV_ENC_INPUT_RESOURCE_D3D12_VER;s.in.pInputBuffer=s.mappedIn;s.in.inputFencePoint.version=NV_ENC_FENCE_POINT_D3D12_VER;s.in.inputFencePoint.pFence=ctx.fence();s.in.inputFencePoint.bWait=1;
        s.out.version=NV_ENC_OUTPUT_RESOURCE_D3D12_VER;s.out.pOutputBuffer=s.mappedOut;s.out.outputFencePoint.version=NV_ENC_FENCE_POINT_D3D12_VER;s.out.outputFencePoint.pFence=p.fence.Get();s.out.outputFencePoint.bSignal=1;
    }return true;
}
bool NvencD3D12Encoder::encode(unsigned frameSlot,bool generated,int64_t pts){auto& p=*p_;auto& s=p.slots[p.submitted%4];if(!p.drain(s))return false;
    Status st=Status::Ok;uint32_t slot=0;auto* list=p.ring->acquireNext(slot,st);if(!list)return false;
    auto* color=generated?p.graph->generatedFrameResource(frameSlot):p.graph->videoFrameResource(frameSlot);
    p.states.transition(list,color,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);p.states.transition(list,p.y.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);p.states.transition(list,p.uv.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const float dims[8]={std::bit_cast<float>(p.w),std::bit_cast<float>(p.h),std::bit_cast<float>(p.hdr?(p.graph->hdr10Output()?2u:1u):0u),0,0,0,0,0};p.convert.bind(list,dims,gpuHandleOf(p.convert,frameSlot+(generated?2:0)).ptr,gpuHandleOf(p.convert,8).ptr);list->Dispatch((p.w+15)/16,(p.h+15)/16,1);
    p.states.uavBarrier(list,p.y.Get());p.states.uavBarrier(list,p.uv.Get());p.states.transition(list,p.y.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);p.states.transition(list,p.uv.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);p.states.transition(list,s.input.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
    for(unsigned plane=0;plane<2;++plane){D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=plane?p.uv.Get():p.y.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=s.input.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.SubresourceIndex=plane;list->CopyTextureRegion(&b,0,0,0,&a,nullptr);}
    p.states.transition(list,s.input.Get(),D3D12_RESOURCE_STATE_COMMON);p.states.transition(list,color,D3D12_RESOURCE_STATE_COMMON);if(!p.ring->submitAndSignal(slot))return false;
    s.in.inputFencePoint.waitValue=p.ring->lastSignaledValue();s.value=p.submitted+1;s.out.outputFencePoint.signalValue=s.value;
    NV_ENC_PIC_PARAMS pic{};pic.version=NV_ENC_PIC_PARAMS_VER;pic.inputWidth=p.w;pic.inputHeight=p.h;pic.inputBuffer=&s.in;pic.outputBitstream=&s.out;pic.bufferFmt=p.inputFormat;pic.pictureStruct=NV_ENC_PIC_STRUCT_FRAME;pic.inputTimeStamp=pts;pic.inputDuration=1;
    if(!p.check(p.api.nvEncEncodePicture(p.encoder,&pic),"EncodePicture"))return false;s.pending=true;++p.submitted;return true;
}
bool NvencD3D12Encoder::finish(){auto& p=*p_;while(p.completed<p.submitted){if(!p.drain(p.slots[p.completed%4]))return false;}return true;}
void NvencD3D12Encoder::close(){if(!p_)return;auto& p=*p_;if(p.encoder){finish();for(auto& s:p.slots){if(s.mappedOut)p.api.nvEncUnmapInputResource(p.encoder,s.mappedOut);if(s.mappedIn)p.api.nvEncUnmapInputResource(p.encoder,s.mappedIn);if(s.registeredOut)p.api.nvEncUnregisterResource(p.encoder,s.registeredOut);if(s.registeredIn)p.api.nvEncUnregisterResource(p.encoder,s.registeredIn);}p.api.nvEncDestroyEncoder(p.encoder);p.encoder=nullptr;}if(p.event){CloseHandle(p.event);p.event=nullptr;}if(p.dll){FreeLibrary(p.dll);p.dll=nullptr;}}
}
