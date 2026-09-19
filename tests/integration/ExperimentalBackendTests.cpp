#include "veyra/engine/VideoPresenter.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
using namespace veyra;

namespace {

struct FlowSample {
    float x = 0.0f;
    float y = 0.0f;
    size_t count = 0;
};

float halfToFloat(uint16_t bits)
{
    const uint32_t sign = uint32_t(bits & 0x8000u) << 16;
    uint32_t exponent = (bits >> 10) & 0x1Fu;
    uint32_t mantissa = bits & 0x03FFu;
    uint32_t result = 0;
    if (exponent == 0) {
        if (mantissa != 0) {
            exponent = 127 - 14;
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            result = sign | (exponent << 23) | ((mantissa & 0x03FFu) << 13);
        } else {
            result = sign;
        }
    } else if (exponent == 0x1Fu) {
        result = sign | 0x7F800000u | (mantissa << 13);
    } else {
        result = sign | ((exponent + 112) << 23) | (mantissa << 13);
    }
    return std::bit_cast<float>(result);
}

// Test-only readback. Normal playback, capture, and video export never call this.
bool readCanonicalFlow(gfx::D3D12DeviceContext& ctx, gfx::CommandSlotRing& ring,
                       ID3D12Resource* flow, FlowSample& sample)
{
    if (flow == nullptr || flow->GetDesc().Format != DXGI_FORMAT_R16G16_FLOAT) return false;
    const D3D12_RESOURCE_DESC desc = flow->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowBytes = 0, total = 0;
    ctx.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = total;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if (FAILED(ctx.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;

    Status status = Status::Ok;
    uint32_t slot = 0;
    ID3D12GraphicsCommandList* list = ring.acquireNext(slot, status);
    if (list == nullptr) return false;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {flow, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = flow;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    if (!ring.submitAndSignal(slot) || !ring.waitIdle()) return false;

    uint8_t* data = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
    if (FAILED(readback->Map(0, &range, reinterpret_cast<void**>(&data)))) return false;
    std::vector<float> xs, ys;
    // Ignore the outer quarter. It contains intentional out-of-bounds rejection.
    for (uint32_t y = desc.Height / 4; y < desc.Height * 3 / 4; y += 4) {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(data + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch);
        for (uint32_t x = uint32_t(desc.Width) / 4; x < uint32_t(desc.Width) * 3 / 4; x += 4) {
            const float vx = halfToFloat(row[x * 2]);
            const float vy = halfToFloat(row[x * 2 + 1]);
            if (std::isfinite(vx) && std::isfinite(vy)) {
                xs.push_back(vx);
                ys.push_back(vy);
            }
        }
    }
    D3D12_RANGE empty{0, 0};
    readback->Unmap(0, &empty);
    if (xs.size() < 64) return false;
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    sample = {xs[xs.size() / 2], ys[ys.size() / 2], xs.size()};
    return true;
}

} // namespace

int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    const bool dis=std::wstring(argv[1]).find(L"dis")==0;
    const bool pan=wcscmp(argv[1],L"xess-pan2")==0||wcscmp(argv[1],L"xess-pan4")==0;
    const bool xess=pan||wcscmp(argv[1],L"xess")==0||wcscmp(argv[1],L"dis-xess")==0;
    const bool amd=wcscmp(argv[1],L"amd")==0;
    const bool sr=std::wstring(argv[1]).find(L"sr")==0;
    if(!xess&&!amd&&!sr&&!dis&&wcscmp(argv[1],L"nvof1080")!=0)return 2;
    const bool full=std::wstring(argv[1]).find(L"1080")!=std::wstring::npos;
    const unsigned width=(sr||full)?1920:640,height=(sr||full)?1080:360,frames=sr?3:48;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::filesystem::create_directories(argv[2]);
    Logger::instance().openFile((std::filesystem::path(argv[2])/"engine.log").wstring());
    SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS",L"1");
    HWND window=CreateWindowExW(0,L"STATIC",L"Veyra experimental backend test",WS_POPUP,0,0,960,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 1;
    ShowWindow(window,SW_SHOWNOACTIVATE);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return 1;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;ctx.device()->QueryInterface(IID_PPV_ARGS(&info));
    pipeline::EnhanceGraph graph(ctx,ring);engine::VideoPresenter presenter;
    pipeline::EnhanceGraphDesc desc;
    desc.sourceWidth=desc.workWidth=width;desc.sourceHeight=desc.workHeight=height;
    desc.rgbInput=true;desc.enableNr=false;desc.enableFg=xess;desc.noNgx=true;
    desc.enableNvofStandalone=true;
    desc.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;
    if(wcscmp(argv[1],L"xess-pan4")==0)desc.fgMultiplier=4;
    desc.opticalFlowBackend=dis?engine::OpticalFlowBackend::GpuDis:amd?engine::OpticalFlowBackend::AmdFidelityFx:engine::OpticalFlowBackend::Nvidia;
    if(sr){
        desc.sourceWidth=width;desc.sourceHeight=height;
        desc.workWidth=std::wstring(argv[1]).find(L"8k")!=std::wstring::npos?7680:2560;
        desc.workHeight=desc.workWidth==7680?4320:1440;
        desc.nrWidth=1920;desc.nrHeight=1080;desc.noNgx=false;desc.enableSr=true;
        desc.enableNvofStandalone=false;
        desc.videoSrQuality=std::wstring(argv[1]).find(L"video")!=std::wstring::npos?1:0;
        desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    }
    bool ok=graph.initialize(desc)&&presenter.open(ctx,window,graph)&&graph.createViews();
    if(xess)ok=ok&&presenter.xessActive();
    AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_RGBA;frame->width=int(width);frame->height=int(height);frame->color_trc=AVCOL_TRC_IEC61966_2_1;
    ok=ok&&av_frame_get_buffer(frame,32)>=0;
    pipeline::EnhanceGraph::FrameOutputs out;
    FlowSample rightward{}, leftward{};
    engine::PreviewView view;
    uint64_t panGenerated=0;
    if(pan)view.zoom=1.5f;
    for(unsigned i=0;ok&&i<frames;++i){
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        const int phase = i < 24 ? int(i) : int(i - 24);
        const int velocity = i < 24 ? 2 : -2;
        for(int y=0;y<int(height);++y)for(int x=0;x<int(width);++x){
            const int px=(x-phase*velocity+int(width))%int(width);auto* p=frame->data[0]+y*frame->linesize[0]+x*4;
            // A spatially non-periodic pattern makes a two-pixel translation
            // identifiable. The previous striped pattern had repeating aliases.
            uint32_t hash=uint32_t(px)*0x9E3779B9u^uint32_t(y)*0x85EBCA6Bu;
            hash^=hash>>16;hash*=0x7FEB352Du;hash^=hash>>15;hash*=0x846CA68Bu;hash^=hash>>16;
            p[0]=uint8_t(hash);p[1]=uint8_t(hash>>8);p[2]=uint8_t(hash>>16);p[3]=255;
        }
        out={};ok=graph.process(frame,i*1000.0/30,i==0||i==24,out,i+1);
        const bool moving=pan&&i>=4&&i<44;
        if(moving)view.pan(i<24?3.0f:-3.0f,1.0f,960,540,float(width),float(height));
        const auto beforePresent=presenter.xessGeneratedCount();
        if(ok)ok=presenter.present(ctx,ring,graph,out.videoSlot,false,false,0,false,.5f,out.batch.identity,view);
        if(moving)panGenerated+=presenter.xessGeneratedCount()-beforePresent;
        if(ok&&i==16){
            const auto before=presenter.xessGeneratedCount();
            ok=presenter.present(ctx,ring,graph,out.videoSlot,false,false,0,false,.5f,out.batch.identity,view);
            if(xess)ok=ok&&presenter.xessGeneratedCount()==before;
        }
        if(i==24)SetWindowPos(window,nullptr,0,0,800,600,SWP_NOACTIVATE|SWP_NOZORDER);
        if((amd||dis)&&(i==23||i==47)){
            ok=ring.drainQueue()&&ok;
            FlowSample& sample=i==23?rightward:leftward;
            ok=ok&&readCanonicalFlow(ctx,ring,graph.flowResource(),sample);
        }
        Sleep(34);
    }
    ok=ring.drainQueue()&&ok;
    const auto generated=presenter.xessGeneratedCount();
    const auto dispatches=dis?graph.metrics().gpuDisExecuteCount:graph.metrics().amdOfExecuteCount;
    if(sr&&ok){
        sink::RgbaImage image;
        ok=graph.metrics().srEvaluateCount==frames&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
        uint64_t nonblack=0;for(size_t i=0;i+3<image.pixels.size();i+=4)nonblack+=image.pixels[i]>8||image.pixels[i+1]>8||image.pixels[i+2]>8;
        ok=ok&&image.width==desc.workWidth&&image.height==desc.workHeight&&nonblack>uint64_t(image.width)*image.height/2;
        std::cout<<"SR extent="<<image.width<<"x"<<image.height<<" evaluate="<<graph.metrics().srEvaluateCount<<" nonblack="<<nonblack<<" pass="<<ok<<std::endl;
    }
    if(xess)ok=ok&&generated>10;
    if(pan){
        ok=ok&&panGenerated>10;
        std::cout<<"PAN multiplier="<<desc.fgMultiplier<<" generatedWhileMoving="<<panGenerated<<" pass="<<ok<<std::endl;
    }
    if(amd||dis){
        // The source moves right then left. Canonical current->previous motion
        // must therefore point left then right with no material vertical drift.
        const bool directionOk=rightward.count>0&&leftward.count>0&&rightward.x<-0.5f&&leftward.x>0.5f&&
            std::abs(rightward.y)<0.5f&&std::abs(leftward.y)<0.5f;
        std::cout<<(dis?"GPU DIS flow rightward=(":"AMD flow rightward=(")<<rightward.x<<","<<rightward.y<<") leftward=("<<leftward.x<<","<<leftward.y<<") samples="<<rightward.count<<"/"<<leftward.count<<" directionPass="<<directionOk<<std::endl;
        ok=ok&&dispatches==(dis?46u:48u)&&directionOk;
    }
    out={};presenter.close();graph.shutdown();av_frame_free(&frame);
    uint64_t errors=0;
    if(info)for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
        SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<uint8_t> data(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(SUCCEEDED(info->GetMessage(i,message,&size))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;log::error("debug",message->pDescription);}
    }
    ok=ok&&errors==0;
    std::cout<<"BACKEND "<<(dis?(xess?"gpu-dis-xess":"gpu-dis"):xess?"xess":amd?"amd-of":"sr")<<" generated="<<generated<<" amdDispatches="<<dispatches<<" debugErrors="<<errors<<" pass="<<ok<<" (SDK submissions, not scanout or visual quality)\n";
    info.Reset();ring.shutdown();ctx.shutdown();DestroyWindow(window);CoUninitialize();return ok?0:1;
}
