#pragma once
#include "veyra/engine/EnhancementSettings.h"

namespace veyra::engine {
enum class FailedBackend { None, Infrastructure, OpticalFlow, NgxCore, Nr, Sr, Fg };
inline bool disableUnsupportedNvidiaEffects(EnhancementSettings& settings,bool nvidia){
    if(nvidia)return false;
    const auto before=settings;
    settings.nr=false;
    // AMD FSR upscaling is vendor neutral and must survive the NVIDIA-only
    // normalization; every other SR backend is NGX-only.
    settings.sr=settings.videoSrQuality==kVideoSrFsr;
    if(settings.frameGenerationBackend==FrameGenerationBackend::Dlss)settings.multiplier=1;
    return settings!=before;
}
inline const wchar_t* backendFailureName(FailedBackend backend) {
    switch(backend){
    case FailedBackend::OpticalFlow:return L"光流";
    case FailedBackend::NgxCore:return L"NGX 核心";
    case FailedBackend::Nr:return L"NR";
    case FailedBackend::Sr:return L"超分";
    case FailedBackend::Fg:return L"帧生成";
    default:return L"GPU / 输入 / 显示资源";
    }
}
// Disable only the failed feature and its consumers. Never substitute zero
// motion for a failed optical-flow backend while claiming normal enhancement.
inline bool disableFailedBackend(EnhancementSettings& settings,FailedBackend backend){
    const auto before=settings;
    switch(backend){
    case FailedBackend::Nr:settings.nr=false;break;
    case FailedBackend::Sr:settings.sr=false;break;
    case FailedBackend::Fg:settings.multiplier=1;break;
    case FailedBackend::NgxCore:
        settings.nr=settings.sr=false;
        if(settings.frameGenerationBackend==FrameGenerationBackend::Dlss)settings.multiplier=1;
        break;
    case FailedBackend::OpticalFlow:
        settings.nr=false;settings.multiplier=1;
        if(!settings.videoSrQuality)settings.sr=false;
        break;
    default:return false;
    }
    return settings!=before;
}
}
