#pragma once
#include <array>
#include <cmath>
#include "veyra/engine/ColorSettings.h"
namespace veyra::pipeline {
// CPU bake of the colour grade into the small tables the ingest shader reads.
// Everything expensive - chromatic adaptation, tone response, curve evaluation,
// per-band mixer weights, grading zones - happens here once per settings change,
// so per-pixel work stays a handful of lookups. Deliberately free of D3D12 so
// the maths can be unit-tested without a GPU.
//
// Curve domain: the working texture is scene-linear, so tonal operations are
// evaluated in a Cineon-style log domain (10 stops around 18% grey) and decoded
// back to linear. That matches the plan's "curves live in a log/display-referred
// domain" rule instead of bending linear values directly.
struct ColorGradeTables {
    static constexpr int kCurveEntries=1024;  // log-domain input -> linear rgb response
    static constexpr int kHueEntries=256;     // hue -> (shift, saturation scale, luminance scale)
    static constexpr int kLumEntries=256;     // tone zone -> (r,g,b) gain for colour grading
    std::array<float,kCurveEntries*4> curve{};
    std::array<float,kHueEntries*4> hue{};
    std::array<float,kLumEntries*4> lum{};
    // Linear-domain 3x3 (row-major): white balance + calibration, applied once
    // before the tone curve.
    std::array<float,9> matrix{1,0,0, 0,1,0, 0,0,1};
    float exposure=0;                 // EV, applied as a linear multiply
    float saturation=0,vibrance=0;    // -100..100, applied in the shader
    float lutStrength=0;              // 0..1 (0 disables the 3D LUT lookup)
    int lutInputSpace=0;
    bool identity=true;               // master switch off or every parameter neutral
    static ColorGradeTables bake(const engine::ColorSettings& settings);
    // Exposed for the tests and for callers that need the same log mapping.
    static float encodeLog(float linear);
    static float decodeLog(float encoded);
};
} // namespace veyra::pipeline
