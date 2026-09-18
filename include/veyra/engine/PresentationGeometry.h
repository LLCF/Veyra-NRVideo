#pragma once
#include "veyra/engine/PreviewView.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace veyra::engine {
// PresentBlit fits in client coordinates; DXGI stretches the buffer to that
// client. Provider color rectangles must use the inverse of the same mapping.
inline RECT presentationRegion(unsigned imageWidth,unsigned imageHeight,
    unsigned clientWidth,unsigned clientHeight,unsigned bufferWidth,unsigned bufferHeight,PreviewView view){
    if(!imageWidth||!imageHeight||!clientWidth||!clientHeight||bufferWidth<2||bufferHeight<2)return {};
    const float zoom=std::isfinite(view.zoom)&&view.zoom>0?view.zoom:1.0f;
    const float fit=std::min(float(clientWidth)/imageWidth,float(clientHeight)/imageHeight)*zoom;
    const float sx=float(bufferWidth)/clientWidth,sy=float(bufferHeight)/clientHeight;
    const float left=(clientWidth*.5f-view.centerX*imageWidth*fit)*sx;
    const float top=(clientHeight*.5f-view.centerY*imageHeight*fit)*sy;
    const auto edge=[](float value,unsigned bound){return LONG(std::clamp(value,0.0f,float(bound)))&~1L;};
    return {edge(left,bufferWidth),edge(top,bufferHeight),
        edge(left+imageWidth*fit*sx+.5f,bufferWidth),edge(top+imageHeight*fit*sy+.5f,bufferHeight)};
}
}
