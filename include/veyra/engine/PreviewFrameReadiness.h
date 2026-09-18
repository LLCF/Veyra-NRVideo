#pragma once
#include "veyra/pipeline/FrameBatch.h"

namespace veyra::engine {
enum class PreviewFrameReadiness { Ready, Pending, Invalid, Suppressed, Expired };

// Obsolete preview outputs must not hold a ready original behind their fences.
// This only decides presentation; the completion watcher still owns every GPU
// lease and resolves generation status before resources can be recycled.
template<class Resolve>
PreviewFrameReadiness previewFrameReadiness(pipeline::BatchFrame& frame,bool suppressed,bool expired,Resolve&& resolve){
    const bool generated=frame.kind==pipeline::FrameKind::Generated;
    if(generated){
        if(frame.validity!=pipeline::GenerationValidity::Pending&&frame.validity!=pipeline::GenerationValidity::Valid)return PreviewFrameReadiness::Invalid;
        if(suppressed)return PreviewFrameReadiness::Suppressed;
        if(expired)return PreviewFrameReadiness::Expired;
    }
    if(!resolve())return PreviewFrameReadiness::Pending;
    return generated&&frame.validity!=pipeline::GenerationValidity::Valid?PreviewFrameReadiness::Invalid:PreviewFrameReadiness::Ready;
}
}
