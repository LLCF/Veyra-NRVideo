#pragma once
#include <windows.h>
#include <string>
#include <functional>
// readFlip/setFlip toggle the manual vertical ingest flip for devices whose
// declared DIB orientation does not match their samples (RGB24 upside down).
namespace veyra::ui {
void showCapturePanel(HWND,std::function<void(const std::wstring&)>,std::function<bool()> readSdr,std::function<bool(bool)> setSdr);
// Extended entry point that also carries the capture flip callbacks.
void showCapturePanel(HWND,std::function<void(const std::wstring&)>,std::function<bool()> readSdr,std::function<bool(bool)> setSdr,
    std::function<bool()> readFlip,std::function<bool(bool)> setFlip);
}
