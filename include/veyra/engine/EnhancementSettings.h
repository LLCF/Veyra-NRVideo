#pragma once
#include "veyra/engine/VideoHdrSettings.h"
#include <cmath>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include "veyra/pipeline/ResolutionPlan.h"
#include "veyra/source/CaptureBuffer.h"
#include "veyra/engine/ColorSettings.h"
namespace veyra::engine {
enum class FrameGenerationBackend { Dlss, XeSS, Fsr };
// Frame generation that runs inside the present sink (external frame
// interpolation swapchain) instead of the in-graph DLSSG backend.
constexpr bool presentSinkFrameGeneration(FrameGenerationBackend backend) {
    return backend==FrameGenerationBackend::XeSS||backend==FrameGenerationBackend::Fsr;
}
// Multipliers the UI offers. 5X is intentionally absent: the DLSS runtime
// exposes 2/3/4/6 and MFG buyers pick from those; 1 = generation off.
inline constexpr uint32_t kFgMultiplierChoices[]={1,2,3,4,6};
inline constexpr size_t kFgMultiplierChoiceCount=sizeof(kFgMultiplierChoices)/sizeof(kFgMultiplierChoices[0]);
// Export bitrate presets in Mbps; index 0 keeps the encoder's constant-quality
// default. Labels are the UI strings for the same order.
inline constexpr uint32_t kExportBitrateChoices[]={0,6,10,16,24,40,60,100,150,200};
inline constexpr size_t kExportBitrateChoiceCount=sizeof(kExportBitrateChoices)/sizeof(kExportBitrateChoices[0]);
inline constexpr const wchar_t* kExportBitrateLabels[]={L"自动 · 恒定质量",L"6 Mbps",L"10 Mbps",L"16 Mbps",L"24 Mbps",L"40 Mbps",L"60 Mbps",L"100 Mbps",L"150 Mbps",L"200 Mbps"};
inline size_t exportBitrateIndex(uint32_t mbps){for(size_t i=0;i<kExportBitrateChoiceCount;++i)if(kExportBitrateChoices[i]==mbps)return i;return 0;}
// videoSrQuality values: 0 = DLSS SR, 1..4 = RTX video SR quality steps,
// 5 = AMD FSR upscaling (vendor neutral; verified on an NVIDIA adapter).
inline constexpr uint32_t kVideoSrFsr=5;
// Capture audio ingress policy. Automatic is what the pipeline did before the
// manual selector existed; the other two exist because Dolby/DTS passthrough
// negotiation depends on the device and the console, and users reported that
// "let the capture card decide" is not reliable in practice.
enum class CaptureAudioIngress { Auto, PcmOnly, BitstreamPreferred };
constexpr std::wstring_view captureAudioIngressName(CaptureAudioIngress mode) {
    switch(mode) {
    case CaptureAudioIngress::PcmOnly: return L"强制线性 PCM";
    case CaptureAudioIngress::BitstreamPreferred: return L"位流优先（Dolby/DTS 直通解码）";
    case CaptureAudioIngress::Auto: break;
    }
    return L"自动（优先 PCM，必要时位流解码）";
}
enum class NrRuntime { Original, Community, Ampere };
constexpr std::string_view nrRuntimeName(NrRuntime runtime) {
    switch(runtime) {
    case NrRuntime::Original:return "NVIDIA-original";
    case NrRuntime::Community:return "community-RTX40-RTX50";
    case NrRuntime::Ampere:return "community-RTX30-experimental";
    }
    return "unknown";
}
enum class FlowQuality { Performance, Balanced, Quality };
enum class OpticalFlowBackend { Nvidia, AmdFidelityFx, GpuDis };
enum class ContentRate { Transport, Auto, Fps30, Fps50, Fps60, Capture60To30 };
enum class AudioSyncMode { Automatic, Manual, Off };
constexpr std::string_view frameGenerationBackendName(FrameGenerationBackend backend) {
    switch(backend) {
    case FrameGenerationBackend::Dlss: return "DLSS";
    case FrameGenerationBackend::XeSS: return "XeSS";
    case FrameGenerationBackend::Fsr: return "AMD-FSR";
    }
    return "unknown";
}
constexpr std::string_view opticalFlowBackendName(OpticalFlowBackend backend) {
    switch(backend) {
    case OpticalFlowBackend::Nvidia: return "NVIDIA_NVOF";
    case OpticalFlowBackend::AmdFidelityFx: return "AMD_FIDELITYFX_OF";
    case OpticalFlowBackend::GpuDis: return "GPU_DIS_FAST";
    }
    return "unknown";
}
struct NrSettings {
    float intensity=1,tone=1,structure=1,skin=-1;
    int32_t style=0,autoMask=0,uiCorrection=0;
    bool operator==(const NrSettings&) const = default;
};
struct ResidualSettings {
    float total=1,darken=1,brighten=1,color=1,luminance=1;
    bool operator==(const ResidualSettings&) const = default;
};
struct ProtectionRect {
    float left=0,top=0,right=0,bottom=0;
    bool empty()const{return left==right||top==bottom;}
    bool operator==(const ProtectionRect&)const=default;
};
struct ProtectionSettings {
    bool enabled=false;
    float featherPixels=2;
    std::array<ProtectionRect,4> regions{};
    bool operator==(const ProtectionSettings&)const=default;
    std::string validate()const{
        // Upper bound is 64 px at the working extent; TiledImageProcessor keeps
        // its tile halo above this value so export tiles cannot clip the ramp.
        if(!std::isfinite(featherPixels)||featherPixels<0||featherPixels>64)return "invalid protection feather";
        for(auto r:regions){for(float v:{r.left,r.top,r.right,r.bottom})if(!std::isfinite(v)||v<0||v>1)return "invalid protection rectangle";
            if(r.left>r.right||r.top>r.bottom)return "inverted protection rectangle";}
        return {};
    }
};
struct EnhancementSettings {
    uint64_t revision=1;
    NrSettings model;
    ResidualSettings residual;
    ProtectionSettings protection;
    bool nr=false,sr=false;
    bool lowLatency=false; // preview only: NR before SR, opt-in
    NrRuntime nrRuntime=NrRuntime::Original;
    bool captureCompatible=false;
    // Capture audio ingress; requires a reconnect to take effect (the media type
    // is negotiated when the graph is built).
    CaptureAudioIngress captureAudio=CaptureAudioIngress::Auto;
    // Capture video-pin allocator policy; requires a reconnect to take effect
    // (the allocator is created while the capture graph is built).
    source::CaptureBufferMode captureBuffer=source::CaptureBufferMode::Auto;
    // Capture ingest only: flip the incoming frame vertically. Exists because
    // some devices declare a DIB orientation that does not match their samples
    // (RGB24 upside-down reports); the capture source asks for top-down first,
    // this is the manual fallback when a driver still misreports. Applies to
    // the next sample, so it is a live edit, not a graph rebuild.
    bool captureFlipVertical=false;
    bool forceSdrPreview=false; // display only; retain actual HDR source metadata
    VideoHdrSettings videoHdr;
    bool useHdrPreview(bool hdrInput,bool hdrDisplayActive)const{return (hdrInput||videoHdr.enabled)&&hdrDisplayActive&&!forceSdrPreview;}
    pipeline::SrTarget srTarget=pipeline::SrTarget::Uhd4K;
    uint32_t videoSrQuality=0; // 0 DLSS SR; 1–4 RTX Video SR
    uint32_t multiplier=1;
    FrameGenerationBackend frameGenerationBackend=FrameGenerationBackend::Dlss;
    // Export target bitrate in Mbps; 0 keeps the encoder's constant-quality
    // default (NVENC CONSTQP / MFT quality mode). Only the export job consumes
    // it: preview never re-encodes.
    uint32_t exportBitrateMbps=0;
    // Lightroom-aligned colour grade. Live: the engine uploads it per frame, so
    // changing it never rebuilds the graph (see sameVideoConfiguration below).
    ColorSettings color;
    pipeline::NrSizePolicy nrPolicy=pipeline::NrSizePolicy::Realtime;
    FlowQuality flow=FlowQuality::Balanced;
    OpticalFlowBackend opticalFlowBackend=OpticalFlowBackend::Nvidia;
    bool amdFlowHalfResolution=false;
    ContentRate content=ContentRate::Transport;
    AudioSyncMode audioSync=AudioSyncMode::Automatic;
    int32_t audioOffsetMs=0;
    bool operator==(const EnhancementSettings&) const = default;
    bool sameVideoConfiguration(const EnhancementSettings& other) const {
        auto video=*this;
        video.revision=other.revision;
        video.audioSync=other.audioSync;
        video.audioOffsetMs=other.audioOffsetMs;
        // Capture flip is applied per sample on the source's callback thread;
        // toggling it must not rebuild the graph.
        video.captureFlipVertical=other.captureFlipVertical;
        // Export-only fields: changing the bitrate must never invalidate the
        // running preview graph (the controller would otherwise rebuild it).
        video.exportBitrateMbps=other.exportBitrateMbps;
        // Colour grade: parameters are uniform-only (never a rebuild), but the
        // master switch changes the graph shape, so it stays in the comparison.
        video.color=other.color;
        video.color.enabled=color.enabled;
        video.videoHdr=other.videoHdr;
        video.videoHdr.enabled=videoHdr.enabled;
        return video==other;
    }
    void rejectVideoRequest(const EnhancementSettings& attempted,const EnhancementSettings& previous) {
        if(revision!=attempted.revision)return;
        auto restored=previous;
        if(audioSync!=attempted.audioSync||audioOffsetMs!=attempted.audioOffsetMs){
            restored.audioSync=audioSync;restored.audioOffsetMs=audioOffsetMs;
        }
        *this=restored;
    }
    std::string validate() const {
        if(!videoHdr.valid())return "invalid RTX Video HDR parameters";
        auto range=[](float v,float hi){return std::isfinite(v)&&v>=0&&v<=hi;};
        if(auto error=protection.validate();!error.empty())return error;
        if(!revision)return "settingsRevision must be nonzero";
        if(nrRuntime!=NrRuntime::Original&&nrRuntime!=NrRuntime::Community&&nrRuntime!=NrRuntime::Ampere)return "invalid NR runtime";
        if(captureAudio<CaptureAudioIngress::Auto||captureAudio>CaptureAudioIngress::BitstreamPreferred)return "invalid capture audio ingress mode";
        if(captureBuffer<source::CaptureBufferMode::Auto||captureBuffer>source::CaptureBufferMode::DriverDefault)return "invalid capture buffer mode";
        if(audioSync<AudioSyncMode::Automatic||audioSync>AudioSyncMode::Off||audioOffsetMs<-250||audioOffsetMs>250)return "invalid audio sync setting";
        if(!range(model.intensity,1)||!range(model.tone,1)||!range(model.structure,1))return "model parameter out of range";
        if(model.skin!=-1&&!range(model.skin,2))return "skin parameter out of range";
        if(model.style<0||model.style>2||model.autoMask<0||model.autoMask>1||model.uiCorrection<0||model.uiCorrection>1)return "invalid experimental parameter";
        for(float v:{residual.total,residual.darken,residual.brighten,residual.color,residual.luminance})if(!range(v,2))return "residual parameter out of range";
        if(frameGenerationBackend<FrameGenerationBackend::Dlss||frameGenerationBackend>FrameGenerationBackend::Fsr)return "invalid frame generation backend";
        // The XeSS provider reports its own generated-frame ceiling at session
        // start; the engine clamps/rejects above it, so validation only guards
        // the absolute API range here.
        if(frameGenerationBackend==FrameGenerationBackend::XeSS&&multiplier>4)return "XeSS frame generation supports up to 4X";
        // The AMD 3.1.x provider delivers one generated frame per present; the
        // probe measured the same count for 2/3/4 requested frames.
        if(frameGenerationBackend==FrameGenerationBackend::Fsr&&multiplier>2)return "AMD FSR frame generation supports up to 2X";
        if(videoSrQuality>kVideoSrFsr)return "invalid video SR quality";
        if(!pipeline::validSrTarget(srTarget))return "invalid SR target";
        if(opticalFlowBackend!=OpticalFlowBackend::Nvidia&&opticalFlowBackend!=OpticalFlowBackend::AmdFidelityFx&&opticalFlowBackend!=OpticalFlowBackend::GpuDis)return "invalid optical flow backend";
        if(multiplier<1||multiplier>6)return "unsupported multiplier";
        // 0 = auto quality; explicit values are capped at 300 Mbps so a typo
        // cannot ask a driver for a nonsense rate.
        if(exportBitrateMbps>300)return "export bitrate out of range";
        if(auto error=color.validate();!error.empty())return error;
        if(!pipeline::validNrSizePolicy(nrPolicy))return "invalid NR size policy";
        if(flow<FlowQuality::Performance||flow>FlowQuality::Quality||content<ContentRate::Transport||content>ContentRate::Capture60To30)return "invalid flow/content mode";
        return {};
    }
};
}
