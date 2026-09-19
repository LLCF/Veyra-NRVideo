#pragma once
#include <cmath>
#include <cstdint>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

namespace veyra::source {
inline bool validCaptureFrameRate(double fps) {
    return std::isfinite(fps)&&(fps==0||(fps>=1&&fps<=1000));
}
inline bool parseCaptureFrameRate(std::wstring_view text,double& fps) {
    if(text.empty()||text.size()>16||text.find_first_not_of(L"0123456789.")!=std::wstring_view::npos)return false;
    std::wistringstream input{std::wstring(text)};input.imbue(std::locale::classic());
    double value=0;if(!(input>>value)||!input.eof()||!validCaptureFrameRate(value))return false;
    fps=value;return true;
}
inline int64_t captureFrameInterval(double fps) {
    return validCaptureFrameRate(fps)&&fps>0?int64_t(std::llround(10000000.0/fps)):0;
}
inline bool captureFrameRateMatches(double requested,int64_t interval) {
    // Permit integer 100ns rounding and nominal 30 vs 30000/1001 modes.
    return requested>0&&interval>0&&std::abs(10000000.0/interval-requested)<=requested*.0011;
}
}
