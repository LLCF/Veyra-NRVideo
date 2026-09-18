#include "veyra/engine/VideoPresenter.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/sink/ImageExportSink.h"
#include <d3d12sdklayers.h>
#include <filesystem>
#include <iostream>
#include <thread>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

int wmain(int argc,wchar_t** argv){
    using namespace veyra;
    if(argc!=2||FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    std::filesystem::create_directories(argv[1]);
    HWND window=CreateWindowExW(0,L"STATIC",L"FG presentation regression",WS_POPUP,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status=Status::Ok;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    bool ok=window&&ctx.initialize(device,status)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status);
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;
    if(ok)ok=SUCCEEDED(ctx.device()->QueryInterface(IID_PPV_ARGS(&info)));
    pipeline::EnhanceGraph graph(ctx,ring);engine::VideoPresenter presenter;
    AVFrame* frame=av_frame_alloc();
    if(frame){frame->format=AV_PIX_FMT_RGBA;frame->width=640;frame->height=360;frame->color_range=AVCOL_RANGE_JPEG;frame->color_trc=AVCOL_TRC_IEC61966_2_1;}
    ok=ok&&frame&&av_frame_get_buffer(frame,32)>=0;
    unsigned cycle=0;
    for(unsigned multiplier:{4u,6u,4u}){
        if(!ok)break;
        ++cycle;
        pipeline::EnhanceGraphDesc desc;desc.sourceWidth=desc.workWidth=640;desc.sourceHeight=desc.workHeight=360;
        desc.enableNr=desc.enableSr=false;desc.enableFg=desc.rgbInput=true;desc.fgMultiplier=multiplier;
        desc.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
        ok=graph.initialize(desc)&&presenter.open(ctx,window,graph)&&graph.createViews();
        if(ok&&cycle==1){
            // A producer allocator stall must not be reported as private
            // presentation-ring service. Release the GPU wait from the CPU.
            Microsoft::WRL::ComPtr<ID3D12Fence> blocker;
            ok=SUCCEEDED(ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&blocker)));
            unsigned slot=0;
            if(ok)ok=ring.acquireNext(slot,status)!=nullptr;
            if(ok)ok=SUCCEEDED(ctx.directQueue()->Wait(blocker.Get(),1));
            if(ok){
                const auto sharedBefore=ring.cpuWaitMilliseconds(),presentBefore=presenter.cpuWaitMilliseconds(ring);
                HRESULT released=E_FAIL;
                std::jthread release([&]{Sleep(40);released=blocker->Signal(1);});
                ok=ring.submitAndSignal(slot)&&ring.acquire(slot,status)!=nullptr;
                release.join();ok=SUCCEEDED(released)&&ring.discardRecording()&&ok;
                const auto sharedWait=ring.cpuWaitMilliseconds()-sharedBefore;
                const auto presentWait=presenter.cpuWaitMilliseconds(ring)-presentBefore;
                ok=ok&&sharedWait>=20&&presentWait==0;
                std::cout<<"WAIT_ACCOUNTING sharedMs="<<sharedWait<<" privateMs="<<presentWait<<" pass="<<ok<<std::endl;
            }
        }
        Microsoft::WRL::ComPtr<ID3D12Fence> consumer;
        if(ok)ok=SUCCEEDED(ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&consumer)));
        if(ok){
            graph.presentationSubmitted(0,consumer.Get(),1);
            ok=!graph.nextFrameSlotAvailable();
            ok=SUCCEEDED(consumer->Signal(1))&&ok&&graph.nextFrameSlotAvailable();
        }
        unsigned generated=0;int maxError=0;
        for(unsigned i=0;ok&&i<40;++i){
            if(i==8||i==24){
                SetWindowPos(window,nullptr,0,0,i==8?320:640,i==8?180:360,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
                Sleep(110);
            }
            for(unsigned y=0;y<360;++y)for(unsigned x=0;x<640;++x){
                auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
                p[0]=p[1]=p[2]=uint8_t(24+(((x+i*8)/32+y/32)%2)*180);p[3]=255;
            }
            pipeline::EnhanceGraph::FrameOutputs out;
            ok=graph.process(frame,i*20.0,i==0||i==4,out,i+1)&&ring.waitIdle()&&graph.resolveGeneration(out);
            for(unsigned j=0;ok&&j<out.batch.count;++j){
                const auto& item=out.batch.frames[j];const bool interpolated=item.kind==pipeline::FrameKind::Generated;
                if(interpolated&&item.validity!=pipeline::GenerationValidity::Valid){ok=false;break;}
                ok=presenter.present(ctx,ring,graph,item.lease->slot,interpolated,false,0,false,.5f,item.identity);
                sink::RgbaImage shown;
                // First verify exact pixels; then exercise queued GPU reads,
                // resize and parity reuse without a readback draining each blit.
                if(i<8){
                    sink::RgbaImage expected;
                    ok=ok&&presenter.readPresentedFrameForTest(ctx,ring,shown)&&sink::readRgba8(ctx,ring,item.lease->texture.Get(),expected);
                    ok=ok&&shown.width==expected.width&&shown.height==expected.height&&shown.pixels.size()==expected.pixels.size();
                    if(ok){
                        for(size_t p=0;p<shown.pixels.size();++p)if(p%4!=3)maxError=std::max(maxError,std::abs(int(shown.pixels[p])-int(expected.pixels[p])));
                        ok=maxError<=2;
                    }
                }
                if(interpolated){
                    ++generated;
                    if(generated==1&&ok)ok=sink::saveImage((std::filesystem::path(argv[1])/(std::to_wstring(cycle)+L"-"+std::to_wstring(multiplier)+L"x-generated.png")).wstring(),shown);
                }
            }
        }
        ok=ok&&generated==38*(multiplier-1);
        // Also exercise producer teardown first: it must cover consumer GPU reads.
        graph.shutdown();presenter.close();
        std::cout<<"PRESENTATION multiplier="<<multiplier<<" generated="<<generated<<" maxPixelError="<<maxError<<" pass="<<ok<<std::endl;
    }
    graph.shutdown();presenter.close();ring.drainQueue();
    unsigned errors=0;
    if(info)for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
        SIZE_T bytes=0;info->GetMessage(i,nullptr,&bytes);std::vector<uint8_t> data(bytes);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(SUCCEEDED(info->GetMessage(i,message,&bytes))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<message->pDescription<<'\n';}
    }
    std::cout<<"D3D12 errors="<<errors<<std::endl;
    av_frame_free(&frame);ring.shutdown();ctx.shutdown();DestroyWindow(window);CoUninitialize();return ok&&!errors?0:1;
}
