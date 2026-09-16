#include "veyra/sink/VideoEncoder.h"
#include "veyra/sink/NvencD3D12Encoder.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/Log.h"

#include <format>

namespace veyra::sink {
// Defined in MfVideoEncoder.cpp (kept out of the header: the MFT details are an
// implementation concern of this sink library).
std::unique_ptr<VideoEncoder> createMediaFoundationEncoder();

std::unique_ptr<VideoEncoder> openVideoEncoder(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,pipeline::EnhanceGraph& graph,
                                              const EncoderConfig& config,PacketWriter writer,std::wstring& detail){
    detail.clear();
    const bool nvidia=ctx.adapter().isNvidia;
    EncoderConfig request=config;request.adapterVendorId=ctx.adapter().vendorId;
    // HDR (HEVC Main10) has no Media Foundation representation: the MFT path is
    // 8-bit 4:2:0 only, so an HDR export without NVENC fails loudly instead of
    // writing a silently tone-mapped file.
    if(graph.hdrOutput()&&!nvidia){
        detail=L"HDR（10bit）导出需要 NVIDIA NVENC；当前显卡只有 8bit 系统编码器。请关闭 HDR 或换 N 卡导出";
        log::error("export","HDR export refused: the media foundation path is 8-bit only");
        return nullptr;
    }
    // Test-only: force the Media Foundation path on an NVIDIA host so the
    // fallback encoder can be exercised without non-NVIDIA hardware. Never set
    // by the product UI.
    const bool forceMediaFoundation=GetEnvironmentVariableW(L"VEYRA_TEST_FORCE_MF_ENCODER",nullptr,0)>0;
    if(nvidia&&!forceMediaFoundation){
        auto nvenc=std::make_unique<NvencD3D12Encoder>();
        if(nvenc->open(ctx,ring,graph,request,writer)){
            log::info("export",std::format("encoder selected={}",std::string(encoderBackendName(nvenc->backend()))));
            return nvenc;
        }
        // A refused NVENC session (mixed/old nvEncodeAPI64.dll, codec the GPU
        // cannot encode) must not cost the user the whole export while the OS
        // exposes a hardware MFT for the same codec.
        log::warn("export","NVENC session unavailable; falling back to the Media Foundation hardware encoder");
    }
    auto mf=createMediaFoundationEncoder();
    if(mf->open(ctx,ring,graph,request,writer)){
        log::info("export",std::format("encoder selected={}",std::string(encoderBackendName(mf->backend()))));
        return mf;
    }
    detail=nvidia?L"NVIDIA NVENC 与系统硬件编码器都无法初始化；请更新显卡驱动后重试"
                 :L"当前显卡没有可用的硬件编码器（系统未提供 H.264/HEVC 编码 MFT）；请更新显卡驱动后重试";
    return nullptr;
}
} // namespace veyra::sink
