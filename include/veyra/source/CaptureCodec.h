#pragma once
#include <windows.h>
#include <dshow.h>
#include "veyra/source/CapturePixelFormat.h"

namespace veyra::source {
// Compressed capture subtypes the direct-connect path can carry. Decoding is
// done by our own backend; the system decoder (RenderStream+RGB32) remains the
// fallback whenever a backend is unavailable.
enum class CaptureCodec { None, Mjpeg, H264, Hevc, Av1, Vp9 };

inline CaptureCodec captureCodecOf(const GUID& subtype) {
    if (subtype == MEDIASUBTYPE_MJPG) return CaptureCodec::Mjpeg;
    if (captureIsFourcc(subtype, captureFourcc('H', '2', '6', '4')) ||
        captureIsFourcc(subtype, captureFourcc('h', '2', '6', '4')) ||
        captureIsFourcc(subtype, captureFourcc('X', '2', '6', '4')) ||
        captureIsFourcc(subtype, captureFourcc('a', 'v', 'c', '1'))) return CaptureCodec::H264;
    if (captureIsFourcc(subtype, captureFourcc('H', 'E', 'V', 'C')) ||
        captureIsFourcc(subtype, captureFourcc('h', 'e', 'v', 'c')) ||
        captureIsFourcc(subtype, captureFourcc('H', '2', '6', '5'))) return CaptureCodec::Hevc;
    if (captureIsFourcc(subtype, captureFourcc('A', 'V', '0', '1'))) return CaptureCodec::Av1;
    if (captureIsFourcc(subtype, captureFourcc('V', 'P', '9', '0'))) return CaptureCodec::Vp9;
    return CaptureCodec::None;
}

inline const char* captureCodecKey(CaptureCodec codec) {
    switch (codec) {
    case CaptureCodec::Mjpeg: return "mjpeg";
    case CaptureCodec::H264: return "h264";
    case CaptureCodec::Hevc: return "hevc";
    case CaptureCodec::Av1: return "av1";
    case CaptureCodec::Vp9: return "vp9";
    case CaptureCodec::None: break;
    }
    return "none";
}
}
