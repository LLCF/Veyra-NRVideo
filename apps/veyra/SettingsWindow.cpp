#include "SettingsWindow.h"
#include "ui/Theme.h"
#include "ui/SettingHelp.h"
#include "ui/UiPreferenceStore.h"
#include <commdlg.h>
#include "veyra/engine/PresetStore.h"
#include "veyra/engine/ColorLookStore.h"
#include "veyra/engine/ColorLut.h"
#include "veyra/RuntimePaths.h"
#include "veyra/gfx/XessMfgUnlock.h"
#include <filesystem>
#include <format>
#include <sstream>
#include <iomanip>
#include <array>
#include <cmath>
namespace veyra::ui {
namespace {
HWND window=nullptr,body=nullptr;engine::EngineController* controller=nullptr;HFONT font=nullptr;
std::function<bool(engine::EnhancementSettings)> apply;
engine::PresetStore store(runtime::localDataDirectory()/"user-presets.v1");bool loaded=false,dirty=false,populating=false,enhancementEnabled=true;int page=0,scroll=0,contentHeight=0;uint64_t displayedRevision=0;engine::EnhancementSettings configuredSettings;
std::wstring displayedBackendWarning;
engine::EnhancementSettings displayedSettings;
bool smoothMotionHelpExpanded=false;
constexpr auto smoothMotionHelp=L"只用 Smooth Motion\n"
    L"1. 本页补帧倍率选择“关闭补帧”。NR、超分照常使用。\n"
    L"2. NVIDIA App → 图形 → 选择当前使用的 Veyra.exe → AI 插帧 → 开。找不到程序时手动添加。\n"
    L"3. 应用后重启播放器。以前给实验版 EXE 开启的设置，需要为当前程序重新设置。\n\n"
    L"切换与叠加\n"
    L"只用 DLSS / XeSS：去 NVIDIA App 关闭 AI 插帧，重启后在这里选择补帧方式和倍率。\n"
    L"也允许双方同时开启。叠加效果尚未验证，不保证更好；可能增加重影、延迟或 GPU 负担，不合适就关掉一层。\n\n"
    L"注意事项\n"
    L"• 软件的“关闭补帧”和总增强开关，不会关闭驱动 AI 插帧。\n"
    L"• 面板 FPS、耗时、队列不包含驱动生成部分，不能据此判断驱动是否生效，也不要直接把 FPS 乘二。\n"
    L"• 驱动额外延迟未测量，音画同步需实测。截图、导出不含驱动生成的帧；直播录制是否捕获到它们也需另测。\n"
    L"• 功能可用性以 NVIDIA App、显卡和驱动支持为准。";
struct Item{HWND h;int page,x,y,w,height;bool hidden=false;};std::vector<Item> items;
HWND item(int id){for(auto& entry:items)if(GetDlgCtrlID(entry.h)==id)return entry.h;return nullptr;}
LRESULT send(int id,UINT message,WPARAM w=0,LPARAM l=0){return SendMessageW(item(id),message,w,l);}
void putText(int id,const wchar_t* value){setText(item(id),value);}
void check(int id,UINT value){if(send(id,BM_GETCHECK)!=value)send(id,BM_SETCHECK,value);}
UINT checked(int id){return UINT(send(id,BM_GETCHECK));}
int viewportHeight(){RECT r{};GetClientRect(body,&r);return std::max(1,MulDiv(r.bottom,96,veyra::ui::layoutDpi(window)));}
void arrange();
LRESULT CALLBACK bodyProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){PaintBuffer paint(h);fillSurface(paint.dc,paint.rect,h);if(contentHeight>viewportHeight()){int thumb=std::max(dip(h,30),int(paint.rect.bottom*float(viewportHeight())/contentHeight));int y=int((paint.rect.bottom-thumb)*float(scroll)/std::max(1,contentHeight-viewportHeight()));RECT bar{paint.rect.right-dip(h,5),y,paint.rect.right-dip(h,2),y+thumb};roundRect(paint.dc,bar,line,dip(h,2));}return 0;}
    if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORLISTBOX||msg==WM_CTLCOLORBTN)return colors(msg,wp,lp);
    if(msg==WM_COMMAND||msg==WM_HSCROLL||msg==WM_MOUSEWHEEL||msg==WM_VSCROLL||msg==WM_NOTIFY)return SendMessageW(window,msg,wp,lp);
    if(msg==WM_LBUTTONDOWN){RECT r{};GetClientRect(h,&r);if(GET_X_LPARAM(lp)>=r.right-dip(h,12))SetCapture(h);}
    if((msg==WM_LBUTTONDOWN||msg==WM_MOUSEMOVE)&&GetCapture()==h){RECT r{};GetClientRect(h,&r);scroll=int(float(GET_Y_LPARAM(lp))/std::max(1L,r.bottom)*std::max(0,contentHeight-viewportHeight()));arrange();return 0;}
    if(msg==WM_LBUTTONUP&&GetCapture()==h){ReleaseCapture();return 0;}
    return DefWindowProcW(h,msg,wp,lp);
}

