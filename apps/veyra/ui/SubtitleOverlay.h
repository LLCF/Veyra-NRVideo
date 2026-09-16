#pragma once

// Layered subtitle overlay. One cue can carry several lines and a full style
// (font/size/colour/outline/shadow/background/alignment) coming from ASS/SSA or
// from the viewer's global preferences. Primary and secondary (dual language)
// lines are stacked bottom-up by the caller.
#include <windows.h>

#include <string>
#include <vector>

#include "veyra/engine/Subtitles.h"

namespace veyra::ui {
struct SubtitleLine {
    std::wstring text;                 // '\n' separated
    engine::SubtitleStyle style;
    int alignOverride=0;               // {\anN}, 0 = style alignment
    double posX=-1,posY=-1;            // {\pos(x,y)} normalised to 0..1 (or -1)
    bool secondary=false;
};
struct SubtitleView {
    double scale=1.0;                  // viewer size multiplier
    std::wstring fontOverride;         // empty = keep the style font
    bool outline=true;
    bool background=false;
    int bottomMargin=0;                // extra logical pixels above the bar
    int blockGap=6;                    // gap between the two languages
};
HWND createSubtitleOverlay(HWND parent);
void updateSubtitleOverlay(HWND,const std::vector<SubtitleLine>&,const SubtitleView&);
} // namespace veyra::ui
