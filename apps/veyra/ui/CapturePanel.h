#pragma once
#include <windows.h>
#include <string>
#include <functional>
namespace veyra::ui {
// readAudioIngress/setAudioIngress use engine::CaptureAudioIngress values
// (0 automatic, 1 PCM only, 2 bitstream preferred). The mode needs a reconnect
// because the audio media type is negotiated when the capture graph is built.
// readFlip/setFlip toggle the manual vertical ingest flip for devices whose
// declared DIB orientation does not match their samples (RGB24 upside down).
void showCapturePanel(HWND,std::function<void(const std::wstring&)>,std::function<bool()> readSdr,std::function<bool(bool)> setSdr,
                      std::function<int()> readAudioIngress,std::function<bool(int)> setAudioIngress,
                      std::function<bool()> readFlip,std::function<bool(bool)> setFlip);
}
