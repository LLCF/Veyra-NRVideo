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
}
