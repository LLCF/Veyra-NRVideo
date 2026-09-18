#pragma once
#include <d3d12.h>
#include <functional>
#include <memory>
#include <vector>
#include "veyra/sink/VideoEncoder.h"
namespace veyra::gfx {class D3D12DeviceContext;class CommandSlotRing;}
namespace veyra::pipeline {class EnhanceGraph;}
namespace veyra::sink {
// NVIDIA path: D3D12 NVENC with a zero-copy conversion into the registered
// NV12/P010 input texture. Selected automatically on NVIDIA adapters.
class NvencD3D12Encoder final : public VideoEncoder {
public:
    NvencD3D12Encoder();~NvencD3D12Encoder();
    bool open(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,pipeline::EnhanceGraph&,const EncoderConfig&,PacketWriter) override;
    bool encode(unsigned frameSlot,bool generated,int64_t pts) override;
    bool finish() override;
    void close() override;
    std::vector<uint8_t> headers() const override;
    EncoderBackend backend() const override {return EncoderBackend::Nvenc;}
    std::wstring describe() const override;
    std::wstring lastError() const override;
private:
    struct Impl;std::unique_ptr<Impl> p_;
};
}