const wchar_t* labels[]={L"模型强度",L"局部明暗",L"局部结构",L"肤质 · 未证实",L"风格 · 实验",L"自动遮罩 · 实验",L"UI修正 · 未证实",L"总变化强度",L"暗化变化",L"亮化变化",L"色彩变化",L"明度变化"};
void loadStore(){if(!loaded){store.load();loaded=true;}}
// ---------------------------------------------------------------------------
// Colour page (plan v4). Sections collapse like an accordion; the rows are laid
// out sequentially in layoutColorPage() so a collapsed section simply skips its
// rows instead of relying on static y offsets. The fold mask is persisted in
// ui-preferences.v1 with the rest of the UI state.
// ---------------------------------------------------------------------------
constexpr int kColorSections=7;
void message(const std::wstring& text);
bool submit(engine::EnhancementSettings s);
uint32_t colorFoldMask=0;
std::wstring colorSectionName(int section){
    static const wchar_t* names[kColorSections]={L"亮",L"颜色",L"曲线",L"混色器",L"颜色分级",L"校准",L"LUT"};
    return section>=0&&section<kColorSections?names[section]:L"色彩";
}
// Which field of ColorSettings a row edits. Scalars use the member pointer;
// array-valued controls (mixer bands, grading wheels, calibration primaries)
// use the target + index pair.
enum class ColorTarget {
    Scalar,MixerHue,MixerSaturation,MixerLuminance,BlackWhiteMix,
    GradingHue,GradingSaturation,GradingLuminance,CalibrationHue,CalibrationSaturation
};
struct ColorParam{
    int section;
    const wchar_t* label;
    float min,max;
    ColorTarget target;
    float engine::ColorSettings::*field;
    int index;
    float value(const engine::ColorSettings& colour)const{
        switch(target){
        case ColorTarget::MixerHue:return colour.mixerHue[std::size_t(index)];
        case ColorTarget::MixerSaturation:return colour.mixerSaturation[std::size_t(index)];
        case ColorTarget::MixerLuminance:return colour.mixerLuminance[std::size_t(index)];
        case ColorTarget::BlackWhiteMix:return colour.blackWhiteMix[std::size_t(index)];
        case ColorTarget::GradingHue:return colour.grading[std::size_t(index)].hue;
        case ColorTarget::GradingSaturation:return colour.grading[std::size_t(index)].saturation;
        case ColorTarget::GradingLuminance:return colour.grading[std::size_t(index)].luminance;
        case ColorTarget::CalibrationHue:return colour.calibrationHue[std::size_t(index)];
        case ColorTarget::CalibrationSaturation:return colour.calibrationSaturation[std::size_t(index)];
        case ColorTarget::Scalar:break;
        }
        return field?colour.*field:0.0f;
    }
    void set(engine::ColorSettings& colour,float v)const{
        switch(target){
        case ColorTarget::MixerHue:colour.mixerHue[std::size_t(index)]=v;return;
        case ColorTarget::MixerSaturation:colour.mixerSaturation[std::size_t(index)]=v;return;
        case ColorTarget::MixerLuminance:colour.mixerLuminance[std::size_t(index)]=v;return;
        case ColorTarget::BlackWhiteMix:colour.blackWhiteMix[std::size_t(index)]=v;return;
        case ColorTarget::GradingHue:colour.grading[std::size_t(index)].hue=v;return;
        case ColorTarget::GradingSaturation:colour.grading[std::size_t(index)].saturation=v;return;
        case ColorTarget::GradingLuminance:colour.grading[std::size_t(index)].luminance=v;return;
        case ColorTarget::CalibrationHue:colour.calibrationHue[std::size_t(index)]=v;return;
        case ColorTarget::CalibrationSaturation:colour.calibrationSaturation[std::size_t(index)]=v;return;
        case ColorTarget::Scalar:break;
        }
        if(field)colour.*field=v;
    }
};
std::vector<ColorParam> colorParams;
// id layout: master 800, reset 801, undo 802, header 810+s, label 830+i,
// edit 850+i, slider 870+i.
// The colour page owns 1200..1499: label/edit/slider per parameter, so the four
// remaining sections (mixer 8x3, grading, calibration, curves) fit without
// colliding with the enhancement page's ids.
constexpr int colorLabelId(int i){return 1200+i;}
constexpr int colorEditId(int i){return 1300+i;}
constexpr int colorSliderId(int i){return 1400+i;}
constexpr int kColorMaxParams=100;
engine::ColorSettings colourTarget(){
    return (enhancementEnabled?controller->snapshot().desired:configuredSettings).color;
}
engine::ColorSettings colourUndo;
bool colourUndoValid=false;
int colorPageContentHeight=0;
bool syncingColour=false;
// Named colour looks + the .cube list shown in the colour page.
std::vector<std::wstring> colourLookNames;
std::vector<std::wstring> colourLutNames;
int selectedColourLook=-1;
// Rebuild the preset combo box. `select` re-selects a named look (used after
// saving/importing so the new preset stays highlighted instead of silently
// falling back to "no preset selected").
void refreshColourLooks(const std::wstring& select={}){
    engine::ColorLookStore lookStore(runtime::localDataDirectory());
    lookStore.load();
    colourLookNames.clear();
    // The controls live on the scrolling `body` panel, not directly on `window`,
    // so they must be addressed through their own handle (SendDlgItemMessageW
    // only walks direct children and silently left this combo empty).
    const auto combo=item(803);
    if(!window||!combo)return;
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    SendMessageW(combo,CB_ADDSTRING,0,LPARAM(L"（未选择预设）"));
    int selection=0;
    for(const auto& look:lookStore.entries()){
        colourLookNames.push_back(look.name);
        SendMessageW(combo,CB_ADDSTRING,0,LPARAM(look.name.c_str()));
        if(!select.empty()&&look.name==select)selection=int(colourLookNames.size());
    }
    SendMessageW(combo,CB_SETCURSEL,WPARAM(selection),0);
    selectedColourLook=selection-1;
}
void refreshColourLuts(){
    engine::ColorLutStore lutStore(runtime::localDataDirectory());
    colourLutNames=lutStore.list();
    const auto combo=item(817);
    if(!window||!combo)return;
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    SendMessageW(combo,CB_ADDSTRING,0,LPARAM(L"不使用 LUT"));
    for(const auto& name:colourLutNames)SendMessageW(combo,CB_ADDSTRING,0,LPARAM(name.c_str()));
    int selection=0;
    const auto current=colourTarget().lutNameString();
    for(size_t i=0;i<colourLutNames.size();++i)if(colourLutNames[i]==current)selection=int(i)+1;
    SendMessageW(combo,CB_SETCURSEL,WPARAM(selection),0);
}
std::wstring windowText(HWND h){wchar_t buffer[256]{};GetWindowTextW(h,buffer,256);return buffer;}
void loadColourFoldState(){
    const auto preferences=veyra::ui::UiPreferenceStore(runtime::localDataDirectory()).load();
    colorFoldMask=preferences.colourFoldMask;
}
void saveColourFoldState(){
    veyra::ui::UiPreferenceStore store_(runtime::localDataDirectory());
    auto preferences=store_.load();
    preferences.colourFoldMask=colorFoldMask;
    if(!store_.save(preferences,nullptr))veyra::log::warn("color-ui","fold state not saved");
}
void layoutColorPage(){
    auto place=[&](int id,int y,int height,bool hidden){
        for(auto& entry:items)if(GetDlgCtrlID(entry.h)==id){entry.y=y;entry.height=height;entry.hidden=hidden;return;}
    };
    int y=12;
    place(800,y,36,false);y+=42;
    // Preset toolbar: which look, a name to save under, and the actions.
    place(803,y,200,false);place(804,y,180,false);y+=38;
    place(805,y,32,false);place(806,y,32,false);place(807,y,32,false);place(808,y,32,false);place(809,y,32,false);y+=40;
    place(801,y,32,false);place(802,y,32,false);y+=40;
    for(int section=0;section<kColorSections;++section){
        const bool collapsed=(colorFoldMask>>section)&1u;
        place(810+section,y,32,false);
        const auto title=std::format(L"{}  {}",collapsed?L"▸":L"▾",colorSectionName(section));
        putText(810+section,title.c_str());
        y+=38;
        for(size_t i=0;i<colorParams.size();++i){
            const auto& param=colorParams[i];
            if(param.section!=section)continue;
            place(colorLabelId(int(i)),y,24,collapsed);
            place(colorEditId(int(i)),y-2,26,collapsed);
            place(colorSliderId(int(i)),y+24,16,collapsed);
            if(!collapsed)y+=46;
        }
        if(section==6){
            place(817,y,200,collapsed);y+=32;
            place(818,y,32,collapsed);y+=36;
            place(819,y,200,collapsed);y+=32;
        }
        // The black & white mixer is a mode, not a slider: one switch in the
        // mixer section turns the eight 黑白 rows on (HSL rows go inert, exactly
        // like Lightroom's B&W panel).
        if(section==3){place(820,y,220,collapsed);y+=32;}
        y+=8;
    }
    colorPageContentHeight=y+12;
}
void syncColorControls(){
    syncingColour=true;
    const auto colour=colourTarget();
    check(800,colour.enabled?BST_CHECKED:BST_UNCHECKED);
    for(size_t i=0;i<colorParams.size();++i){
        const float value=colorParams[i].value(colour);
        const auto text=std::format(L"{:.2f}",value);
        if(auto edit=item(colorEditId(int(i))))if(windowText(edit)!=text)putText(colorEditId(int(i)),text.c_str());
        if(auto slider=item(colorSliderId(int(i)))){
            const int scaled=int(std::lround(value*100.0f));
            if(SendMessageW(slider,TBM_GETPOS,0,0)!=scaled)SendMessageW(slider,TBM_SETPOS,TRUE,LPARAM(scaled));
        }
    }
    if(auto space=item(819))if(int(SendMessageW(space,CB_GETCURSEL,0,0))!=colour.lutInputSpace)SendMessageW(space,CB_SETCURSEL,WPARAM(colour.lutInputSpace),0);
    check(820,colour.blackWhite?BST_CHECKED:BST_UNCHECKED);
    syncingColour=false;
}
bool applyColour(const engine::ColorSettings& colour,bool autoEnable){
    auto settings=enhancementEnabled?controller->snapshot().desired:configuredSettings;
    const auto previous=settings.color;
    settings.color=colour;
    if(autoEnable)settings.color.enabled=true;
    if(!(settings.color==previous)&&!submit(settings))return false;
    return true;
}
// Native file pickers for the colour page. They run modal on the UI thread, which
// is what a settings dialog is expected to do.
std::wstring pickColourFile(const wchar_t* filter,const wchar_t* title){
    wchar_t buffer[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner=window;dialog.lpstrFilter=filter;dialog.lpstrFile=buffer;dialog.nMaxFile=32768;
    dialog.lpstrTitle=title;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&dialog))return {};
    return buffer;
}
std::wstring pickColourSave(const wchar_t* filter,const wchar_t* title,const wchar_t* suggested){
    wchar_t buffer[32768]{};if(suggested)wcsncpy_s(buffer,suggested,_TRUNCATE);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner=window;dialog.lpstrFilter=filter;dialog.lpstrFile=buffer;dialog.nMaxFile=32768;
    dialog.lpstrTitle=title;dialog.lpstrDefExt=L"vpcolor";dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetSaveFileNameW(&dialog))return {};
    return buffer;
}bool colourFieldEdited(int index,float value){
    if(index<0||size_t(index)>=colorParams.size())return false;
    auto colour=colourTarget();
    if(!std::isfinite(value)||value<colorParams[size_t(index)].min||value>colorParams[size_t(index)].max){
        message(L"数值超出范围；仍使用上次有效值");syncColorControls();return false;
    }
    colorParams[size_t(index)].set(colour,value);
    if(!applyColour(colour,true)){
        veyra::log::warn("color-ui",std::format("colour edit rejected index={} value={:.3f}",index,value));
        syncColorControls();
        return false;
    }
    veyra::log::info("color-ui",std::format("colour edit applied index={} value={:.3f}",index,value));
    syncColorControls();
    return true;
}
void message(const std::wstring& text){putText(401,text.c_str());}
bool submit(engine::EnhancementSettings s){if(!apply(s)){dirty=true;message(L"总增强正在切换；本次修改未接受，请稍后重试。");return false;}dirty=false;return true;}
void syncProtection(const engine::ProtectionSettings& protection){
    check(206,protection.enabled?BST_CHECKED:BST_UNCHECKED);unsigned count=0;for(auto q:protection.regions)count+=!q.empty();
    putText(206,(L"NR剔除区 · "+std::to_wstring(count)+L"/4").c_str());
    // The feather value is stored in working-extent pixels (see
    // NrResidualComposite.hlsl); the panel only converts it for display.
    const float feather=std::clamp(protection.featherPixels,0.0f,64.0f);
    if(auto slider=item(622))SendMessageW(slider,TBM_SETPOS,TRUE,LPARAM(std::lround(feather)));
    if(item(222))putText(222,std::to_wstring(int(std::lround(feather))).c_str());
    if(auto label=item(1123)){
        const unsigned extent=controller?controller->snapshot().metrics.resolution.base.height:0u;
        if(extent>0)putText(1123,std::format(L"羽化 {} px · ≈{:.1f}% 画面高度",int(std::lround(feather)),feather*100.0f/float(extent)).c_str());
        else putText(1123,std::format(L"羽化 {} px（工作分辨率像素）",int(std::lround(feather))).c_str());
    }
}
void selectDiscrete(int group,int value){for(int j=0;j<(group==0?3:2);++j){auto h=item(700+group*10+j);if(j==value)SetPropW(h,L"veyra.selected",HANDLE(1));else RemovePropW(h,L"veyra.selected");InvalidateRect(h,nullptr,FALSE);}}
// Multiplier list is capability-driven: the DLSS runtime reports how many
// generated frames it supports (1 = 2X only on Ada, 5 = 6X on Blackwell), and
// the XeSS unlock path raises its own ceiling. Unknown capability offers the
// full list; the engine gate rejects an unsupported request with a message.
int multiplierChoiceCount(engine::FrameGenerationBackend backend){
    int cap=6;
    if(backend==engine::FrameGenerationBackend::XeSS){
        // Stock provider is 2X only; the audited OptiScaler unlock raises it to
        // 4X. Hash the provider once, and prefer the ceiling an actual session
        // already reported.
        static const bool providerAudited=[](){
            const auto path=veyra::runtime::localDataDirectory()/L"intel"/L"experimental"/L"libxess_fg.dll";
            return veyra::gfx::XessMfgUnlock::providerIsAudited(path.wstring());
        }();
        cap=providerAudited?4:2;
        if(controller){const auto snapshot=controller->snapshot();if(snapshot.xessMaxInterpolatedFrames>1)cap=std::clamp(snapshot.xessMaxInterpolatedFrames+1,2,4);}
    }
    else if(backend==engine::FrameGenerationBackend::Fsr){
        // AMD 3.1.x frame generation delivers one generated frame per present;
        // tools/fsr_probe measured the same count for 2/3/4 requested frames.
        cap=2;
        if(controller){const auto snapshot=controller->snapshot();if(snapshot.fsrMaxGeneratedFrames>0)cap=std::clamp(int(snapshot.fsrMaxGeneratedFrames)+1,2,2);}
    }
    else if(controller){const auto snapshot=controller->snapshot();if(snapshot.fgMultiFrameMax>0)cap=std::clamp(snapshot.fgMultiFrameMax+1,2,6);}
    int count=1;
    for(size_t i=1;i<engine::kFgMultiplierChoiceCount;++i)if(int(engine::kFgMultiplierChoices[i])<=cap)++count;
    return count;
}
int multiplierChoiceIndex(uint32_t multiplier){for(size_t i=0;i<engine::kFgMultiplierChoiceCount;++i)if(engine::kFgMultiplierChoices[i]==multiplier)return int(i);return 0;}
void populate(engine::EnhancementSettings s){
    populating=true;
    float v[]={s.model.intensity,s.model.tone,s.model.structure,s.model.skin,float(s.model.style),float(s.model.autoMask),float(s.model.uiCorrection),s.residual.total,s.residual.darken,s.residual.brighten,s.residual.color,s.residual.luminance};
    for(int i=0;i<12;++i){std::wostringstream o;o<<std::setprecision(7)<<v[i];if(i>=4&&i<=6){selectDiscrete(i-4,int(v[i]));continue;}if(GetFocus()!=item(100+i))putText(100+i,o.str().c_str());send(600+i,TBM_SETPOS,TRUE,LPARAM(v[i]*100));}
    syncProtection(s.protection);
    check(200,enhancementEnabled&&s.nr?BST_CHECKED:BST_UNCHECKED);
    check(201,enhancementEnabled&&s.sr?BST_CHECKED:BST_UNCHECKED);
    for(int j=0;j<3;++j){auto h=item(730+j);if(j==int(s.srTarget))SetPropW(h,L"veyra.selected",HANDLE(1));else RemovePropW(h,L"veyra.selected");InvalidateRect(h,nullptr,FALSE);}
    const int multiplierCount=multiplierChoiceCount(s.frameGenerationBackend);
    if(send(202,CB_GETCOUNT)!=multiplierCount){send(202,CB_RESETCONTENT);const wchar_t* choices[]={L"关闭补帧",L"2X · 一张中间帧",L"3X · 两张中间帧",L"4X · 三张中间帧",L"6X · 五张中间帧"};for(int i=0;i<multiplierCount;++i)send(202,CB_ADDSTRING,0,LPARAM(choices[i]));}
    send(207,CB_SETCURSEL,s.videoSrQuality,0);send(202,CB_SETCURSEL,multiplierChoiceIndex(s.multiplier),0);
    send(208,CB_SETCURSEL,int(s.frameGenerationBackend),0);send(203,CB_SETCURSEL,int(s.nrPolicy),0);
    send(508,CB_SETCURSEL,int(engine::exportBitrateIndex(s.exportBitrateMbps)),0);
    send(218,CB_SETCURSEL,int(s.nrRuntime));
    check(219,s.captureCompatible?BST_CHECKED:BST_UNCHECKED);check(220,s.lowLatency?BST_CHECKED:BST_UNCHECKED);
    send(204,CB_SETCURSEL,int(s.flow),0);send(205,CB_SETCURSEL,int(s.content),0);
    send(209,CB_SETCURSEL,int(s.opticalFlowBackend),0);
    check(215,s.amdFlowHalfResolution?BST_CHECKED:BST_UNCHECKED);
    send(216,CB_SETCURSEL,int(s.audioSync));
    if(GetFocus()!=item(217))putText(217,std::to_wstring(s.audioOffsetMs).c_str());
    EnableWindow(item(217),s.audioSync==engine::AudioSyncMode::Manual);
    EnableWindow(item(204),s.opticalFlowBackend==engine::OpticalFlowBackend::Nvidia);
    EnableWindow(item(215),s.opticalFlowBackend==engine::OpticalFlowBackend::AmdFidelityFx);
    displayedRevision=s.revision;displayedSettings=s;populating=false;dirty=false;
    syncColorControls();
}
bool read(engine::EnhancementSettings& s,bool allPages=false){s=enhancementEnabled?controller->snapshot().desired:configuredSettings;float v[12]{};for(int i=0;i<12;++i){if(i>=4&&i<=6){v[i]=float(i==4?s.model.style:i==5?s.model.autoMask:s.model.uiCorrection);continue;}wchar_t b[64]{};GetWindowTextW(item(100+i),b,64);wchar_t* end=nullptr;v[i]=wcstof(b,&end);if(end==b||*end||!std::isfinite(v[i])){message(L"请输入完整的有限数值；未提交设置");return false;}}
    for(int i=4;i<7;++i)if(v[i]!=std::floor(v[i])||v[i]<0||v[i]>(i==4?2:1)){message(L"风格/遮罩/UI修正必须为整数");return false;}
    s.model={v[0],v[1],v[2],v[3],int(v[4]),int(v[5]),int(v[6])};s.residual={v[7],v[8],v[9],v[10],v[11]};if(enhancementEnabled){s.nr=checked(200)==BST_CHECKED;s.sr=checked(201)==BST_CHECKED;}s.videoSrQuality=uint32_t(send(207,CB_GETCURSEL,0,0));s.nrPolicy=static_cast<pipeline::NrSizePolicy>(send(203,CB_GETCURSEL,0,0));if(allPages){
        const auto multiplier=send(202,CB_GETCURSEL,0,0),generation=send(208,CB_GETCURSEL,0,0),flowBackend=send(209,CB_GETCURSEL,0,0),flowQuality=send(204,CB_GETCURSEL,0,0),content=send(205,CB_GETCURSEL,0,0);
        if(multiplier==CB_ERR||generation==CB_ERR||flowBackend==CB_ERR||flowQuality==CB_ERR||content==CB_ERR){message(L"设置控件未完成初始化；未保存设置");return false;}
        s.multiplier=(multiplier>=0&&multiplier<int(engine::kFgMultiplierChoiceCount))?engine::kFgMultiplierChoices[multiplier]:1;s.frameGenerationBackend=static_cast<engine::FrameGenerationBackend>(generation);s.opticalFlowBackend=static_cast<engine::OpticalFlowBackend>(flowBackend);s.amdFlowHalfResolution=checked(215)==BST_CHECKED;s.flow=static_cast<engine::FlowQuality>(flowQuality);s.content=static_cast<engine::ContentRate>(content);
        {const int bitrate=send(508,CB_GETCURSEL,0,0);if(bitrate==CB_ERR||bitrate<0||bitrate>=int(engine::kExportBitrateChoiceCount)){message(L"导出码率控件未完成初始化；未保存设置");return false;}s.exportBitrateMbps=engine::kExportBitrateChoices[bitrate];}
        s.audioSync=static_cast<engine::AudioSyncMode>(send(216,CB_GETCURSEL));
        s.nrRuntime=static_cast<engine::NrRuntime>(send(218,CB_GETCURSEL));
        s.captureCompatible=checked(219)==BST_CHECKED;s.lowLatency=checked(220)==BST_CHECKED;
        wchar_t offset[32]{};GetWindowTextW(item(217),offset,32);wchar_t* offsetEnd=nullptr;const auto parsed=wcstol(offset,&offsetEnd,10);
        if(offsetEnd==offset||*offsetEnd||parsed<-250||parsed>250){message(L"声音偏移须为 -250 至 250 ms");return false;}s.audioOffsetMs=int(parsed);
        for(int j=0;j<3;++j)if(GetPropW(item(730+j),L"veyra.selected")){s.srTarget=static_cast<pipeline::SrTarget>(j);break;}
        if(engine::presentSinkFrameGeneration(s.frameGenerationBackend)){
            const int choices=multiplierChoiceCount(s.frameGenerationBackend);
            const size_t index=size_t(std::clamp(choices-1,1,int(engine::kFgMultiplierChoiceCount)-1));
            s.multiplier=std::min(s.multiplier,engine::kFgMultiplierChoices[index]);
        }
    }if(!s.validate().empty()){message(L"参数越界，未提交。悬停数值框查看允许范围。");return false;}return true;}

