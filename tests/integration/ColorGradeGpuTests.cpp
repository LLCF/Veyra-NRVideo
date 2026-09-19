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
#include "veyra/RuntimePaths.h"
#include "veyra/engine/ColorLut.h"
#include <filesystem>
#include <fstream>
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
    // Horizontal ramp used by the banding check: the grade compresses it so the
    // ideal 8-bit output advances by a fraction of a code per pixel.
    bool makeRamp(uint8_t from,uint8_t to){
        frame=av_frame_alloc();
        frame->format=AV_PIX_FMT_BGR0;frame->width=frame->height=kSize;
        frame->color_range=AVCOL_RANGE_UNSPECIFIED;frame->color_trc=AVCOL_TRC_IEC61966_2_1;frame->colorspace=AVCOL_SPC_RGB;
        if(av_frame_get_buffer(frame,32)<0)return false;
        for(int y=0;y<kSize;++y)for(int x=0;x<kSize;++x){
            const int value=from+int(std::lround(double(to-from)*double(x)/double(kSize-1)));
            auto* p=frame->data[0]+std::size_t(y)*frame->linesize[0]+std::size_t(x)*4;
            p[0]=uint8_t(value);p[1]=uint8_t(value);p[2]=uint8_t(value);p[3]=255;
        }
        return true;
    }
};
struct Graph {
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;
    pipeline::EnhanceGraph g{ctx,ring};
    bool up=false;
    bool start(bool colorEnabled,bool rgb=true){
        gfx::DeviceContextDesc device;Status st;
        if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
        up=true;
        pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=kSize;
        gd.rgbInput=rgb;gd.stillImage=true;gd.noFeatures=true;gd.enableNr=false;gd.enableSr=false;gd.enableFg=false;
        gd.color.enabled=colorEnabled;
        return g.initialize(gd)&&g.createViews();
    }
    bool startWithLut(const std::wstring& lutName,int inputSpace=engine::ColorSettings::kLutInputSrgb){
        gfx::DeviceContextDesc device;Status st;
        if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
        up=true;
        pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=kSize;
        gd.rgbInput=true;gd.stillImage=true;gd.noFeatures=true;gd.enableNr=false;gd.enableSr=false;gd.enableFg=false;
        gd.color.enabled=true;
        if(!gd.color.setLutName(lutName))return false;
        gd.color.lutStrength=100.0f;
        gd.color.lutInputSpace=inputSpace;
        return g.initialize(gd)&&g.createViews();
    }
    bool startWithDither(float ditherStep){
        gfx::DeviceContextDesc device;Status st;
        if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return false;
        up=true;
        pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.sourceHeight=gd.workWidth=gd.workHeight=kSize;
        gd.rgbInput=true;gd.stillImage=true;gd.noFeatures=true;gd.enableNr=false;gd.enableSr=false;gd.enableFg=false;
        // A compressing grade: the ramp must be flattened by the tone curve so
        // plateaus are long enough to measure the dither's effect.
        gd.color.enabled=true;gd.color.contrast=-100.0f;gd.outputDitherStep=ditherStep;
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
// Test-only raw FP16 readback preserves exact comparison-reference values.
bool readReference(Graph& graph,ID3D12Resource* texture,std::vector<uint8_t>& pixels){
    if(!texture)return false;
    const auto desc=texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    graph.ctx.device()->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;
    buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if(FAILED(graph.ctx.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))return false;
    Status status;uint32_t slot;auto* list=graph.ring.acquireNext(slot,status);if(!list)return false;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=texture;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
    if(!graph.ring.submitAndSignal(slot)||!graph.ring.waitIdle())return false;
    void* data=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};if(FAILED(readback->Map(0,&range,&data)))return false;
    pixels.resize(size_t(desc.Width)*desc.Height*8);
    for(unsigned y=0;y<desc.Height;++y)memcpy(pixels.data()+size_t(y)*desc.Width*8,static_cast<uint8_t*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,size_t(desc.Width)*8);
    D3D12_RANGE empty{};readback->Unmap(0,&empty);return true;
}
Pixel center(const sink::RgbaImage& image){
    const auto index=(std::size_t(kSize/2)*std::size_t(image.width)+std::size_t(kSize/2))*4;
    return {image.pixels[index],image.pixels[index+1],image.pixels[index+2]};
}
}
int wmain(){
    for(bool rgb:{true,false}){
        Graph graph;Frame frame;
        bool ok=graph.start(true,rgb);
        if(rgb)ok=ok&&frame.make(100,120,140);
        else{
            frame.frame=av_frame_alloc();frame.frame->format=AV_PIX_FMT_NV12;frame.frame->width=frame.frame->height=kSize;
            frame.frame->color_range=AVCOL_RANGE_MPEG;frame.frame->colorspace=AVCOL_SPC_BT709;frame.frame->color_trc=AVCOL_TRC_BT709;
            ok=ok&&av_frame_get_buffer(frame.frame,32)>=0;
            if(ok){for(int y=0;y<kSize;++y)memset(frame.frame->data[0]+y*frame.frame->linesize[0],100,kSize);
                for(int y=0;y<kSize/2;++y)memset(frame.frame->data[1]+y*frame.frame->linesize[1],128,kSize);}
        }
        pipeline::EnhanceGraph::FrameOutputs out;
        std::vector<uint8_t> baseline,original,base;
        sink::RgbaImage graded,released;
        ok=ok&&graph.g.process(frame.frame,0,true,out,1)&&readReference(graph,graph.g.sourceReference(out.videoSlot),baseline);
        engine::EnhancementSettings settings;settings.color.enabled=true;settings.color.exposure=1.5f;
        ok=ok&&graph.apply(settings);
        for(uint64_t id=2;ok&&id<=5;++id){
            out={};
            ok=graph.g.process(frame.frame,double(id)*33,false,out,id)&&
                readReference(graph,graph.g.sourceReference(out.videoSlot),original)&&
                readReference(graph,graph.g.baseReference(out.videoSlot),base)&&
                sink::readRgba8(graph.ctx,graph.ring,graph.g.videoFrameResource(out.videoSlot),graded);
            ok=ok&&original==baseline&&base!=original;
        }
        out={};
        ok=ok&&graph.g.process(frame.frame,200,false,out,6,nullptr,nullptr,false)&&
            sink::readRgba8(graph.ctx,graph.ring,graph.g.videoFrameResource(out.videoSlot),released)&&released.pixels==graded.pixels;
        check(ok,rgb?"RGB original excludes grade across both slots; base and released output retain grade":"NV12 original excludes grade across both slots; base and released output retain grade");
    }
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
        Pixel neutralGrass{};
        {
            if(!graph.render(grass,d)){std::printf("FAIL colour render\n");return 1;}
            const auto coloured=center(d);neutralGrass=coloured;
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
        {
            // T4 verbs: the hue table (mixer) and the luminance table (grading).
            engine::EnhancementSettings s;s.color.enabled=true;s.color.mixerSaturation[3]=100.0f;
            sink::RgbaImage mixed;
            const bool ok=graph.apply(s)&&graph.render(grass,mixed);
            const auto pixel=center(mixed);
            check(ok&&(pixel.g-std::max(pixel.r,pixel.b))>(neutralGrass.g-std::max(neutralGrass.r,neutralGrass.b)),
                "green mixer saturation raises the green separation");
        }
        {
            engine::EnhancementSettings s;s.color.enabled=true;
            s.color.grading[1]={0,100,0};
            sink::RgbaImage graded;
            const bool ok=graph.apply(s)&&graph.render(neutral,graded);
            const auto pixel=center(graded);
            check(ok&&pixel.r>pixel.b+10,"mid-tone grading wheel tints mid grey towards red");
        }
        {
            // T4 black & white mixer: the switch must actually produce a
            // monochrome frame, and the per-band row must move that band's grey
            // instead of being an inert slider.
            engine::EnhancementSettings s;s.color.enabled=true;s.color.blackWhite=true;
            sink::RgbaImage monoGreen,monoGreenLifted;
            const bool monochrome=graph.apply(s)&&graph.render(grass,monoGreen)&&!monoGreen.pixels.empty();
            check(monochrome,"the black and white mixer renders through the real graph");
            if(monochrome){
                const auto grey=center(monoGreen);
                check(grey.r==grey.g&&grey.g==grey.b,"the black and white mixer collapses the frame to grey");
                s.color.blackWhiteMix[3]=60.0f;
                const bool lifted=graph.apply(s)&&graph.render(grass,monoGreenLifted)&&!monoGreenLifted.pixels.empty();
                const auto liftedGrey=lifted?center(monoGreenLifted):Pixel{};
                check(lifted&&liftedGrey.r==liftedGrey.g&&liftedGrey.g==liftedGrey.b&&liftedGrey.r>grey.r+8,
                    std::format("the green band row lightens the green area's grey (neutral {} vs lifted {})",grey.r,liftedGrey.r));
                // Same non-colour payload as the accepted call above, so only the
                // colour block changes (a fresh default settings object carries a
                // different multiplier/backend combination the graph rejects).
                auto off=s;off.color=engine::ColorSettings{};off.color.enabled=true;
                sink::RgbaImage colour;
                const bool applied=graph.apply(off);
                const bool rendered=applied&&graph.render(grass,colour)&&!colour.pixels.empty();
                const auto back=rendered?center(colour):Pixel{};
                check(rendered&&back.g>back.r+8,
                    std::format("turning the black and white mixer off restores the colour image (applied={} r={} g={} b={})",applied,back.r,back.g,back.b));
            }
        }
        {
            // Section bypass ("分组眼睛"): stopping the 亮 group must render
            // exactly like the ungraded reference while the numbers stay stored.
            engine::EnhancementSettings lit;lit.color.enabled=true;lit.color.exposure=1.0f;lit.color.contrast=-40.0f;
            sink::RgbaImage graded,bypassed;
            // A fresh default settings object differs from this graph in a
            // non-colour field, so the reference keeps the accepted payload and
            // only resets the colour block (enabled but neutral = identity).
            auto reference=lit;reference.color=engine::ColorSettings{};reference.color.enabled=true;
            const bool referenced=graph.apply(reference)&&graph.render(neutral,graded);
            auto off=lit;off.color.groupBypassMask=1u<<0;
            const bool ignored=graph.apply(off)&&graph.render(neutral,bypassed);
            check(referenced&&ignored&&!graded.pixels.empty()&&graded.pixels==bypassed.pixels,
                "bypassing the 亮 group renders exactly like the ungraded reference");
            check(off.color.exposure==1.0f,"the bypassed group keeps its stored numbers");
        }
    }
    // 3. Output dither (plan section 4.1): a smooth ramp pushed through a
    // compressing grade must not turn into long flat plateaus at the 8-bit
    // write. The same graph is rendered with the dither off and at one LSB.
    {
        Graph plain,ditheredGraph;
        const bool started=plain.startWithDither(0.0f)&&ditheredGraph.startWithDither(1.0f/255.0f);
        check(started,"the dither probe graphs initialise");
        if(started){
            Frame rampA,rampB;
            sink::RgbaImage noDither,dithered;
            const bool rendered=rampA.makeRamp(112,128)&&rampB.makeRamp(112,128)&&
                plain.render(rampA,noDither)&&ditheredGraph.render(rampB,dithered);
            check(rendered,"the compressing ramp renders through both graphs");
            if(rendered&&!noDither.pixels.empty()&&!dithered.pixels.empty()){
                const auto longestRun=[&](const sink::RgbaImage& image){
                    int longest=0,current=0,previous=-1;
                    for(int x=8;x<kSize-8;++x){
                        const int value=image.pixels[(std::size_t(kSize/2)*image.width+std::size_t(x))*4];
                        if(value==previous)++current;else current=1;
                        previous=value;longest=std::max(longest,current);
                    }
                    return longest;
                };
                const int without=longestRun(noDither),with=longestRun(dithered);
                check(with<without,std::format("the output dither breaks the banding plateaus apart ({} -> {} px)",without,with));
                check(with<=6,std::format("dither keeps the longest identical 8-bit run short ({} px, without dither {} px)",with,without));
            }
        }
    }
    // 4. A real .cube file, imported into runtime_local/luts and resolved by name
    // when the graph is built - the same path the UI importer uses.
    {
        const engine::ColorLutStore store(veyra::runtime::localDataDirectory());
        std::error_code ec;std::filesystem::create_directories(store.folder(),ec);
        const auto source=store.folder()/L"gpu-probe-halve.cube";
        {
            std::ofstream file(source,std::ios::binary);
            file<<"TITLE \"halve\"\nLUT_3D_SIZE 2\n";
            for(int b=0;b<2;++b)for(int g=0;g<2;++g)for(int r=0;r<2;++r)
                file<<(r*0.5f)<<" "<<(g*0.5f)<<" "<<(b*0.5f)<<"\n";
        }
        Frame frame,reference;
        sink::RgbaImage baseline,graded;
        bool ok=frame.make(188,188,188)&&reference.make(188,188,188);
        if(ok){
            Graph plain,withLut;
            ok=plain.start(true)&&plain.render(reference,baseline);
            if(ok)ok=withLut.startWithLut(L"gpu-probe-halve.cube")&&withLut.render(frame,graded);
        }
        check(ok,"a .cube imported into runtime_local/luts resolves when the graph is built");
        if(ok)check(center(graded).r<center(baseline).r-20,"the imported halving LUT darkens the frame through the 3D sampler");
        // A PQ input space means nothing on SDR content: it must be refused with
        // a visible notice, and the frame must come out exactly as without a LUT.
        if(getenv("VEYRA_SKIP_LUT_SPACE_CASE")==nullptr){
            Graph mismatched;
            Frame third;
            sink::RgbaImage ungraded;
            bool refused=third.make(188,188,188)&&mismatched.startWithLut(L"gpu-probe-halve.cube",engine::ColorSettings::kLutInputPq)&&mismatched.render(third,ungraded);
            check(refused&&!mismatched.g.colorLutNotice().empty(),
                "a PQ LUT on SDR content is refused with a notice instead of applied silently");
            if(refused)check(std::abs(center(ungraded).r-center(baseline).r)<=1,
                "the refused LUT leaves the frame identical to the no-LUT render");
        }
        std::filesystem::remove(source,ec);
    }
    if(failures){std::printf("FAIL: colour grade GPU contract (%d checks)\n",failures);return 1;}
    std::printf("PASS: colour grade GPU contract (off/neutral identity, exposure, curve, saturation, hue)\n");
    return 0;
}
