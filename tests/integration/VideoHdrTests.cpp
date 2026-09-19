#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/RuntimePaths.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include <DirectXPackedVector.h>
#include <algorithm>
#include <cmath>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra;
using namespace veyra::pipeline;

// Diagnostic readback only. The product conversion stays on the GPU.
static bool pixels(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,ID3D12Resource* texture,std::vector<float>& result){
    const auto d=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    ctx.device()->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buffer;if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer))))return false;
    unsigned slot=0;Status status;auto* list=ring.acquireNext(slot,status);if(!list)return false;
    StateTracker states;states.transition(list,texture,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=texture;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=buffer.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);states.transition(list,texture,D3D12_RESOURCE_STATE_COMMON);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;
    void* p=nullptr;D3D12_RANGE range{0,size_t(bytes)};if(FAILED(buffer->Map(0,&range,&p)))return false;
    result.resize(size_t(d.Width)*d.Height*3);
    for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x)for(unsigned c=0;c<3;++c){
        const auto* row=static_cast<const uint8_t*>(p)+fp.Offset+y*fp.Footprint.RowPitch;
        float value=0;
        if(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT)value=DirectX::PackedVector::XMConvertHalfToFloat(reinterpret_cast<const uint16_t*>(row)[x*4+c]);
        else if(d.Format==DXGI_FORMAT_R10G10B10A2_UNORM){const double coded=double((reinterpret_cast<const uint32_t*>(row)[x]>>(10*c))&1023)/1023;const double t=pow(coded,32.0/2523);value=float(125*pow(std::max(t-3424.0/4096,0.0)/(2413.0/128-2392.0/128*t),16384.0/2610));}
        else {buffer->Unmap(0,nullptr);return false;}
        result[(size_t(y)*d.Width+x)*3+c]=value;
    }
    D3D12_RANGE none{};buffer->Unmap(0,&none);return true;
}
int wmain(int argc,wchar_t** argv){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;gfx::DeviceContextDesc dd;
    if(!ctx.initialize(dd,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    EnhanceGraph graph(ctx,ring);EnhanceGraphDesc gd;
    gd.sourceWidth=gd.workWidth=gd.nrWidth=1280;gd.sourceHeight=gd.workHeight=gd.nrHeight=720;
    gd.rgbInput=true;gd.enableNr=gd.enableSr=gd.enableFg=gd.enableNvofStandalone=false;
    gd.videoHdr.enabled=true;gd.hdrOutput=true;gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    if(argc>1){const auto multiplier=unsigned(_wtoi(argv[1]));gd.fgMultiplier=std::max(2u,multiplier);gd.enableFg=multiplier>1;}
    if(argc>2){gd.enableNr=_wtoi(argv[2])!=0;gd.enableNvofStandalone=gd.enableNr;}
    if(argc>3&&_wtoi(argv[3])>0){gd.enableSr=true;gd.workWidth=1920;gd.workHeight=1080;gd.videoSrQuality=unsigned(_wtoi(argv[3]));}
    source::MediaFileSource media;const AVFrame* decodedFrame=nullptr;FramePacket packet;
    if(argc>5){source::SourceOpenDesc input;input.path=argv[5];input.preferHardwareDecode=false;
        if(!media.open(input)||media.read(packet,&decodedFrame)!=source::SourceReadStatus::Frame)return 6;
        gd.sourceWidth=gd.workWidth=gd.nrWidth=decodedFrame->width;gd.sourceHeight=gd.workHeight=gd.nrHeight=decodedFrame->height;gd.rgbInput=false;
    }
    if(!graph.initialize(gd)||!graph.createViews()||!graph.videoHdrActive())return 3;
    AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_BGRA;frame->width=1280;frame->height=720;
    frame->color_range=AVCOL_RANGE_JPEG;frame->color_primaries=AVCOL_PRI_BT709;frame->color_trc=AVCOL_TRC_IEC61966_2_1;
    if(av_frame_get_buffer(frame,32)<0)return 4;
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto* p=frame->data[0]+y*frame->linesize[0]+x*4;const uint8_t v=uint8_t(x*255/1279);p[0]=p[1]=p[2]=v;p[3]=255;}
    if(argc>4)for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto* p=frame->data[0]+y*frame->linesize[0]+x*4;p[0]=uint8_t(x*17+y*13);p[1]=uint8_t(x/5+y/3);p[2]=uint8_t((x^y)&255);p[3]=255;}
    bool ok=true;double peak=0,changed=0;std::vector<float> baseline;
    unsigned generated=0;
    for(unsigned n=0;n<12&&ok;++n){
        if(n==6){engine::EnhancementSettings s;s.nr=gd.enableNr;s.sr=gd.enableSr;s.multiplier=gd.enableFg?gd.fgMultiplier:1;s.videoSrQuality=gd.videoSrQuality;s.videoHdr=gd.videoHdr;s.videoHdr.peakNits=400;ok=graph.applySettings(s);}
        if(argc>5&&n&&media.read(packet,&decodedFrame)!=source::SourceReadStatus::Frame){ok=false;break;}
        EnhanceGraph::FrameOutputs out;ok=ok&&graph.process(decodedFrame?decodedFrame:frame,n*1000.0/30,n==0||n==8,out,n+1);
        if(!ok)break;
        std::vector<float> data;ok=pixels(ctx,ring,graph.videoFrameResource(out.videoSlot),data);
        if(!ok)break;
        ok=std::all_of(data.begin(),data.end(),[](float v){return std::isfinite(v)&&v>=-.5f&&v<100;});
        peak=*std::max_element(data.begin(),data.end())*80;
        ok=ok&&peak>100&&peak<2100;
        if(n==5)baseline=data;
        if(n==11){for(size_t i=0;i<data.size();++i)changed+=std::abs(data[i]-baseline[i]);changed/=data.size();
            ok=ok&&changed>.001&&sink::saveHdrScreenshot(L"video-hdr.jxr",ctx,ring,graph.videoFrameResource(out.videoSlot));}
        if(gd.enableFg){ok=ok&&graph.resolveGeneration(out);if(out.hasGenerated)++generated;}
    }
    ok=ok&&(!gd.enableFg||generated>0);
    std::cout<<"VIDEO_HDR pass="<<ok<<" peakNits="<<peak<<" parameterChange="<<changed<<" generatedBatches="<<generated<<" nr="<<graph.metrics().nrEvaluateCount<<" sr="<<graph.metrics().srEvaluateCount<<std::endl;
    ring.drainQueue();graph.shutdown();av_frame_free(&frame);ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:5;
}