// Each notification changes one field on the latest desired settings. Hidden
// controls and incomplete numeric text can never overwrite another field.
bool liveField(int id){
    if(id==202){
        const auto index=send(id,CB_GETCURSEL);if(index==CB_ERR)return false;
        const uint32_t requested=(index>=0&&index<int(engine::kFgMultiplierChoiceCount))?engine::kFgMultiplierChoices[index]:1;
        const bool accepted=SendMessageW(GetParent(window),WM_APP+44,202,requested)!=0;
        populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);
        message(accepted?L"已请求补帧；无需先开启NR。":L"总增强正在切换，请待当前事务完成。");return accepted;
    }
    auto s=enhancementEnabled?controller->snapshot().desired:configuredSettings;
    if(id>=100&&id<=111){
        wchar_t b[64]{};GetWindowTextW(item(id),b,64);wchar_t* end=nullptr;float v=wcstof(b,&end);
        if(end==b||*end||!std::isfinite(v)){message(L"数值未完整；仍使用上次有效值");return false;}
        switch(id){case 100:s.model.intensity=v;break;case 101:s.model.tone=v;break;case 102:s.model.structure=v;break;case 103:s.model.skin=v;break;
        case 107:s.residual.total=v;break;case 108:s.residual.darken=v;break;case 109:s.residual.brighten=v;break;case 110:s.residual.color=v;break;case 111:s.residual.luminance=v;break;default:return false;}
    }else switch(id){
        case 219:s.captureCompatible=checked(id)==BST_CHECKED;break;
        case 218:s.nrRuntime=static_cast<engine::NrRuntime>(send(id,CB_GETCURSEL));break;
        case 203:s.nrPolicy=static_cast<pipeline::NrSizePolicy>(send(id,CB_GETCURSEL));break;
        case 204:s.flow=static_cast<engine::FlowQuality>(send(id,CB_GETCURSEL));break;
        case 205:s.content=static_cast<engine::ContentRate>(send(id,CB_GETCURSEL));break;
        case 208:{s.frameGenerationBackend=static_cast<engine::FrameGenerationBackend>(send(id,CB_GETCURSEL));if(engine::presentSinkFrameGeneration(s.frameGenerationBackend)){const int choices=multiplierChoiceCount(s.frameGenerationBackend);const size_t index=size_t(std::clamp(choices-1,1,int(engine::kFgMultiplierChoiceCount)-1));s.multiplier=std::min(s.multiplier,engine::kFgMultiplierChoices[index]);}break;}
        case 209:s.opticalFlowBackend=static_cast<engine::OpticalFlowBackend>(send(id,CB_GETCURSEL));break;
        case 220:s.lowLatency=checked(id)==BST_CHECKED;break;
        case 215:s.amdFlowHalfResolution=checked(id)==BST_CHECKED;break;
        case 216:s.audioSync=static_cast<engine::AudioSyncMode>(send(id,CB_GETCURSEL));break;
        case 217:{wchar_t value[32]{};GetWindowTextW(item(id),value,32);wchar_t* end=nullptr;const auto parsed=wcstol(value,&end,10);if(end==value||*end||parsed<-250||parsed>250){message(L"声音偏移须为 -250 至 250 ms");return false;}s.audioOffsetMs=int(parsed);break;}
        case 222:{wchar_t value[32]{};GetWindowTextW(item(id),value,32);wchar_t* end=nullptr;const auto parsed=wcstol(value,&end,10);if(end==value||*end||parsed<0||parsed>64){message(L"剔除区羽化须为 0 至 64 像素");return false;}s.protection.featherPixels=float(parsed);break;}
        case 207:s.videoSrQuality=uint32_t(send(id,CB_GETCURSEL));break;
        case 508:{const int index=send(id,CB_GETCURSEL,0,0);if(index==CB_ERR||index<0||index>=int(engine::kExportBitrateChoiceCount))return false;s.exportBitrateMbps=engine::kExportBitrateChoices[index];break;}
        case 700:case 701:case 702:s.model.style=id-700;break;
        case 710:case 711:s.model.autoMask=id-710;break;
        case 720:case 721:s.model.uiCorrection=id-720;break;
        case 730:case 731:case 732:s.srTarget=static_cast<pipeline::SrTarget>(id-730);break;
        default:return false;
    }
    if(!s.validate().empty()){message(L"数值超出范围；仍使用上次有效值");return false;}
    if(!submit(s)){populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);return false;}
    populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);
    message(enhancementEnabled?L"实时生效 · 以已应用版本为准":L"增强关闭中 · 已保存待启用设置");return true;
}
LRESULT CALLBACK scrollOnly(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(msg==WM_MOUSEWHEEL||msg==WM_MOUSEHWHEEL){if(window)SendMessageW(window,WM_MOUSEWHEEL,wp,lp);return 0;}
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(h,scrollOnly,id);
    return DefSubclassProc(h,msg,wp,lp);
}

