#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/RuntimePaths.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/source/MediaFileSource.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <d3d12sdklayers.h>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra;

// This probe exercises the product graph. Readback is diagnostic only, at the
// end of each sequence; the intervening frames use the normal six-slot queue.
int wmain(int argc, wchar_t** argv) {
    if(argc<2){std::cerr<<"usage: fsr41_tests <absolute-provider|baseline|fsr3> [frames=90] [width=1280] [height=720] [output-width=1920] [output-height=1080]\n";return 1;}
    const bool baseline=std::wstring(argv[1])==L"baseline", fsr3=std::wstring(argv[1])==L"fsr3";
    SetEnvironmentVariableW(L"VEYRA_FSR41_PROVIDER",baseline||fsr3?nullptr:argv[1]);
    const unsigned frames=argc>2?unsigned(_wtoi(argv[2])):90;
    unsigned w=argc>3?unsigned(_wtoi(argv[3])):1280,h=argc>4?unsigned(_wtoi(argv[4])):720;
    const bool media=argc>7;
    source::MediaFileSource source;
    if(media){source::SourceOpenDesc od;od.path=argv[7];od.preferHardwareDecode=false;
        if(!source.open(od))return 2;w=source.info().width;h=source.info().height;}
    const unsigned ow=argc>5?unsigned(_wtoi(argv[5])):1920,oh=argc>6?unsigned(_wtoi(argv[6])):1080;
    if(frames<12||frames>30000||!w||!h||w>3840||h>2160||ow<w||oh<h||ow>3840||oh>2160)return 1;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const auto initBegin=std::chrono::steady_clock::now();
    gfx::D3D12DeviceContext ctx; gfx::CommandSlotRing ring; Status status; gfx::DeviceContextDesc dd;
    dd.enableDebugLayer=GetEnvironmentVariableW(L"VEYRA_FSR_TEST_DEBUG",nullptr,0)!=0;
    if(!ctx.initialize(dd,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
    gd.sourceWidth=w;gd.sourceHeight=h;gd.workWidth=gd.nrWidth=ow;gd.workHeight=gd.nrHeight=oh;
    gd.rgbInput=!media;gd.enableNr=gd.enableFg=false;gd.enableSr=!baseline;
    gd.enableNvofStandalone=true;gd.videoSrQuality=engine::kVideoSrFsr;
    gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    if(!graph.initialize(gd)||!graph.createViews()||(!baseline&&!graph.fsrSrEnabled()))return 3;
    const auto initSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-initBegin).count();
    uint64_t budget=0,initialMemory=0,finalMemory=0;
    ctx.videoMemoryInfo(budget,initialMemory);
    AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_BGRA;frame->width=int(w);frame->height=int(h);
    frame->color_range=AVCOL_RANGE_JPEG;frame->color_primaries=AVCOL_PRI_BT709;frame->color_trc=AVCOL_TRC_IEC61966_2_1;
    if(av_frame_get_buffer(frame,32)<0)return 4;
    const auto begin=std::chrono::steady_clock::now();
    bool ok=true;unsigned processed=0;pipeline::EnhanceGraph::FrameOutputs out;
    for(unsigned n=0;n<frames&&ok;++n){
        if(std::chrono::steady_clock::now()-begin>std::chrono::seconds(240)){ok=false;break;}
        if(media){
            pipeline::FramePacket packet;const AVFrame* decoded=nullptr;
            if(source.read(packet,&decoded)!=source::SourceReadStatus::Frame){ok=false;break;}
            ok=graph.process(decoded,packet.pts.toDouble()*1000,n%37==0,out,n+1,&packet.colorInfo);++processed;
            continue;
        }
        // A translated checker and gradient expose black output, stale history,
        // wrong transfer and shifted geometry; resets occur while work is queued.
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
            const unsigned xx=(x+n*2)%w;
            const uint8_t v=uint8_t((((xx/64+y/64)&1)?140:30)+xx*70/w);
            p[0]=v;p[1]=v;p[2]=v;p[3]=255;
        }
        ok=graph.process(frame,n*1000.0/30,n%37==0,out,n+1);++processed;
    }
    ok=ring.waitIdle()&&ok;
    sink::RgbaImage image;
    if(ok)ok=sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
    double mean=0,mae=0;unsigned lo=255,hi=0;
    if(ok){
        for(unsigned y=0;y<oh;++y)for(unsigned x=0;x<ow;++x){
            const auto value=image.pixels[(size_t(y)*ow+x)*4];mean+=value;lo=std::min(lo,unsigned(value));hi=std::max(hi,unsigned(value));
            const unsigned sx=std::min(w-1,unsigned((x+.5)*w/ow)),sy=std::min(h-1,unsigned((y+.5)*h/oh));
            if(!media)mae+=std::abs(int(value)-int(frame->data[0][size_t(sy)*frame->linesize[0]+sx*4]));
        }
        mean/=double(ow)*oh;mae/=double(ow)*oh;
        ok=media?(hi>lo):(mean>30&&mean<225&&hi-lo>50&&mae<30);
        sink::saveImage(L"fsr-frame.png",image);
    }
    const auto& times=ring.gpuCommandTimesMs();
    auto sorted=times;std::sort(sorted.begin(),sorted.end());
    double avg=0;for(auto t:times)avg+=t;if(!times.empty())avg/=times.size();
    const auto srCount=graph.metrics().srEvaluateCount;
    unsigned debugErrors=0;
    gfx::ComPtr<ID3D12InfoQueue> messages;
    if(SUCCEEDED(ctx.device()->QueryInterface(IID_PPV_ARGS(&messages)))){
        for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){
            SIZE_T size=0;messages->GetMessage(i,nullptr,&size);std::vector<uint8_t> storage(size);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if(SUCCEEDED(messages->GetMessage(i,message,&size))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){
                ++debugErrors;std::cerr<<"D3D12_ERROR "<<message->pDescription<<std::endl;
            }
        }
    }
    ctx.videoMemoryInfo(budget,finalMemory);
    ok=ok&&(baseline||srCount>frames/2)&&SUCCEEDED(ctx.device()->GetDeviceRemovedReason());
    ok=ok&&debugErrors==0;
    std::cout<<"FSR_VIDEO pass="<<ok<<" media="<<media<<" frames="<<processed<<" sr="<<srCount<<" mean="<<mean<<" min="<<lo<<" max="<<hi<<" referenceMAE="<<(media?-1:mae)
             <<" commandGpuAvgMs="<<avg<<" commandGpuP95Ms="<<(sorted.empty()?0:sorted[size_t((sorted.size()-1)*.95)])
             <<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()
             <<" initSeconds="<<initSeconds<<" initialMemoryMiB="<<initialMemory/1048576<<" finalMemoryMiB="<<finalMemory/1048576
             <<" debugEnabled="<<ctx.debugLayerEnabled()<<" debugErrors="<<debugErrors<<std::endl;
    ring.drainQueue();graph.shutdown();av_frame_free(&frame);ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:5;
}
