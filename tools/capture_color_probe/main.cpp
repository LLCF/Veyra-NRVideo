// Explicit diagnostic readback. Never used by normal capture/playback.
#include "veyra/source/CaptureCardSource.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/Log.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}
using namespace veyra;
int wmain(int argc,wchar_t**argv){
    if(argc<3){std::cerr<<"capture:device:format:audio output-directory [--legacy-rgb]\n";return 2;}
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const std::filesystem::path dir(argv[2]);std::filesystem::create_directories(dir);
    Logger::instance().openFile((dir/"probe.log").wstring());
    source::SourceOpenDesc desc;desc.path=argv[1];desc.legacyCaptureRgbForDiagnostic=argc>3&&wcscmp(argv[3],L"--legacy-rgb")==0;
    source::CaptureCardSource source;if(!source.configure(desc))return 3;
    const auto info=source.info();
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc dd;
    if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return 4;
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
    gd.sourceWidth=gd.workWidth=info.width;gd.sourceHeight=gd.workHeight=info.height;
    gd.rgbInput=info.color.pixelFormat==pipeline::SourcePixelFormat::Bgra8;
    gd.yuy2Input=info.color.pixelFormat==pipeline::SourcePixelFormat::Yuy2;
    gd.packedInput=pipeline::packedInputCode(info.color.pixelFormat);
    gd.captureBitDepth=info.color.pixelFormat==pipeline::SourcePixelFormat::P010?10:info.color.pixelFormat==pipeline::SourcePixelFormat::P016?16:8;
    gd.enableNr=gd.enableFg=false;gd.noFeatures=true;
    const HWND window=CreateWindowExW(0,L"STATIC",L"Veyra color diagnostic",WS_POPUP,0,0,info.width,info.height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    engine::VideoPresenter presenter;
    if(!window||!graph.initialize(gd)||!presenter.open(ctx,window,graph)||!graph.createViews()||!source.start())return 5;
    const AVFrame* frame=nullptr;pipeline::FramePacket packet;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    unsigned frames=0;while(std::chrono::steady_clock::now()<deadline&&frames<20){
        const auto result=source.read(packet,&frame);
        if(result==source::SourceReadStatus::Error)return 6;
        if(result==source::SourceReadStatus::Frame)++frames;
    }
    if(frames<20||!frame)return 7;
    std::cout<<"frame ptr="<<static_cast<const void*>(frame)<<" format="<<frame->format<<" "<<frame->width<<"x"<<frame->height
        <<" data0="<<static_cast<const void*>(frame->data[0])<<" data1="<<static_cast<const void*>(frame->data[1])
        <<" linesize0="<<frame->linesize[0]<<" linesize1="<<frame->linesize[1]<<std::endl;
    const auto pixelFormat=AVPixelFormat(frame->format);
    const unsigned rowBytes=unsigned(av_image_get_linesize(pixelFormat,int(info.width),0));
    std::cout<<"rowBytes="<<rowBytes<<std::endl;
    std::ofstream raw(dir/"source.raw",std::ios::binary);
    const auto* pixelDesc=av_pix_fmt_desc_get(pixelFormat);
    std::cout<<"planes="<<av_pix_fmt_count_planes(pixelFormat)<<std::endl;
    for(int plane=0;plane<av_pix_fmt_count_planes(pixelFormat);++plane){
        // AV_CEIL_RSHIFT on an unsigned operand wraps through unsigned negation
        // (1080 -> 2147484188 rows and a buffer overrun); keep the shift on a
        // signed value like the FFmpeg macro expects.
        const unsigned rows=plane?unsigned(AV_CEIL_RSHIFT(int(info.height),int(pixelDesc->log2_chroma_h))):info.height;
        const int bytes=av_image_get_linesize(pixelFormat,int(info.width),plane);
        std::cout<<"plane="<<plane<<" rows="<<rows<<" bytes="<<bytes<<" src="<<static_cast<const void*>(frame->data[plane])<<" stride="<<frame->linesize[plane]<<std::endl;
        for(unsigned y=0;y<rows;++y)raw.write(reinterpret_cast<const char*>(frame->data[plane]+ptrdiff_t(y)*frame->linesize[plane]),bytes);
    }
    raw.close();
    std::cout<<"raw written"<<std::endl;
    pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage gpu,display;
    // Split the chain so a crash reports the exact stage instead of dying
    // silently inside one boolean expression.
    bool ok=graph.process(frame,packet.pts.toDouble()*1000,true,out,packet.sequence,&packet.colorInfo);
    std::cout<<"stage process="<<ok<<" slot="<<out.videoSlot<<std::endl;
    if(ok){ok=sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),gpu);std::cout<<"stage readRgba8="<<ok<<" bytes="<<gpu.pixels.size()<<std::endl;}
    if(ok){ok=sink::saveImage((dir/"gpu.png").wstring(),gpu);std::cout<<"stage saveGpu="<<ok<<std::endl;}
    if(ok){ok=presenter.present(ctx,ring,graph,out.videoSlot,false);std::cout<<"stage present="<<ok<<std::endl;}
    if(ok){ok=presenter.readPresentedFrameForTest(ctx,ring,display);std::cout<<"stage readPresented="<<ok<<std::endl;}
    if(ok){ok=sink::saveImage((dir/"present.png").wstring(),display);std::cout<<"stage savePresent="<<ok<<std::endl;}
    int error=-1;if(ok&&gpu.pixels.size()==display.pixels.size()){error=0;for(size_t i=0;i<gpu.pixels.size();++i)error=std::max(error,std::abs(int(gpu.pixels[i])-int(display.pixels[i])));}
    const auto metrics=source.metrics();
    std::ofstream report(dir/"result.json");report<<"{\"success\":"<<(ok?"true":"false")<<",\"width\":"<<info.width<<",\"height\":"<<info.height<<",\"avPixelFormat\":"<<frame->format<<",\"rowBytes\":"<<rowBytes<<",\"matrix\":"<<int(info.color.matrix)<<",\"range\":"<<int(info.color.range)<<",\"transfer\":"<<int(info.color.transfer)<<",\"presentMaxError8\":"<<error<<",\"callbackFps\":"<<metrics.callbackFps<<",\"received\":"<<metrics.received<<",\"nrEvaluations\":"<<graph.metrics().nrEvaluateCount<<"}\n";report.close();
    std::cout<<"CAPTURE_COLOR success="<<ok<<" presentMaxError8="<<error<<" format="<<frame->format<<" size="<<info.width<<"x"<<info.height<<" fps="<<metrics.callbackFps<<"\n";
    source.close();out={};ring.drainQueue();presenter.close();graph.shutdown();ring.shutdown();ctx.shutdown();DestroyWindow(window);CoUninitialize();return ok?0:8;
}
