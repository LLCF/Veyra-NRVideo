#pragma once
#include <windows.h>
#include <string>
#include <functional>
namespace veyra::ui {
// readAudioIngress/setAudioIngress use engine::CaptureAudioIngress values
// (0 automatic, 1 PCM only, 2 bitstream preferred). The mode needs a reconnect
// because the audio media type is negotiated when the capture graph is built.
void showCapturePanel(HWND,std::function<void(const std::wstring&)>,std::function<bool()> readSdr,std::function<bool(bool)> setSdr,
                      std::function<int()> readAudioIngress,std::function<bool(int)> setAudioIngress);
}