void arrange(){
    if(!window||!body)return;RECT r{};GetClientRect(window,&r);int width=MulDiv(r.right,96,veyra::ui::layoutDpi(window)),height=MulDiv(r.bottom,96,veyra::ui::layoutDpi(window));
    const int sticky=128,viewport=std::max(1,height-sticky);contentHeight=0;
    if(page==2)layoutColorPage();
    int helpHeight=0;
    if(smoothMotionHelpExpanded){auto dc=GetDC(window);auto old=SelectObject(dc,font);RECT textRect{0,0,dip(window,std::max(1,width-24)),0};DrawTextW(dc,smoothMotionHelp,-1,&textRect,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(window,dc);helpHeight=MulDiv(textRect.bottom,96,layoutDpi(window))+16;}
    const auto helpOffset=[&](const Item& entry){const auto id=GetDlgCtrlID(entry.h);return entry.page==1&&(id==1114||id==205||id==1110)?helpHeight:0;};
    for(auto& entry:items){if(GetDlgCtrlID(entry.h)==1120)entry.height=helpHeight;
        if(entry.page==page&&!entry.hidden&&(GetDlgCtrlID(entry.h)!=1120||smoothMotionHelpExpanded)){wchar_t cls[32]{};GetClassNameW(entry.h,cls,32);contentHeight=std::max(contentHeight,entry.y+helpOffset(entry)+(_wcsicmp(cls,L"COMBOBOX")==0?36:entry.height)+12);}}
    if(page==2)contentHeight=std::max(contentHeight,colorPageContentHeight);
    scroll=std::clamp(scroll,0,std::max(0,contentHeight-viewport));
    SetWindowPos(body,nullptr,0,dip(window,sticky),r.right,dip(window,viewport),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
    auto batch=BeginDeferWindowPos(int(items.size()));
    for(auto& entry:items){bool fixed=entry.page==-1,visible=!entry.hidden&&(entry.page==page||fixed)&&(GetDlgCtrlID(entry.h)!=1120||smoothMotionHelpExpanded);int w=entry.w<0?width-entry.x-12:entry.w;int y=fixed?(GetDlgCtrlID(entry.h)==400?0:(GetDlgCtrlID(entry.h)==211||GetDlgCtrlID(entry.h)==219)?86:42):entry.y+helpOffset(entry)-scroll;
        if(fixed){SetWindowPos(entry.h,nullptr,dip(window,entry.x),dip(window,y),dip(window,std::max(1,w)),dip(window,42),SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOREDRAW);continue;}batch=DeferWindowPos(batch,entry.h,nullptr,dip(window,entry.x),dip(window,y),dip(window,std::max(1,w)),dip(window,entry.height),SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCOPYBITS|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));}
    EndDeferWindowPos(batch);RedrawWindow(body,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);InvalidateRect(window,nullptr,FALSE);
}

HWND add(const wchar_t* cls,const wchar_t* text,int id,DWORD style,int group,int x,int y,int width,int height){auto h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|style,0,0,1,1,group==-1?window:body,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,WPARAM(font),TRUE);themeControl(h);if(group!=-1)SetWindowSubclass(h,scrollOnly,950,0);items.push_back({h,group,x,y,width,height});return h;}
void button(const wchar_t* title,int id,int group,int x,int y,int width=-1){add(L"BUTTON",title,id,BS_PUSHBUTTON|WS_TABSTOP,group,x,y,width,36);}
void combo(int id,int group,int y,std::initializer_list<const wchar_t*> names){auto h=add(L"COMBOBOX",L"",id,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,group,12,y,-1,200);for(auto name:names)SendMessageW(h,CB_ADDSTRING,0,LPARAM(name));}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_COMMAND&&LOWORD(wp)==221&&HIWORD(wp)==BN_CLICKED){smoothMotionHelpExpanded=!smoothMotionHelpExpanded;putText(221,smoothMotionHelpExpanded?L"Smooth Motion · 收起说明 ▴":L"Smooth Motion · 开启方法 ▾");arrange();return 0;}
    if(msg==WM_COMMAND&&!populating&&LOWORD(wp)==220&&HIWORD(wp)==BN_CLICKED){liveField(220);return 0;}
    if(msg==WM_COMMAND&&!populating&&LOWORD(wp)==219&&HIWORD(wp)==BN_CLICKED){liveField(219);return 0;}
    if(msg==WM_COMMAND&&!populating&&LOWORD(wp)==218&&HIWORD(wp)==CBN_SELCHANGE){liveField(218);return 0;}
    if(msg==WM_COMMAND&&!populating&&((LOWORD(wp)==216&&HIWORD(wp)==CBN_SELCHANGE)||(LOWORD(wp)==217&&HIWORD(wp)==EN_CHANGE))){liveField(LOWORD(wp));return 0;}
    if(msg==WM_COMMAND&&!populating&&LOWORD(wp)==222&&HIWORD(wp)==EN_CHANGE){liveField(222);return 0;}
    if(msg==WM_COMMAND&&!populating&&LOWORD(wp)==222&&HIWORD(wp)==EN_KILLFOCUS){populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);return 0;}
    // --- colour page (plan v4) ---------------------------------------------
    if(msg==WM_COMMAND&&LOWORD(wp)==800&&HIWORD(wp)==BN_CLICKED){
        auto settings=enhancementEnabled?controller->snapshot().desired:configuredSettings;
        settings.color.enabled=SendMessageW(item(800),BM_GETCHECK,0,0)==BST_CHECKED;
        if(!submit(settings)){syncColorControls();return 0;}
        message(settings.color.enabled?L"调色已开启：链路在所有效果器之前，会有一点额外开销。":L"调色已关闭：这条链完全不存在，零开销。");
        return 0;
    }
    if(msg==WM_COMMAND&&(LOWORD(wp)==801||LOWORD(wp)==802)&&HIWORD(wp)==BN_CLICKED){
        if(LOWORD(wp)==801){
            colourUndo=colourTarget();colourUndoValid=true;
            auto neutral=engine::ColorSettings{};neutral.enabled=true;
            if(applyColour(neutral,false))message(L"已还原为中性；可以点“撤销还原”找回上一组数值。");
        }else if(colourUndoValid){
            if(applyColour(colourUndo,false))message(L"已恢复还原前的数值。");
        }else message(L"没有可撤销的还原。");
        syncColorControls();arrange();return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)==820&&HIWORD(wp)==BN_CLICKED){
        auto colour=colourTarget();
        colour.blackWhite=SendMessageW(item(820),BM_GETCHECK,0,0)==BST_CHECKED;
        if(!applyColour(colour,true)){
            veyra::log::warn("color-ui","black and white mixer switch rejected");
            syncColorControls();
            return 0;
        }
        veyra::log::info("color-ui",std::format("black and white mixer={}",colour.blackWhite?1:0));
        message(colour.blackWhite?L"黑白混色器已打开：下面八个“黑白”滑块控制各色系的灰阶明暗。":L"黑白混色器已关闭，回到 HSL 混色。");
        syncColorControls();
        return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)>=810&&LOWORD(wp)<810+kColorSections&&HIWORD(wp)==BN_CLICKED){
        const int section=LOWORD(wp)-810;
        colorFoldMask^=1u<<section;
        saveColourFoldState();
        arrange();
        return 0;
    }
    // --- colour presets + the .cube picker (T5-b) ---------------------------
    if(msg==WM_COMMAND&&LOWORD(wp)>=805&&LOWORD(wp)<=809&&HIWORD(wp)==BN_CLICKED){
        const int id=LOWORD(wp);
        engine::ColorLookStore lookStore(runtime::localDataDirectory());
        if(id==805){
            wchar_t name[64]{};GetWindowTextW(item(804),name,64);
            auto colour=colourTarget();colour.enabled=true;
            if(lookStore.load()&&lookStore.put(name,colour,true)){
                refreshColourLooks(name);
                message(L"已保存色彩预设（同名会覆盖）。可以导出成 .vpcolor 分享。");
            }else message(L"保存失败："+lookStore.error());
        }else if(id==806){
            const int index=int(SendMessageW(item(803),CB_GETCURSEL,0,0));
            if(lookStore.load()&&index>=1&&size_t(index-1)<lookStore.entries().size()){
                const auto& colour=lookStore.entries()[size_t(index-1)].color;
                const bool applied=applyColour(colour,true);
                veyra::log::info("color-ui",std::format("preset apply index={} exposure={:.3f} lut={} accepted={}",
                    index,colour.exposure,colour.lutNameString().empty()?0:1,applied));
                if(applied){
                    syncColorControls();refreshColourLuts();
                    message(L"已应用该色彩预设（只改色彩，不动 NR/超分/补帧）。");
                }else message(L"设置正在切换，请稍后再试。");
            }else{
                veyra::log::info("color-ui",std::format("preset apply rejected index={} entries={}",index,lookStore.entries().size()));
                message(L"先在列表里选一个预设。");
            }
        }else if(id==807){
            const int index=int(SendMessageW(item(803),CB_GETCURSEL,0,0));
            if(lookStore.load()&&index>=1&&lookStore.erase(size_t(index-1))){
                refreshColourLooks();
                message(L"已删除该色彩预设。");
            }else message(L"删除失败："+lookStore.error());
        }else if(id==808){
            const int index=int(SendMessageW(item(803),CB_GETCURSEL,0,0));
            const auto path=pickColourSave(L"Veyra 色彩预设 (*.vpcolor)\0*.vpcolor\0所有文件 (*.*)\0*.*\0\0",L"导出色彩预设",L"look.vpcolor");
            if(!path.empty()&&lookStore.load()&&lookStore.exportFile(size_t(std::max(0,index-1)),path))message(L"已导出 .vpcolor。");
            else if(!path.empty())message(L"导出失败："+lookStore.error());
        }else{
            const auto path=pickColourFile(L"Veyra 色彩预设 (*.vpcolor)\0*.vpcolor\0所有文件 (*.*)\0*.*\0\0",L"导入色彩预设");
            if(!path.empty()){
                std::wstring name;
                if(lookStore.load()&&lookStore.importFile(path,name)){refreshColourLooks(name);message(L"已导入预设："+name);}
                else message(L"导入失败："+lookStore.error());
            }
        }
        return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)==817&&HIWORD(wp)==CBN_SELCHANGE){
        const int index=int(SendMessageW(item(817),CB_GETCURSEL,0,0));
        auto colour=colourTarget();colour.enabled=true;
        if(index<=0)colour.clearLut();
        else if(size_t(index-1)<colourLutNames.size()&&!colour.setLutName(colourLutNames[size_t(index-1)])){message(L"LUT 名字非法。");return 0;}
        if(index>0&&colour.lutStrength<=0.0f)colour.lutStrength=100.0f;
        if(!applyColour(colour,false))message(L"设置正在切换，请稍后再试。");
        else message(index<=0?L"已停用 LUT。":L"已选择 LUT；管线会重建一次，短暂停顿正常。");
        syncColorControls();
        return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)==819&&HIWORD(wp)==CBN_SELCHANGE){
        const int index=int(SendMessageW(item(819),CB_GETCURSEL,0,0));
        auto colour=colourTarget();
        colour.lutInputSpace=std::clamp(index,0,2);
        if(applyColour(colour,true))message(L"已切换 LUT 输入空间（日志会记录）。");
        return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)==818&&HIWORD(wp)==BN_CLICKED){
        const auto path=pickColourFile(L"Cube LUT (*.cube)\0*.cube\0所有文件 (*.*)\0*.*\0\0",L"导入 .cube LUT");
        if(!path.empty()){
            engine::ColorLutStore lutStore(runtime::localDataDirectory());
            std::wstring name;std::string error;
            if(lutStore.importFile(path,name,error)&&!name.empty()){
                auto colour=colourTarget();colour.enabled=true;
                colour.setLutName(name);
                if(colour.lutStrength<=0.0f)colour.lutStrength=100.0f;
                refreshColourLuts();
                if(!applyColour(colour,false))message(L"LUT 已导入，但设置正在切换；稍后重选即可。");
                else message(L"已导入并选择 LUT："+name+L"（已写入 manifest）。");
            }else message(L"导入失败："+std::wstring(error.begin(),error.end()));
        }
        return 0;
    }
    if(msg==WM_COMMAND&&!populating&&!syncingColour&&LOWORD(wp)>=colorEditId(0)&&LOWORD(wp)<colorEditId(0)+kColorMaxParams&&HIWORD(wp)==EN_CHANGE){
        const int index=LOWORD(wp)-colorEditId(0);
        if(index<int(colorParams.size())){
            wchar_t buffer[64]{};GetWindowTextW(item(LOWORD(wp)),buffer,64);
            wchar_t* end=nullptr;const float value=wcstof(buffer,&end);
            if(end==buffer||*end||!std::isfinite(value))message(L"数值未完整；仍使用上次有效值");
            else colourFieldEdited(index,value);
        }
        return 0;
    }
    if(msg==WM_COMMAND&&!populating&&((LOWORD(wp)==209&&HIWORD(wp)==CBN_SELCHANGE)||(LOWORD(wp)==215&&HIWORD(wp)==BN_CLICKED))){liveField(LOWORD(wp));return 0;}
    switch(msg){
case WM_CREATE:{window=h;font=makeFont(h);items.clear();displayedBackendWarning.clear();smoothMotionHelpExpanded=false;
    WNDCLASSW bodyClass{};bodyClass.lpfnWndProc=bodyProc;bodyClass.hInstance=GetModuleHandleW(nullptr);bodyClass.lpszClassName=L"VeyraInspectorBody";bodyClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&bodyClass);
    body=CreateWindowExW(WS_EX_CONTROLPARENT,bodyClass.lpszClassName,L"滚动参数",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,88,300,300,h,nullptr,bodyClass.hInstance,nullptr);
    add(L"BUTTON",L"实验性 NVIDIA NR 增强",200,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,12,-1,36);
    add(L"BUTTON",L"超分辨率",201,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,56,-1,36);
    combo(203,0,104,{L"1080p NR · 实时默认",L"原生NR · 高性能成本",L"480p NR",L"720p NR",L"900p NR",L"1440p NR"});
    add(L"STATIC",L"模型参数",1100,0,0,12,146,-1,24);
    for(int i=0;i<12;++i){const int y=174+i*62+(i>=7?32:0);add(L"STATIC",labels[i],1000+i,0,0,12,y,176,28);
        if(i>=4&&i<=6){const int group=i-4,count=group==0?3:2;for(int j=0;j<count;++j){const auto title=group==0?std::to_wstring(j):j==0?std::wstring(L"关闭"):std::wstring(L"开启");button(title.c_str(),700+group*10+j,0,12+j*88,y+28,80);}continue;}
        add(L"EDIT",L"",100+i,ES_AUTOHSCROLL|WS_TABSTOP|ES_RIGHT,0,202,y,-1,28);
        const wchar_t* range=i<3?L"范围：0–1":i==3?L"-1表示默认；其余范围0–2（效果未证实）":L"范围：0–2";SetPropW(item(100+i),L"veyra.tip",HANDLE(range));
        auto slider=add(TRACKBAR_CLASSW,L"",600+i,TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,0,12,y+32,-1,16);SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(i==3?-100:0,i<3?100:200));}
    add(L"STATIC",L"增强变化量",1101,0,0,12,606,-1,24);
    add(L"STATIC",L"肤质 / 风格 / 遮罩 / UI键为本地实验参数。未验证的效果不会标成可用能力。",1102,0,0,12,956,-1,70);
    button(L"还原默认",211,-1,12,86,88);
    add(L"BUTTON",L"直播兼容 · 实验",219,BS_AUTOCHECKBOX|WS_TABSTOP,-1,108,86,-1,36);
    SetPropW(item(219),L"veyra.tip",HANDLE(L"直播兼容模式：切换显示交换链，会短暂停顿；不改变增强算法或导出。不保证所有捕获方式有效。"));
    add(L"STATIC",L"补帧与运动估算",1103,0,1,12,12,-1,30);
    add(L"STATIC",L"补帧方式",1111,0,1,12,50,-1,24);
    combo(208,1,78,{L"DLSS 帧生成",L"Intel XeSS · 实验显示补帧 2X-4X",L"AMD FSR 帧生成 · 2X"});
    add(L"STATIC",L"补帧倍率",1112,0,1,12,122,-1,24);
    combo(202,1,150,{L"关闭补帧",L"2X · 一张中间帧",L"3X · 两张中间帧",L"4X · 三张中间帧"});
    add(L"STATIC",L"运动估算",1113,0,1,12,194,-1,24);
    combo(209,1,222,{L"NVIDIA NVOF 光流",L"AMD FidelityFX 光流 · 实验",L"GPU DIS 光流 · FAST 实验"});
    add(L"BUTTON",L"AMD 性能档 · 光流宽高各减半",215,BS_AUTOCHECKBOX|WS_TABSTOP,1,12,266,-1,36);
    combo(204,1,310,{L"NR / DLSS光流 · 性能",L"NR / DLSS光流 · 平衡",L"NR / DLSS光流 · 质量"});
    add(L"STATIC",L"内容节奏",1114,0,1,12,354,-1,24);
    combo(205,1,382,{L"采用源时间戳",L"自动识别内容节奏",L"识别30fps内容节奏",L"识别50fps内容节奏",L"识别60fps内容节奏",L"采集60→30fps处理（PS5 30帧）"});
    add(L"STATIC",L"AMD FidelityFX 为运动估算；不是 AMD NR。XeSS 为实验预览 2X，不支持导出。",1110,0,1,12,426,-1,72);
    add(L"STATIC",L"采集音频同步",1115,0,1,12,608,-1,26);
    combo(216,1,644,{L"自动同步 · 软件估算",L"手动声音偏移",L"关闭补偿"});
    add(L"STATIC",L"声音偏移 ms",1116,0,1,12,692,160,28);
    add(L"EDIT",L"0",217,ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,1,182,692,-1,28);
    SetPropW(item(217),L"veyra.tip",HANDLE(L"-250 至 250 ms；正值让声音更晚。负值只能减少已有延迟，实际补偿最低为0。"));

    // Page 2 is the colour page. The old preset page (ids 300/301/310-315,
    // statics 1105/1106) was deleted on purpose on 2026-09-17: named colour
    // presets replace it and carry only look parameters, never NR/SR/FG.
    loadColourFoldState();
    add(L"BUTTON",L"调色总开关（关闭时这条链路不存在，零开销）",800,BS_AUTOCHECKBOX|WS_TABSTOP,2,12,12,-1,36);
    button(L"一键还原（回到中性）",801,2,12,56,200);button(L"撤销还原",802,2,216,56,120);
    {
        struct Definition{int section;const wchar_t* label;float engine::ColorSettings::*field;float min,max;};
        const Definition definitions[]={
            {0,L"曝光（EV）",&engine::ColorSettings::exposure,-5,5},
            {0,L"对比度",&engine::ColorSettings::contrast,-100,100},
            {0,L"高光",&engine::ColorSettings::highlights,-100,100},
            {0,L"阴影",&engine::ColorSettings::shadows,-100,100},
            {0,L"白色",&engine::ColorSettings::whites,-100,100},
            {0,L"黑色",&engine::ColorSettings::blacks,-100,100},
            {1,L"色温（相对）",&engine::ColorSettings::temperature,-100,100},
            {1,L"色调",&engine::ColorSettings::tint,-100,100},
            {1,L"自然饱和度",&engine::ColorSettings::vibrance,-100,100},
            {1,L"饱和度",&engine::ColorSettings::saturation,-100,100},
            {2,L"高光（参数曲线）",&engine::ColorSettings::paramHighlights,-100,100},
            {2,L"亮色调（参数曲线）",&engine::ColorSettings::paramLights,-100,100},
            {2,L"暗色调（参数曲线）",&engine::ColorSettings::paramDarks,-100,100},
            {2,L"阴影（参数曲线）",&engine::ColorSettings::paramShadows,-100,100},
            {2,L"高光范围分割",&engine::ColorSettings::splitHighlights,-100,100},
            {2,L"中间调范围分割",&engine::ColorSettings::splitMidtones,-100,100},
            {2,L"阴影范围分割",&engine::ColorSettings::splitShadows,-100,100},
        };
        for(const auto& definition:definitions)
            colorParams.push_back({definition.section,definition.label,definition.min,definition.max,ColorTarget::Scalar,definition.field,0});
        // Composite labels are built once; reserve keeps the c_str() pointers
        // stable for the lifetime of the panel.
        static std::vector<std::wstring> colorLabelStorage;
        colorLabelStorage.clear();colorLabelStorage.reserve(256);
        auto composed=[&](const std::wstring& text){colorLabelStorage.push_back(text);return colorLabelStorage.back().c_str();};
        // Mixer: eight hue bands, each with hue / saturation / luminance, then
        // the same eight bands for the black & white mixer.
        {
            static const wchar_t* bands[engine::kColorMixerBands]={L"红色",L"橙色",L"黄色",L"绿色",L"浅绿色",L"蓝色",L"紫色",L"洋红"};
            for(int band=0;band<engine::kColorMixerBands;++band){
                colorParams.push_back({3,composed(std::wstring(bands[band])+L" · 色相"),-100,100,ColorTarget::MixerHue,nullptr,band});
                colorParams.push_back({3,composed(std::wstring(bands[band])+L" · 饱和度"),-100,100,ColorTarget::MixerSaturation,nullptr,band});
                colorParams.push_back({3,composed(std::wstring(bands[band])+L" · 明亮度"),-100,100,ColorTarget::MixerLuminance,nullptr,band});
            }
            for(int band=0;band<engine::kColorMixerBands;++band)
                colorParams.push_back({3,composed(std::wstring(bands[band])+L" · 黑白"),-100,100,ColorTarget::BlackWhiteMix,nullptr,band});
        }
        // Colour grading: four zones with hue / saturation / luminance, plus the
        // blending and balance controls.
        {
            static const wchar_t* zones[engine::kColorGradingZones]={L"阴影",L"中间调",L"高光",L"全局"};
            for(int zone=0;zone<engine::kColorGradingZones;++zone){
                colorParams.push_back({4,composed(std::wstring(zones[zone])+L" · 色相"),0,360,ColorTarget::GradingHue,nullptr,zone});
                colorParams.push_back({4,composed(std::wstring(zones[zone])+L" · 饱和度"),0,100,ColorTarget::GradingSaturation,nullptr,zone});
                colorParams.push_back({4,composed(std::wstring(zones[zone])+L" · 明亮度"),-100,100,ColorTarget::GradingLuminance,nullptr,zone});
            }
            colorParams.push_back({4,L"混合",0,100,ColorTarget::Scalar,&engine::ColorSettings::gradingBlending,0});
            colorParams.push_back({4,L"平衡",-100,100,ColorTarget::Scalar,&engine::ColorSettings::gradingBalance,0});
        }
        // Calibration: shadow tint plus the three primaries.
        {
            colorParams.push_back({5,L"阴影色调",-100,100,ColorTarget::Scalar,&engine::ColorSettings::calibrationShadowTint,0});
            static const wchar_t* primaries[3]={L"红原色",L"绿原色",L"蓝原色"};
            for(int primary=0;primary<3;++primary){
                colorParams.push_back({5,composed(std::wstring(primaries[primary])+L" · 色相"),-100,100,ColorTarget::CalibrationHue,nullptr,primary});
                colorParams.push_back({5,composed(std::wstring(primaries[primary])+L" · 饱和度"),-100,100,ColorTarget::CalibrationSaturation,nullptr,primary});
            }
        }
        // LUT: the .cube selection, its strength and the input-space choice. The
        // strength row is a normal parameter; the two combos are placed by
        // layoutColorPage().
        colorParams.push_back({6,L"LUT 强度",0,100,ColorTarget::Scalar,&engine::ColorSettings::lutStrength,0});
        check(820,BST_UNCHECKED);add(L"BUTTON",L"黑白混色器（把画面转成黑白）",820,BS_AUTOCHECKBOX|WS_TABSTOP,3,12,0,-1,28);
        SetPropW(item(820),L"veyra.tip",HANDLE(L"打开后画面变成黑白，下面八个“黑白”滑块控制各色系对应的灰阶明暗（和 Lightroom 的黑白混色器同一套语义）。"));
        for(int section=0;section<kColorSections;++section)add(L"BUTTON",L"",810+section,BS_PUSHBUTTON|WS_TABSTOP,2,12,12,-1,32);
        // Preset toolbar.
        combo(803,2,0,{});add(L"EDIT",L"",804,ES_AUTOHSCROLL|WS_TABSTOP,2,12,0,-1,26);send(804,EM_SETLIMITTEXT,48,0);
        button(L"保存预设",805,2,12,0,96);button(L"应用",806,2,12,0,80);button(L"删除",807,2,12,0,80);
        button(L"导出",808,2,12,0,80);button(L"导入",809,2,12,0,80);
        SetPropW(item(804),L"veyra.tip",HANDLE(L"给当前色彩设置起个名字，点“保存预设”存下来；导出会生成 .vpcolor 文件，可以发给别人导入。"));
        SetPropW(item(805),L"veyra.tip",HANDLE(L"把当前色彩设置保存为命名预设。同名会覆盖。"));
        SetPropW(item(806),L"veyra.tip",HANDLE(L"把选中的预设应用到当前画面（只改色彩，不动 NR/超分/补帧）。"));
        SetPropW(item(807),L"veyra.tip",HANDLE(L"删除选中的色彩预设。"));
        // LUT section: choose an imported .cube, import a new one, pick its input
        // space (the strength row is registered as a normal parameter).
        combo(817,2,0,{});button(L"导入 .cube",818,2,12,0,140);combo(819,2,0,{L"Cineon Log（创作者 LUT 默认）",L"sRGB 显示参考",L"PQ（HDR）"});
        SetPropW(item(817),L"veyra.tip",HANDLE(L"选择 runtime_local/luts 里已导入的 .cube。切换会重建管线，短暂停顿正常。"));
        SetPropW(item(818),L"veyra.tip",HANDLE(L"从磁盘导入 .cube：校验通过后复制到 runtime_local/luts，并写入 manifest（含 SHA-256）。"));
        SetPropW(item(819),L"veyra.tip",HANDLE(L"LUT 期望的输入空间。创作者 LUT 多数是 Cineon Log；sRGB 显示参考用于 SDR 内容；PQ 给 HDR 用。选错会提示并由日志记录。"));
        refreshColourLooks();refreshColourLuts();
        for(size_t i=0;i<colorParams.size();++i){
            const auto& param=colorParams[i];
            add(L"STATIC",param.label,colorLabelId(int(i)),0,2,12,0,180,24);
            add(L"EDIT",L"0",colorEditId(int(i)),ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,2,202,0,-1,26);
            auto slider=add(TRACKBAR_CLASSW,L"",colorSliderId(int(i)),TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,2,12,0,-1,16);
            SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(int(std::lround(param.min*100.0f)),int(std::lround(param.max*100.0f))));
            SetPropW(item(colorEditId(int(i))),L"veyra.tip",HANDLE(L"可以直接输入数字，回车生效；拖动滑块即时生效。"));
        }
    }
    add(L"STATIC",L"原生画质导出",1107,0,3,12,12,-1,32);combo(500,3,60,{L"H.264 · MP4",L"HEVC · MP4"});send(500,CB_SETCURSEL,0,0);
    add(L"STATIC",L"冻结启动时整套参数；NR按原生尺寸处理。保留兼容音轨。VFR不改写为CFR；字幕不烧录。",1108,0,3,12,108,-1,94);
    button(L"选择位置并导出视频",501,3,12,212);marked(item(501));button(L"保存当前图片 / 视频帧",502,3,12,256);
    button(L"暂停 / 继续导出",503,3,12,310);button(L"取消导出",504,3,12,354);
    add(L"BUTTON",L"优先观看 · 降低导出占用",505,BS_AUTOCHECKBOX|WS_TABSTOP,3,12,406,-1,36);check(505,BST_CHECKED);
    add(L"STATIC",L"",506,0,3,12,458,-1,120);add(L"STATIC",L"",507,0,3,12,588,-1,80);
    // Export bitrate row, inserted between the codec selector and everything
    // below it (the page scrolls, so the shift keeps the reading order).
    for(auto& entry:items)if(entry.page==3&&entry.y>=108)entry.y+=72;
    add(L"STATIC",L"导出码率",1121,0,3,12,104,-1,24);
    combo(508,3,132,{L"自动 · 恒定质量",L"6 Mbps",L"10 Mbps",L"16 Mbps",L"24 Mbps",L"40 Mbps",L"60 Mbps",L"100 Mbps",L"150 Mbps",L"200 Mbps"});
    add(L"STATIC",L"",400,0,-1,12,900,-1,92);add(L"STATIC",L"",401,0,-1,12,996,-1,86);
    for(auto& entry:items)if(entry.page==0&&entry.y>=146)entry.y+=176;
    add(L"BUTTON",L"NR剔除区",206,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,146,-1,36);
    button(L"框选剔除区",213,0,12,188,140);button(L"清除剔除区",214,0,162,188);
    add(L"STATIC",L"最多4区，左键拖框，Esc取消。仅抑制NR变化；不保护SR或补帧。随增强参数一起保存；换源清空。",1109,0,0,12,232,-1,82);
    for(auto& entry:items)if(entry.page==0&&entry.y>=104)entry.y+=48;
    combo(207,0,100,{L"DLSS SR",L"RTX 视频超分 · 低",L"RTX 视频超分 · 中",L"RTX 视频超分 · 高",L"RTX 视频超分 · 最高",L"AMD FSR 超分 · 3.1.x（N卡可用）"});
    for(auto& entry:items)if(entry.page==0&&entry.y>=100)entry.y+=44;
    button(L"2K",730,0,12,100,80);button(L"4K",731,0,100,100,80);button(L"8K",732,0,188,100,80);
    for(auto& entry:items)if(entry.page==0&&entry.y>=56)entry.y+=80;
    add(L"STATIC",L"NR 运行版本",1117,0,0,12,56,-1,24);
    combo(218,0,84,{L"NVIDIA 原版 · RTX 50",L"社区兼容 · RTX 40/50 实验",L"RTX 30 兼容 · 实验"});
    SetPropW(item(218),L"veyra.tip",HANDLE(L"社区版为修改运行时。RTX 30 档需单独组件，性能与兼容性待持卡验证；不解锁 DLSS 补帧。切换会重建管线，失败恢复原设置。"));
    // Final layout in reading order; existing control IDs and bindings stay intact.
    for(auto& entry:items){
        const int id=GetDlgCtrlID(entry.h);
        if(id==203)entry.y=128;
        if(id==201)entry.y=176;
        if(id>=730&&id<=732)entry.y=220;
        if(id==207)entry.y=264;
        switch(id){
        case 1113:entry.y=50;break;case 209:entry.y=78;break;

        case 215:entry.y=122;break;case 204:entry.y=166;break;
        case 1111:entry.y=214;break;case 208:entry.y=242;break;
        case 1112:entry.y=286;break;case 202:entry.y=314;break;
        case 1114:entry.y=402;break;case 205:entry.y=430;break;
        case 1110:entry.y=474;break;
        case 1115:entry.page=4;entry.y=12;break;
        case 216:entry.page=4;entry.y=50;break;
        case 1116:case 217:entry.page=4;entry.y=100;break;
        }
    }
    setText(item(1103),L"光流与补帧");
    button(L"Smooth Motion · 开启方法 ▾",221,1,12,358);
    ghost(item(221));
    SetPropW(item(221),L"veyra.tip",HANDLE(L"查看 NVIDIA App 的 AI 插帧开启方法。这里只提供说明，不修改驱动，也不限制叠加补帧。"));
    add(L"STATIC",smoothMotionHelp,1120,SS_NOPREFIX,1,12,400,-1,1);
    setText(item(1113),L"光流 · 运动估算");
    setText(item(1115),L"采集 / 串流音频同步");
    add(L"STATIC",L"调整实时输入的声音补偿，不改变补帧倍率。正值让声音更晚；自动模式由软件估算。",1118,0,4,12,148,-1,90);
    for(auto& entry:items)if(entry.page==0&&entry.y>=176)entry.y+=44;
    add(L"BUTTON",L"低延迟模式 · 实验",220,BS_AUTOCHECKBOX|WS_TABSTOP,0,12,172,-1,36);
    SetPropW(item(220),L"veyra.tip",HANDLE(L"默认先超分，再NR（DLSS5）。打开后先NR再超分，最后补帧：少搬点砖，可能更快，也可能多些鬼影或边缘瑕疵。只用于预览；导出不换顺序。需同时开启NR和超分才有作用。"));
    // NR exclusion-zone feather lands directly under the zone help text; every
    // later block moves down by the same amount so nothing overlaps.
    for(auto& entry:items)if(entry.page==0&&entry.y>=530)entry.y+=80;
    add(L"STATIC",L"",1123,0,0,12,530,-1,24);
    add(L"EDIT",L"12",222,ES_AUTOHSCROLL|ES_RIGHT|WS_TABSTOP,0,202,530,-1,28);
    auto featherSlider=add(TRACKBAR_CLASSW,L"",622,TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,0,12,562,-1,16);
    SendMessageW(featherSlider,TBM_SETRANGE,TRUE,MAKELPARAM(0,64));
    SendMessageW(featherSlider,TBM_SETPOS,TRUE,12);
    SetPropW(item(222),L"veyra.tip",HANDLE(L"剔除区边缘的过渡宽度，单位是工作分辨率像素。0 就是硬边；4K 上 12 px 约等于画面高度的 0.5%，越大边缘越柔和。"));
    for(const auto& entry:items)if(auto help=settingHelp(GetDlgCtrlID(entry.h)))SetPropW(entry.h,L"veyra.tip",HANDLE(help));
    loadStore();populate(controller->snapshot().desired);message(store.error());SetTimer(h,1,250,nullptr);arrange();
    // Layout evidence for UI work: VEYRA_DUMP_SETTINGS_LAYOUT=1 prints the
    // resolved position of every control once, in DIP units.
    if(GetEnvironmentVariableW(L"VEYRA_DUMP_SETTINGS_LAYOUT",nullptr,0))
        for(const auto& entry:items)veyra::log::info("settings-layout",std::format("id={} page={} x={} y={} w={} h={}",
            GetDlgCtrlID(entry.h),entry.page,entry.x,entry.y,entry.w,entry.height));
    return 0;}
