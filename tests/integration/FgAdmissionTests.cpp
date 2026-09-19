#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/engine/DeadlineWait.h"
#include "veyra/engine/LiveFgAdmission.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include <d3d12sdklayers.h>
#include <filesystem>
#include <iostream>

int wmain(int argc,wchar_t** argv){
    using namespace veyra;
    if(argc!=5)return 2;
    if(std::wstring(argv[1])!=L"dlss")return 2;const unsigned multiplier=unsigned(std::stoi(argv[2]));
    if(multiplier<2||multiplier>6)return 2;
    std::filesystem::create_directories(argv[4]);Logger::instance().openFile((std::filesystem::path(argv[4])/"engine.log").wstring());Logger::instance().setConsoleEnabled(false);
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    bool ok=ctx.initialize(device,status)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status);
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;if(ok)ctx.device()->QueryInterface(IID_PPV_ARGS(&info));
    source::MediaFileSource source;source::SourceOpenDesc input;input.path=argv[3];input.preferHardwareDecode=false;
    ok=ok&&source.open(input);
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc desc;
    desc.sourceWidth=desc.workWidth=source.info().width;desc.sourceHeight=desc.workHeight=source.info().height;
    desc.enableNr=true;desc.enableFg=true;desc.fgMultiplier=multiplier;
    desc.frameGenerationBackend=engine::FrameGenerationBackend::Dlss;
    desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    ok=ok&&graph.initialize(desc)&&graph.createViews();
    unsigned skipped=0,evaluated=0,resetSkipped=0,recoveries=0,generated=0,real=0;
    engine::DeadlineWait wait;
    for(unsigned i=0;ok&&i<40;++i){
        pipeline::FramePacket packet;const AVFrame* frame=nullptr;
        ok=source.read(packet,&frame)==source::SourceReadStatus::Frame;if(!ok)break;
        pipeline::EnhanceGraph::FrameOutputs out;
        const bool reject=(i>=8&&i<12)||(i>=24&&i<28);
        bool observedWarmup=false;
        auto admission=[&](const auto&,bool warmingHistory){
            observedWarmup=warmingHistory;
            return engine::admitLiveFg(reject?2000000:900000,1000000,0,std::nullopt,0);
        };
        ok=graph.process(frame,packet.pts.toDouble()*1000,i==0||i==20,out,packet.sequence,&packet.colorInfo,nullptr,false,admission);
        ok=ok&&(observedWarmup==(i==0||i==9||i==10||i==11||i==12||i==20||i==25||i==26||i==27||i==28));
        const auto start=std::chrono::steady_clock::now();
        while(ok&&!graph.resolveGeneration(out)){
            if(std::chrono::steady_clock::now()-start>std::chrono::seconds(2)){ok=false;break;}wait.slice(.2);
        }
        if(!ok)break;
        skipped+=out.fgSkippedBeforeEval;evaluated+=out.fgEvaluated;resetSkipped+=out.fgSkippedForReset;recoveries+=out.fgRecovery;
        ok=ok&&out.fgCandidates==multiplier-1&&out.fgCandidates==out.fgSkippedBeforeEval+out.fgEvaluated+out.fgSkippedForReset;
        if(reject)ok=ok&&out.fgEvaluated==0&&out.batch.count==1&&!out.hasGenerated;
        if(i==12||i==28)ok=ok&&out.fgRecovery&&out.batch.count==1;
        if(i==0||i==12||i==20||i==28)ok=ok&&out.fgEvaluated==1&&out.fgSkippedForReset==multiplier-2;
        if(i==1||i==13||i==21||i==29){
            ok=ok&&out.batch.count==multiplier&&out.hasGenerated;
            for(unsigned j=0;j<out.batch.count;++j)if(out.batch.frames[j].kind==pipeline::FrameKind::Generated)
                ok=ok&&out.batch.frames[j].validity==pipeline::GenerationValidity::Valid;
        }
        for(unsigned j=0;j<out.batch.count;++j){const auto& f=out.batch.frames[j];
            if(f.kind==pipeline::FrameKind::Real){++real;ok=ok&&f.pts100ns==packet.pts.to100ns();}
            else if(f.validity==pipeline::GenerationValidity::Valid){++generated;ok=ok&&f.pts100ns>out.batch.a100ns&&f.pts100ns<out.batch.b100ns;}
        }
        if(i==10||i==30){sink::RgbaImage image;ok=ok&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
            unsigned nonblack=0;for(size_t p=0;p+3<image.pixels.size();p+=4)nonblack+=image.pixels[p]>8||image.pixels[p+1]>8||image.pixels[p+2]>8;
            ok=ok&&nonblack>image.width*image.height/20;
        }
        if(i==13||i==21||i==29)for(unsigned j=0;j<out.batch.count;++j){
            const auto& f=out.batch.frames[j];if(f.kind!=pipeline::FrameKind::Generated)continue;
            sink::RgbaImage image;ok=ok&&sink::readRgba8(ctx,ring,f.lease->texture.Get(),image);
            unsigned nonblack=0;for(size_t p=0;p+3<image.pixels.size();p+=4)nonblack+=image.pixels[p]>8||image.pixels[p+1]>8||image.pixels[p+2]>8;
            ok=ok&&nonblack>image.width*image.height/20;
        }
        std::cout<<"frame="<<i<<" skip="<<out.fgSkippedBeforeEval<<" evaluate="<<out.fgEvaluated<<" recovery="<<out.fgRecovery<<" batch="<<out.batch.count<<" pass="<<ok<<std::endl;
    }
    const auto nr=graph.metrics().nrEvaluateCount;
    ok=ok&&real==40&&nr==40&&skipped==8*(multiplier-1)&&resetSkipped==4*(multiplier-2)&&evaluated==28*(multiplier-1)+4&&recoveries==2&&generated>0;
    ring.drainQueue();graph.shutdown();source.close();
    unsigned errors=0;if(info)for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<uint8_t> data(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());if(SUCCEEDED(info->GetMessage(i,m,&size))&&m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;log::error("debug",m->pDescription);}}
    ok=ok&&errors==0;
    std::cout<<"FG_ADMISSION backend="<<"DLSS"<<" multiplier="<<multiplier<<" real="<<real<<" NR="<<nr<<" skipped="<<skipped<<" evaluated="<<evaluated<<" recoveries="<<recoveries<<" valid="<<generated<<" debugErrors="<<errors<<" pass="<<ok<<std::endl;
    info.Reset();ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:1;
}
