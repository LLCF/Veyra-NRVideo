// N1 microbenchmark: per-frame cost of the DirectShow callback copy for the
// 4K packed capture formats, with and without the legacy CPU per-pixel unpack.
// CPU only; no device or capture card required.
#include "veyra/source/CaptureMediaType.h"
#include <chrono>
#include <cstdio>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra::source;
namespace {
GUID fourcc(const char* n){
    return GUID{captureFourcc(n[0],n[1],n[2],n[3]),0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
}
double benchCopy(GUID id,bool legacy,int iterations){
    VIDEOINFOHEADER2 vi{};vi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);vi.bmiHeader.biWidth=3840;vi.bmiHeader.biHeight=2160;vi.AvgTimePerFrame=166667;
    AM_MEDIA_TYPE type{};type.majortype=MEDIATYPE_Video;type.subtype=id;type.formattype=FORMAT_VideoInfo2;type.pbFormat=reinterpret_cast<BYTE*>(&vi);type.cbFormat=sizeof(vi);
    CaptureMediaLayout layout;if(!captureMediaLayout(type,layout))return -1;
    if(legacy&&!captureLegacyCpuLayout(layout))return -1;
    AVFrame* frame=av_frame_alloc();frame->width=int(layout.width);frame->height=int(layout.height);frame->format=layout.format;
    if(av_frame_get_buffer(frame,32)<0){av_frame_free(&frame);return -1;}
    std::vector<uint8_t> source(layout.sampleBytes,7);
    for(int i=0;i<3;++i)copyCaptureSample(layout,source.data(),source.size(),*frame);
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<iterations;++i)copyCaptureSample(layout,source.data(),source.size(),*frame);
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    av_frame_free(&frame);
    return elapsed/iterations;
}
}
int wmain(){
    struct Case{const char* name;GUID id;};
    const Case cases[]={
        {"YUY2",fourcc("YUY2")},{"UYVY",fourcc("UYVY")},{"YVYU",fourcc("YVYU")},
        {"RGB24",MEDIASUBTYPE_RGB24},{"RGB32",MEDIASUBTYPE_RGB32},
        {"RGB555",MEDIASUBTYPE_RGB555},{"RGB565",MEDIASUBTYPE_RGB565},
        {"NV12",fourcc("NV12")}};
    constexpr int iterations=30;
    std::printf("capture copy benchmark 3840x2160, %d iterations/frame\n",iterations);
    for(const auto& item:cases){
        const double gpu=benchCopy(item.id,false,iterations);
        const double legacy=benchCopy(item.id,true,iterations);
        if(legacy>=0)std::printf("%-7s N1 row-copy %6.3f ms/frame   legacy CPU-unpack %6.3f ms/frame\n",item.name,gpu,legacy);
        else std::printf("%-7s N1 row-copy %6.3f ms/frame\n",item.name,gpu);
    }
    return 0;
}
