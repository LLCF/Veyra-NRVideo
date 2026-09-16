#pragma once
#include <string_view>

namespace veyra::source {
// Video-pin allocator policy for the DirectShow direct-connect capture paths.
// Persisted with the settings and applied on the next connect: the allocator is
// created while the capture graph is built, so this cannot be a live edit.
enum class CaptureBufferMode { Auto = 0, Minimum = 1, DriverDefault = 2 };

constexpr std::wstring_view captureBufferModeName(CaptureBufferMode mode) {
    switch (mode) {
    case CaptureBufferMode::Minimum: return L"最小（1 帧缓冲）";
    case CaptureBufferMode::DriverDefault: return L"驱动默认（不干预）";
    case CaptureBufferMode::Auto: break;
    }
    return L"自动（按分辨率）";
}

// Narrow key for logs and CLI parsing.
constexpr const char* captureBufferModeKey(CaptureBufferMode mode) {
    switch (mode) {
    case CaptureBufferMode::Minimum: return "minimum";
    case CaptureBufferMode::DriverDefault: return "driver";
    case CaptureBufferMode::Auto: break;
    }
    return "auto";
}

// Auto keeps two buffers at 1080p and below, three above it: one slot is always
// being filled while the callback drains the other, and a >1080p frame takes
// longer over USB, so starving the device there costs dropped frames. Minimum
// asks for a single buffer; DriverDefault makes no suggestion (0).
constexpr long captureDesiredVideoBuffers(CaptureBufferMode mode, long width, long height) {
    switch (mode) {
    case CaptureBufferMode::Minimum: return 1;
    case CaptureBufferMode::Auto: return (width > 1920 || height > 1080) ? 3 : 2;
    case CaptureBufferMode::DriverDefault: break;
    }
    return 0;
}
}
