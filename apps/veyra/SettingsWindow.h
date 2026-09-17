#pragma once
#include "veyra/engine/EngineController.h"
#include "veyra/engine/ExportJobManager.h"
#include <vector>
namespace veyra::ui {
engine::EnhancementSettings defaultSettings();
HWND createSettingsPanel(HWND,engine::EngineController&,std::function<bool(engine::EnhancementSettings)>);
void settingsEnabled(bool,const engine::EnhancementSettings&);
void settingsVisibility(bool);void settingsPage(int);void settingsDpi();
void exportPanelStatus(const engine::ExportJobSnapshot&,bool canExport,bool canSave);
HWND settingsControlForTest(int);
// Test/diagnostic hook: edit-control id of the colour row with this exact label,
// or -1. Lets acceptance code address rows by name instead of by index.
int colourParamEditId(const wchar_t* label);
// Test/diagnostic hooks for the colour-grading wheels: the client point that
// corresponds to a hue/saturation pair on the disc, and to a luminance value on
// the bar below it. Keeps the wheel geometry in one place.
bool settingsColorWheelTestPoint(int zone,float hue,float saturation,POINT& out);
bool settingsColorWheelTestBarPoint(int zone,float luminance,POINT& out);
int colourWheelControlId(int zone);
// Scroll the colour page so the given control is inside the viewport (used by
// acceptance screenshots and by the smoke before it drives a control).
void settingsColorScrollToTest(int id);
// Mixer correction mode (0 hue / 1 saturation / 2 luminance / 3 black & white)
// and the selected colour range, driven through the same code path the UI uses.
void settingsColorMixerModeForTest(int mode);
void settingsColorBandForTest(int band);
int colourBandsControlId();
}
