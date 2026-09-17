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
}
