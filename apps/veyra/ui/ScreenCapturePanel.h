#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include "veyra/engine/EngineController.h"
namespace veyra::ui {
void showScreenCapturePanel(HWND,std::function<void(const std::wstring&)>,std::function<void()>,std::function<engine::PlayerSnapshot()>,std::function<void(bool)>);
bool screenCaptureDialogMessage(MSG&);
}