case WM_SIZE:arrange();return 0;
case WM_ERASEBKGND:return 1;
case WM_PAINT:{PaintBuffer paint(h);fillSurface(paint.dc,paint.rect,h);return 0;}
case WM_VSCROLL:{switch(LOWORD(wp)){case SB_LINEUP:scroll-=40;break;case SB_LINEDOWN:scroll+=40;break;case SB_PAGEUP:scroll-=240;break;case SB_PAGEDOWN:scroll+=240;break;case SB_THUMBTRACK:{SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(h,SB_VERT,&si);scroll=si.nTrackPos;break;}}arrange();return 0;}
case WM_MOUSEWHEEL:scroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*36;arrange();return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(msg,wp,lp);
case WM_COMMAND:{const int id=LOWORD(wp);if(!populating&&((id>=202&&id<=205||id==207||id==208)&&HIWORD(wp)==CBN_SELCHANGE||(id>=700&&id<=732)&&HIWORD(wp)==BN_CLICKED)){liveField(id);return 0;}if((id==206||id==213||id==214)&&HIWORD(wp)==BN_CLICKED){const auto accepted=SendMessageW(GetParent(h),WM_APP+45,id,checked(206));message(accepted?(id==213?L"请在画面中左键拖动框选；Esc取消。":L"已请求更新NR剔除区。"):L"未能操作：请先打开画面，或清除已满的4个区域。");return 0;}if((id==200||id==201)&&HIWORD(wp)==BN_CLICKED){const bool accepted=SendMessageW(GetParent(h),WM_APP+44,id,checked(id))!=0;message(accepted?L"已请求开关；确认帧边界结果后生效。":L"总增强正在切换，请待当前事务完成。");return 0;}if(HIWORD(wp)==EN_SETFOCUS){for(auto& item:items)if(GetDlgCtrlID(item.h)==id&&item.page==page){RECT r{};GetClientRect(h,&r);int height=MulDiv(r.bottom,96,veyra::ui::layoutDpi(h))-128;if(item.y<scroll)scroll=item.y;if(item.y+item.height>scroll+height)scroll=item.y+item.height-height;arrange();break;}}if(!populating&&id>=100&&id<=111&&HIWORD(wp)==EN_CHANGE){liveField(id);return 0;}
    if(!populating&&id>=100&&id<=111&&HIWORD(wp)==EN_KILLFOCUS){populate(enhancementEnabled?controller->snapshot().desired:configuredSettings);return 0;}
    engine::EnhancementSettings s;
    if(id==211){SetFocus(body);s={};if(submit(s))populate(s);message(L"已还原内建默认。");}
    else if(id>=501&&id<=505)SendMessageW(GetParent(h),WM_APP+41,id,id==501?send(500,CB_GETCURSEL,0,0):id==505?checked(505):0);
    return 0;}
