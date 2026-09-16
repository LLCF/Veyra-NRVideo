#pragma once

// Subtitle engine: external text formats (SRT/ASS/SSA/WebVTT), subtitle tracks
// embedded in the media container (text codecs only: SubRip/ASS/SSA/mov_text/
// WebVTT) and the timing helpers the UI needs. Image subtitles (PGS/DVB) have
// no decoder in the shipped FFmpeg build, so those tracks are listed with an
// explicit note instead of silently disappearing.
#include <string>
#include <vector>

namespace veyra::engine {

// Colors are 0xAARRGGBB. Sizes are in 96-DPI logical pixels; 0 means "follow
// the viewer's global subtitle size".
struct SubtitleStyle {
    std::wstring font;
    double size=0;
    uint32_t primary=0xFFFFFFFFu;
    uint32_t outline=0xC0000000u;
    uint32_t back=0x80000000u;
    double outlineWidth=0;
    double shadow=0;
    int alignment=2;              // numpad layout: 1..9, 2 = bottom centre
    double marginL=0,marginR=0,marginV=0;
    bool bold=false,italic=false,background=false;
};

struct SubtitleCue {
    double begin=0,end=0;
    std::wstring text;            // '\n' separated, override tags already removed
    int style=0;                  // index into SubtitleTrack::styles
    int alignOverride=0;          // 1..9 from {\anN}; 0 = keep the style value
    double posX=-1,posY=-1;       // {\pos(x,y)} in script pixels; -1 = unset
};

struct SubtitleTrack {
    std::wstring name,language,codec,note;
    bool embedded=false;
    int streamIndex=-1;
    std::vector<SubtitleStyle> styles;
    std::vector<SubtitleCue> cues;
    std::vector<double> starts;   // parallel to cues; sorted, for binary search
    int offsetMs=0;               // viewer offset (per track, not persisted)
    double scriptWidth=1920,scriptHeight=1080; // ASS PlayRes, for \pos scaling
    bool usable() const {return !cues.empty();}
    void rebuildIndex();
};

// Format is chosen from the file extension first, then sniffed from content.
SubtitleTrack loadSubtitleFile(const std::wstring& path);

// Text subtitle tracks inside the container (Matroska/MP4/...).
std::vector<SubtitleTrack> loadEmbeddedSubtitleTracks(const std::wstring& path);

// Cues overlapping `seconds`, newest first, at most `limit`; binary searched.
std::vector<const SubtitleCue*> cuesAt(const SubtitleTrack&,double seconds,size_t limit=4);
std::wstring textAt(const SubtitleTrack&,double seconds);

// Constant-offset alignment: correlates the file's speech activity with the
// track's cue activity. Returns the shift to ADD to cue timestamps.
struct SubtitleAlignResult { bool ok=false; int offsetMs=0; double score=0; std::wstring detail; };
SubtitleAlignResult alignSubtitleToAudio(const std::wstring& mediaPath,const SubtitleTrack&,int maxShiftSeconds=30);

// Legacy helpers kept for the existing call sites/tests.
std::vector<SubtitleCue> loadSrt(const std::wstring& path);
std::wstring subtitleAt(const std::vector<SubtitleCue>&,double seconds);

// True when the codec name is a text subtitle format we can turn into cues.
bool isTextSubtitleCodec(const std::wstring& codec);
} // namespace veyra::engine
