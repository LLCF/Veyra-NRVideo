#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/VideoEncoder.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

int main(int argc,char** argv){
    using namespace veyra;
    if(argc<2)return 2;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;pipeline::EnhanceGraph graph(ctx,ring);
    Status status=Status::Ok;gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    bool ok=ctx.initialize(device,status)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status);
    pipeline::EnhanceGraphDesc desc;desc.sourceWidth=desc.workWidth=640;desc.sourceHeight=desc.workHeight=360;
    desc.enableNr=desc.enableSr=false;desc.enableFg=true;desc.fgMultiplier=6;desc.rgbInput=true;
    desc.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    ok=ok&&graph.initialize(desc)&&graph.createViews();
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);
    sink::EncoderConfig config;config.fpsNum=60;config.fpsDen=1;config.hevc=argc>2;
    std::wstring detail;std::unique_ptr<sink::VideoEncoder> encoder;
    if(ok)encoder=sink::openVideoEncoder(ctx,ring,graph,config,[&](const uint8_t* data,size_t size,int64_t,bool){file.write(reinterpret_cast<const char*>(data),size);return bool(file);},detail);
    ok=ok&&bool(encoder)&&bool(file);
    if(!ok)std::wcerr<<detail<<'\n';
    if(ok){const auto headers=encoder->headers();file.write(reinterpret_cast<const char*>(headers.data()),headers.size());}
    auto upload=ok?pipeline::makeUploadBuffer(ctx.device(),640*360*4):nullptr;
    ok=ok&&bool(upload);
    for(unsigned i=0;ok&&i<pipeline::kOutputPoolSlots;++i){
        void* data=nullptr;ok=SUCCEEDED(upload->Map(0,nullptr,&data));if(!ok)break;
        const uint8_t gray=uint8_t(20+i*18);
        auto* pixels=static_cast<uint8_t*>(data);
        for(size_t n=0;n<640*360;++n){pixels[n*4]=pixels[n*4+1]=pixels[n*4+2]=gray;pixels[n*4+3]=255;}
        upload->Unmap(0,nullptr);
        auto* target=i<2?graph.videoFrameResource(i):graph.generatedFrameResource(i-2);
        unsigned slot=0;auto* list=ring.acquireNext(slot,status);if(!list||!target){ok=false;break;}
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={target,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST};list->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,640,360,1,640*4};dst.pResource=target;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
        ok=ring.submitAndSignal(slot)&&ring.waitIdle()&&encoder->encode(i<2?i:i-2,i>=2,i)&&ring.waitIdle();
    }
    if(encoder){ok=encoder->finish()&&ok;encoder->close();encoder.reset();}file.close();
    ring.drainQueue();graph.shutdown();ring.shutdown();ctx.shutdown();
    AVFormatContext* format=nullptr;AVCodecContext* decoder=nullptr;AVPacket* packet=av_packet_alloc();AVFrame* frame=av_frame_alloc();
    unsigned decoded=0;
    if(ok)ok=avformat_open_input(&format,argv[1],nullptr,nullptr)>=0&&avformat_find_stream_info(format,nullptr)>=0;
    int video=-1;
    if(ok){video=av_find_best_stream(format,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);ok=video>=0;}
    if(ok){auto* parameters=format->streams[video]->codecpar;decoder=avcodec_alloc_context3(avcodec_find_decoder(parameters->codec_id));ok=decoder&&avcodec_parameters_to_context(decoder,parameters)>=0&&avcodec_open2(decoder,decoder->codec,nullptr)>=0;}
    auto receive=[&]{
        int result=0;
        while((result=avcodec_receive_frame(decoder,frame))>=0){
            const double expected=16+(20+decoded*18)*219.0/255;
            double sum=0;for(unsigned y=20;y<340;++y)for(unsigned x=20;x<620;++x)sum+=frame->data[0][size_t(y)*frame->linesize[0]+x];
            const double mean=sum/(320*600);
            std::cout<<"slot="<<decoded<<" expectedY="<<expected<<" decodedY="<<mean<<'\n';
            ok=ok&&decoded<pipeline::kOutputPoolSlots&&std::abs(mean-expected)<3;++decoded;av_frame_unref(frame);
        }
        if(result!=AVERROR(EAGAIN)&&result!=AVERROR_EOF)ok=false;
    };
    while(ok&&av_read_frame(format,packet)>=0){if(packet->stream_index==video){ok=avcodec_send_packet(decoder,packet)>=0;if(ok)receive();}av_packet_unref(packet);}
    if(ok){ok=avcodec_send_packet(decoder,nullptr)>=0;if(ok)receive();}
    ok=ok&&decoded==pipeline::kOutputPoolSlots;
    std::cout<<"EXPORT_POOL decoded="<<decoded<<" passed="<<ok<<'\n';
    av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&decoder);avformat_close_input(&format);CoUninitialize();return ok?0:1;
}