case WM_HSCROLL:{int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));if(id>=600&&id<612){int index=id-600;float v=float(SendMessageW(reinterpret_cast<HWND>(lp),TBM_GETPOS,0,0))/(index>=4&&index<=6?1:100);if(index==3&&v<0)v=-1;std::wostringstream o;o<<std::setprecision(4)<<v;putText(100+index,o.str().c_str());}
    else if(id>=colorSliderId(0)&&id<colorSliderId(0)+kColorMaxParams){
        const int index=id-colorSliderId(0);
        if(index<int(colorParams.size()))colourFieldEdited(index,float(SendMessageW(reinterpret_cast<HWND>(lp),TBM_GETPOS,0,0))/100.0f);
    }
    else if(id==622){// Feather slider: the edit box owns the value, its EN_CHANGE applies it.
        const int value=std::clamp(int(SendMessageW(reinterpret_cast<HWND>(lp),TBM_GETPOS,0,0)),0,64);putText(222,std::to_wstring(value).c_str());}
    return 0;}
case WM_TIMER:{auto s=controller->snapshot();syncProtection(enhancementEnabled?s.desired.protection:configuredSettings.protection);if(enhancementEnabled&&!dirty&&displayedSettings!=s.desired)populate(s.desired);syncColorControls();check(200,enhancementEnabled&&s.desired.nr?BST_CHECKED:BST_UNCHECKED);check(201,enhancementEnabled&&s.desired.sr?BST_CHECKED:BST_UNCHECKED);std::wostringstream o;if(!s.running&&!s.frames&&s.transport!=engine::TransportState::Opening)o<<L"未打开媒体 · 设置待启用\n";else{
    o<<L"期望版本 "<<s.desired.revision<<L" / 已应用 "<<s.applied.revision<<(s.applying?L" · 应用中":L"");
    const wchar_t* backend=s.applied.frameGenerationBackend==engine::FrameGenerationBackend::XeSS?L"XeSS":s.applied.frameGenerationBackend==engine::FrameGenerationBackend::Fsr?L"AMD FSR":L"DLSS";
    o<<L"\n"<<backend<<L" · "<<(s.applied.multiplier<=1?L"补帧关闭":s.fgActive?L"补帧运行":L"等待有效补帧");
    if(!s.backendWarning.empty())message(s.backendWarning);
    else if(!displayedBackendWarning.empty())message(L"设置已应用");
    displayedBackendWarning=s.backendWarning;
}setText(item(400),o.str());return 0;}
case WM_DESTROY:KillTimer(h,1);DeleteObject(font);window=nullptr;body=nullptr;items.clear();return 0;
}return DefWindowProcW(h,msg,wp,lp);}
}
engine::EnhancementSettings defaultSettings(){loadStore();return store.defaultSettings();}
HWND settingsControlForTest(int id){return item(id);}
int colourParamEditId(const wchar_t* label){
    if(!label)return -1;
    for(size_t i=0;i<colorParams.size();++i)if(std::wcscmp(colorParams[i].label,label)==0)return colorEditId(int(i));
    return -1;
}
HWND createSettingsPanel(HWND parent,engine::EngineController& engine,std::function<bool(engine::EnhancementSettings)> callback){controller=&engine;apply=std::move(callback);WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraInspector";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);return CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"专业参数",WS_CHILD|WS_CLIPCHILDREN,0,0,328,500,parent,nullptr,wc.hInstance,nullptr);}
void settingsVisibility(bool visible){if(window&&!visible&&IsChild(window,GetFocus()))SetFocus(GetParent(window));}
void settingsPage(int value){if(window&&IsChild(window,GetFocus()))SetFocus(body);page=std::clamp(value,0,4);scroll=0;arrange();}
void settingsEnabled(bool enabled,const engine::EnhancementSettings& configured){enhancementEnabled=enabled;configuredSettings=configured;if(window)syncProtection(enabled?controller->snapshot().desired.protection:configured.protection);if(window&&!enabled&&!dirty&&displayedRevision!=configured.revision)populate(configured);if(window){auto desired=controller->snapshot().desired;check(200,enabled&&desired.nr?BST_CHECKED:BST_UNCHECKED);check(201,enabled&&desired.sr?BST_CHECKED:BST_UNCHECKED);if(!enabled)message(L"增强已关闭。数值修改保存待启用配置；点击 NR / 超分可直接开启。");}}
void settingsDpi(){if(!window)return;auto old=font;font=makeFont(window);for(auto& item:items)SendMessageW(item.h,WM_SETFONT,WPARAM(font),TRUE);DeleteObject(old);arrange();}
void exportPanelStatus(const engine::ExportJobSnapshot& job,bool canExport,bool canSave){if(!window)return;setText(item(506),job.state==engine::ExportState::Idle?L"尚无导出任务":job.message+L"\n"+std::to_wstring(int(job.progress*100))+L"% · 源帧 "+std::to_wstring(job.sourceFrames)+L" / 编码 "+std::to_wstring(job.encoded)+L"\n生成 "+std::to_wstring(job.generated)+L" / CFR占位 "+std::to_wstring(job.holds)+L"\n作业 "+std::to_wstring(job.jobId)+L" · 冻结版本 "+std::to_wstring(job.frozenRevision));setText(item(507),job.output.empty()?L"目标由保存窗口选择":L"输出："+std::filesystem::path(job.output).filename().wstring());EnableWindow(item(501),canExport&&!job.active());EnableWindow(item(502),canSave);EnableWindow(item(503),job.state==engine::ExportState::Running||job.state==engine::ExportState::Paused);EnableWindow(item(504),job.active());}
}
