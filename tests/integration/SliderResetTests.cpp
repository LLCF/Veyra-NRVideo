#include "../../apps/veyra/SettingsWindow.h"
#include "../../apps/veyra/ui/Theme.h"
#include <iostream>
#include <stdexcept>

void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int wmain(int argc,wchar_t**){try{
    using namespace veyra;
    Gdiplus::GdiplusStartupInput graphicsInput;ULONG_PTR graphicsToken=0;
    Gdiplus::GdiplusStartup(&graphicsToken,&graphicsInput,nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    engine::EngineController engine;
    engine::EnhancementSettings actual,defaults;
    bool reject=false;
    HWND parent=CreateWindowExW(0,L"STATIC",L"Veyra slider reset verification",WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,100,100,390,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    auto panel=ui::createSettingsPanel(parent,engine,[&](auto settings){
        if(reject)return false;
        actual=settings;ui::settingsEnabled(false,actual);return true;
    });
    SetWindowPos(panel,nullptr,0,0,360,800,SWP_NOZORDER|SWP_SHOWWINDOW);
    auto sync=[&](){ui::settingsEnabled(false,actual);SendMessageW(panel,WM_TIMER,1,0);};
    auto verify=[&](int id,auto change,auto restore){
        actual=defaults;actual.color.enabled=true;actual.color.contrast=17;actual.videoHdr.enabled=true;
        change(actual);++actual.revision;sync();
        auto expected=actual;restore(expected);
        auto button=ui::settingsControlForTest(id);
        require(button&&IsWindowEnabled(button),"changed row must enable reset");
        SendMessageW(button,BM_CLICK,0,0);
        require(actual==expected,"reset must change only its own parameter and retain effect switches");
        require(!IsWindowEnabled(button),"default row must disable reset");
    };
    verify(2600,[](auto& s){s.model.intensity=.37f;},[&](auto& s){s.model.intensity=defaults.model.intensity;});
    verify(2622,[](auto& s){s.protection.featherPixels=33;},[&](auto& s){s.protection.featherPixels=defaults.protection.featherPixels;});
    verify(2634,[](auto& s){s.videoHdr.peakNits=1400;},[&](auto& s){s.videoHdr.peakNits=defaults.videoHdr.peakNits;});
    verify(3400,[](auto& s){s.color.exposure=1.5f;},[&](auto& s){s.color.exposure=defaults.color.exposure;});
    auto undo=ui::settingsControlForTest(802);SendMessageW(undo,BM_CLICK,0,0);
    require(actual.color.exposure==1.5f&&actual.color.contrast==17,"colour reset must support undo");
    const int blendReset=ui::colourParamEditId(L"混合")+2100;
    verify(blendReset,[](auto& s){s.color.gradingBlending=77;},[&](auto& s){s.color.gradingBlending=defaults.color.gradingBlending;});
    verify(ui::colourParamEditId(L"LUT 强度")+2100,[](auto& s){s.color.lutStrength=35;},[&](auto& s){s.color.lutStrength=defaults.color.lutStrength;});
    actual.color.exposure=2;++actual.revision;sync();auto before=actual;reject=true;
    SendMessageW(ui::settingsControlForTest(3400),BM_CLICK,0,0);
    require(actual==before&&IsWindowEnabled(ui::settingsControlForTest(3400)),"rejected transaction must retain the changed value");reject=false;
    ui::settingsPage(2);ui::settingsColorSectionForTest(0,true);
    for(int width:{280,328,420}){
        SetWindowPos(panel,nullptr,0,0,width,800,SWP_NOZORDER);
        RECT label{},value{},reset{};
        GetWindowRect(ui::settingsControlForTest(1200),&label);GetWindowRect(ui::settingsControlForTest(1300),&value);GetWindowRect(ui::settingsControlForTest(3400),&reset);
        require(label.right<=value.left&&value.right<reset.left&&reset.right-reset.left>=28,"row controls must remain separate at narrow widths");
    }
    ui::settingsColorSectionForTest(0,false);
    require(!(GetWindowLongW(ui::settingsControlForTest(3400),GWL_STYLE)&WS_VISIBLE),"collapsed row must hide reset");
    ui::settingsColorSectionForTest(0,true);
    SetWindowPos(panel,nullptr,0,0,360,800,SWP_NOZORDER);
    std::cout<<"PASS: individual defaults, unrelated settings, undo, rejection, enabled state, narrow layout and folding\n";
    if(argc>1){const auto end=GetTickCount64()+60000;MSG msg{};while(GetTickCount64()<end&&IsWindow(parent)){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(10);}}
    DestroyWindow(parent);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
