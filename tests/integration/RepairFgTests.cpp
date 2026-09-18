// Synthetic pixels only. Diagnostic readback is intentionally outside all
// player/capture/export paths. The actual product EnhanceGraph is under test.
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include <cmath>
#include <filesystem>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace {
uint64_t hash(const veyra::sink::RgbaImage& im){uint64_t v=14695981039346656037ull;for(auto b:im.pixels){v^=b;v*=1099511628211ull;}return v;}
double latticeValue(unsigned x,unsigned y){
    uint32_t value=x*0x9e3779b9u+y*0x85ebca6bu;
    value^=value>>16;value*=0x7feb352du;value^=value>>15;
    return 32.0+double(value%193);
}
uint8_t detailedPlane(unsigned x,unsigned y,unsigned frame){
    // A translated, nonperiodic bilinear texture avoids the sine fixture's
    // repeated low-frequency structure without changing the position gate.
    const unsigned local=x+256-frame*16;
    const unsigned cellX=local/8,cellY=y/8;
    const double tx=double(local%8)/8,ty=double(y%8)/8;
    const double a=latticeValue(cellX,cellY)*(1-tx)+latticeValue(cellX+1,cellY)*tx;
    const double b=latticeValue(cellX,cellY+1)*(1-tx)+latticeValue(cellX+1,cellY+1)*tx;
    return uint8_t(std::lround(a*(1-ty)+b*ty));
}
double center(const veyra::sink::RgbaImage& im){double total=0,sum=0;for(unsigned y=300;y<780;++y)for(unsigned x=0;x<im.width;++x){const auto v=im.pixels[(size_t(y)*im.width+x)*4];if(v>160){total+=1;sum+=x;}}return total?sum/total:-1;}
double translation(const veyra::sink::RgbaImage& previous,const veyra::sink::RgbaImage& generated){
    double best=1e30,shift=0;
    for(unsigned quarter=0;quarter<=64;++quarter){
        const double dx=quarter*0.25;double error=0;
        for(unsigned y=200;y<880;y+=8)for(unsigned x=300;x<1620;x+=8){
            const double sample=x-dx;const unsigned left=unsigned(sample);const double fraction=sample-left;
            const size_t offset=(size_t(y)*previous.width+left)*4;
            const double expected=previous.pixels[offset]*(1-fraction)+previous.pixels[offset+4]*fraction;
            const double delta=expected-generated.pixels[(size_t(y)*generated.width+x)*4];error+=delta*delta;
        }
        if(error<best){best=error;shift=dx;}
    }
    return shift;
}
}
int main(int argc,char** argv){
    using namespace veyra;CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;pipeline::EnhanceGraph graph(ctx,ring);
    Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;bool ok=ctx.initialize(dd,st)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st);
    pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=1920;gd.sourceHeight=gd.workHeight=1080;gd.enableNr=false;gd.enableSr=false;gd.enableFg=true;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    const std::string mode=argc>1?argv[1]:"off";
    gd.rgbInput=mode.find("-rgb")!=std::string::npos;
    gd.enableNr=mode!="off";gd.enableNvofStandalone=gd.enableNr;
    if(mode.starts_with("dis"))gd.opticalFlowBackend=engine::OpticalFlowBackend::GpuDis;
    if(mode=="flowP")gd.flowQuality=engine::FlowQuality::Performance;
    if(mode=="flowQ")gd.flowQuality=engine::FlowQuality::Quality;
    if(mode=="dis-sr"){gd.workWidth=3840;gd.workHeight=2160;gd.nrWidth=1920;gd.nrHeight=1080;gd.enableSr=true;gd.videoSrQuality=2;}
    if(mode=="sr"){gd.workWidth=3840;gd.workHeight=2160;gd.nrWidth=1920;gd.nrHeight=1080;gd.enableSr=true;}
    const bool textured=mode.ends_with("-textured");
    const bool exact=mode.find("-exact")!=std::string::npos;
    const bool globalMotion=mode.find("-pan")!=std::string::npos;
    const bool planar=mode.find("-planar")!=std::string::npos;
    const bool detailed=mode.find("-detail")!=std::string::npos;
    const uint16_t motionBits=mode.find("-reverse")!=std::string::npos?0x4c00:mode.find("-zero")!=std::string::npos?0:0xcc00;
    pipeline::ComPtr<ID3D12Resource> motion,upload;
    if(ok&&exact){motion=pipeline::makeTexture(ctx.device(),1920,1080,DXGI_FORMAT_R16G16_FLOAT,false);upload=pipeline::makeUploadBuffer(ctx.device(),1920ull*1080*4);ok=motion&&upload;gd.fgMotionProbe=motion.Get();}
    if(mode.starts_with("mfg")&&mode.size()>=4&&mode[3]>='2'&&mode[3]<='6'){gd.fgMultiplier=uint32_t(mode[3]-'0');gd.enableNr=false;gd.enableNvofStandalone=false;}
    if(ok)ok=graph.initialize(gd)&&graph.createViews();
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_RGBA;f->width=1920;f->height=1080;f->color_range=AVCOL_RANGE_JPEG;ok=ok&&av_frame_get_buffer(f,32)>=0;
    sink::RgbaImage previous;unsigned generated=0,valid=0;pipeline::FrameBatch retained;
    for(unsigned i=0;ok&&i<12;++i){
        if(exact){
            void* data=nullptr;if(FAILED(upload->Map(0,nullptr,&data))){ok=false;break;}
            for(unsigned y=0;y<1080;++y)for(unsigned x=0;x<1920;++x){auto* p=static_cast<uint16_t*>(data)+(size_t(y)*1920+x)*2;const bool square=x>=240+i*16&&x<880+i*16&&y>=220&&y<860;p[0]=i&&(square||globalMotion||planar)?motionBits:0;p[1]=0;}
            upload->Unmap(0,nullptr);unsigned slot=0;auto* list=ring.acquireNext(slot,st);if(!list){ok=false;break;}
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={motion.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,i?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST};list->ResourceBarrier(1,&b);
            D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=motion.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint={DXGI_FORMAT_R16G16_FLOAT,1920,1080,1,1920*4};list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;list->ResourceBarrier(1,&b);
            if(!ring.submitAndSignal(slot)||!ring.drainQueue()){ok=false;break;}
        }
        for(int y=0;y<1080;++y)for(int x=0;x<1920;++x){auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;const int local=x-int(i)*16;const bool square=local>=240&&local<880&&y>=220&&y<860;const int texture=textured?((local*13+y*7)^((local/8)*19+(y/8)*23))&31:0;p[0]=p[1]=p[2]=square?240-texture:20;p[3]=255;}
        if(planar)for(int y=0;y<1080;++y)for(int x=0;x<1920;++x){
            auto* p=f->data[0]+size_t(y)*f->linesize[0]+x*4;const double local=x-int(i)*16;
            p[0]=p[1]=p[2]=uint8_t(std::lround(128+45*std::sin(local*0.07)+35*std::sin(local*0.031+y*0.06)+25*std::sin(y*0.031)));p[3]=255;
            if(detailed)p[0]=p[1]=p[2]=detailedPlane(unsigned(x),unsigned(y),i);
        }
        pipeline::EnhanceGraph::FrameOutputs out;if(!graph.process(f,i*20.0,i==0,out)){ok=false;break;}
        sink::RgbaImage real,g;if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),real)){ok=false;break;}
        if(!ring.waitIdle()||!graph.resolveGeneration(out)){ok=false;break;}
        uint64_t previousGeneratedHash=0;
        for(uint32_t j=0;j<out.batch.count;++j){const auto& item=out.batch.frames[j];if(item.kind!=pipeline::FrameKind::Generated)continue;
            ++generated;if(!sink::readRgba8(ctx,ring,graph.generatedFrameResource(item.lease->slot),g)){ok=false;break;}
            if(i==8&&exact)sink::saveImage((std::filesystem::path(VEYRA_PROJECT_ROOT)/"out/logs"/("post140-exact-"+std::to_string(item.subframe)+".png")).wstring(),g);
            const double t=double(item.subframe)/gd.fgMultiplier;
            double blendResidual=0;for(size_t k=0;k<g.pixels.size();k+=4)blendResidual+=std::abs(double(g.pixels[k])-(double(previous.pixels[k])*(1-t)+real.pixels[k]*t));blendResidual/=g.width*g.height;
            const double expected=center(previous)*(1-t)+center(real)*t;
            const double displacement=planar?translation(previous,g):0;
            const bool position=planar?std::abs(displacement-16*t)<=1.0:std::abs(center(g)-expected)<=3.0;
            const bool good=item.validity==pipeline::GenerationValidity::Valid&&hash(g)!=hash(previous)&&hash(g)!=hash(real)&&hash(g)!=previousGeneratedHash&&blendResidual>0.1&&position&&std::abs(item.pts100ns/10000.0-((i-1+t)*20.0))<=0.0001;
            valid+=good;
            previousGeneratedHash=hash(g);
            std::cout<<"CONTENT frame="<<i<<" subframe="<<item.subframe<<" pts="<<item.pts100ns<<" previousHash="<<hash(previous)<<" realHash="<<hash(real)<<" generatedHash="<<hash(g)<<" expectedCenter="<<expected<<" actualCenter="<<center(g)<<" blendResidual="<<blendResidual<<" valid="<<good<<std::endl;
            if(planar)std::cout<<"TRANSLATION expected="<<16*t<<" observed="<<displacement<<std::endl;
        }
        previous=std::move(real);
        if(i==10)retained=out.batch;
    }
    std::cout<<"RESULT source="<<graph.metrics().nvofExecuteCount+graph.metrics().gpuDisExecuteCount+1<<" generated="<<generated<<" contentValid="<<valid<<" display=unmeasured"<<std::endl;
    pipeline::EnhanceGraph::FrameOutputs rejected;
    const bool protectedLease=ok&&!graph.process(f,240,false,rejected);
    std::cout<<"LEASE retained-slot-overwrite-rejected="<<protectedLease<<std::endl;
    retained={};rejected={};
    ok=ok&&generated==11*(gd.fgMultiplier-1)&&valid==generated&&protectedLease;ring.drainQueue();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:1;
}
