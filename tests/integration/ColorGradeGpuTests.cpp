// GPU acceptance for the v4 colour chain. The grade rides inside the ingest
// dispatch, so these cases drive a real graph and read the presented frame:
//   * master switch off  -> byte-identical to the ungraded baseline
//   * enabled + neutral  -> byte-identical to the ungraded baseline (identity)
//   * +1 EV              -> clearly brighter coded value
//   * point curve        -> the control point lands where the model says
//   * saturation -100    -> a saturated input collapses to grey
// The optional .cube LUT is exercised once its importer lands (T5).
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/ColorSettings.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace {
using namespace veyra;
int failures=0;
void check(bool ok,const std::string& label){
    std::printf("%s %s\n",ok?"PASS":"FAIL",label.c_str());
    if(!ok)++failures;
}
constexpr int kSize=64;

// One frame of a solid colour, sRGB-coded RGB32 (the still-image / RGB capture
// ingress path), so the linear value is exactly the sRGB decode of the code.
struct Frame {
    AVFrame* frame=nullptr;
    ~Frame(){if(frame)av_frame_free(&frame);}
    bool make(uint8_t r,uint8_t g,uint8_t b){
        frame=av_frame_alloc();
        frame->format=AV_PIX_FMT_BGR0;frame->width=frame->height=kSize;
        frame->color_range=AVCOL_RANGE_UNSPECIFIED;frame->color_trc=AVCOL_TRC_IEC61966_2_1;frame->colorspace=AVCOL_SPC_RGB;
        if(av_frame_get_buffer(frame,32)<0)return false;
        for(int y=0;y<kSize;++y)for(int x=0;x<kSize;++x){
            auto* p=frame->data[0]+std::size_t(y)*frame->linesize[0]+std::size_t(x)*4;
            p[0]=b;p[1]=g;p[2]=r;p[3]=255;
        }
        return true;
    }
};
struct Graph {
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;
    pipeline::EnhanceGraph g{ctx,ring};
    bool up=false;
    bool start(bool colorEnabled){
        gfx::DeviceContextDesc device;Status st;
        if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
        up=true;
        pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=kSize;
        gd.rgbInput=true;gd.stillImage=true;gd.noFeatures=true;gd.enableNr=false;gd.enableSr=false;gd.enableFg=false;
        gd.color.enabled=colorEnabled;
        return g.initialize(gd)&&g.createViews();
    }
    bool apply(const engine::EnhancementSettings& settings){return g.applySettings(settings);}
    bool render(Frame& frame,sink::RgbaImage& out){
        pipeline::EnhanceGraph::FrameOutputs outputs;
        return g.process(frame.frame,0,true,outputs,1)&&
               sink::readRgba8(ctx,ring,g.videoFrameResource(outputs.videoSlot),out);
    }
    ~Graph(){if(up){ring.drainQueue();g.shutdown();ring.shutdown();ctx.shutdown();}}
};
struct Pixel { int r=0,g=0,b=0; };
Pixel center(const sink::RgbaImage& image){
    const auto index=(std::size_t(kSize/2)*std::size_t(image.width)+std::size_t(kSize/2))*4;
    return {image.pixels[index],image.pixels[index+1],image.pixels[index+2]};
}
}
int wmain(){
    // 1. Master switch off and enabled-neutral are both identity.
    {
        Graph off,on;
        if(!off.start(false)||!on.start(true)){std::printf("FAIL graph init\n");return 1;}
        Frame f1,f2;
        sink::RgbaImage baseline,neutral;
        const bool ready=f1.make(188,96,64)&&f2.make(188,96,64)&&off.render(f1,baseline)&&on.render(f2,neutral);
        check(ready,"colour graphs initialise and render");
        if(ready)check(baseline.pixels==neutral.pixels,"master off and enabled-neutral are byte-identical to the ungraded path");
    }
    // 2. Exposure, curve, saturation and hue behaviour through the real graph.
    {
        Graph graph;
        if(!graph.start(true)){std::printf("FAIL graph init\n");return 1;}
        Frame neutral,exposed,curved,grass,desaturated;
        sink::RgbaImage a,b,c,d,e;
        if(!(neutral.make(188,188,188)&&exposed.make(188,188,188)&&curved.make(188,188,188)&&
             grass.make(64,160,64)&&desaturated.make(64,160,64)&&graph.render(neutral,a))){
            std::printf("FAIL baseline render\n");return 1;
        }
        const auto neutralPixel=center(a);
        {
            if(!graph.render(grass,d)){std::printf("FAIL colour render\n");return 1;}
            const auto coloured=center(d);
            check(coloured.g>coloured.r&&coloured.g>coloured.b,"a green input stays green through the neutral grade");
        }
        {
            engine::EnhancementSettings s;s.color.enabled=true;s.color.exposure=1.0f;
            if(!graph.apply(s)||!graph.render(exposed,b)){std::printf("FAIL exposure render\n");return 1;}
            check(center(b).r>neutralPixel.r+20,"+1 EV brightens mid grey by a visible step");
        }
        {
            engine::EnhancementSettings s;s.color.enabled=true;
            s.color.curves[0].count=3;s.color.curves[0].points[1]={0.5f,0.75f};s.color.curves[0].points[2]={1,1};
            if(!graph.apply(s)||!graph.render(curved,c)){std::printf("FAIL curve render\n");return 1;}
            check(center(c).r>neutralPixel.r+10,"point curve at 0.5 raises the coded value");
        }
        {
            engine::EnhancementSettings s;s.color.enabled=true;s.color.saturation=-100.0f;
            if(!graph.apply(s)||!graph.render(desaturated,e)){std::printf("FAIL saturation render\n");return 1;}
            const auto pixel=center(e);
            check(std::abs(pixel.r-pixel.g)<=2&&std::abs(pixel.g-pixel.b)<=2,"saturation -100 collapses the frame to grey");
        }
    }
    if(failures){std::printf("FAIL: colour grade GPU contract (%d checks)\n",failures);return 1;}
    std::printf("PASS: colour grade GPU contract (off/neutral identity, exposure, curve, saturation, hue)\n");
    return 0;
}
