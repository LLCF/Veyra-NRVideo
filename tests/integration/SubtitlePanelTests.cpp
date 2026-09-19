#include "../../apps/veyra/ui/SubtitleSettingsPanel.h"
#include <iostream>
int wmain(){
    using namespace veyra::ui;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_UPDOWN_CLASS};InitCommonControlsEx(&controls);
    HWND owner=CreateWindowExW(0,L"STATIC",L"Subtitle regression",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,600,500,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    int failures=0,updates=0;SubtitleSettings current;
    auto check=[&](bool ok,const char* message){std::cout<<(ok?"PASS ":"FAIL ")<<message<<'\n';failures+=!ok;};
    showSubtitleSettings(owner,{},[&](const auto& settings){current=settings;++updates;});
    const HWND panel=subtitlePanel::window,edit=GetDlgItem(panel,101);SetFocus(edit);
    for(int i=0;i<20;++i){SetWindowTextW(edit,std::to_wstring(20+i).c_str());check(IsWindow(panel)&&GetFocus()==edit&&current.pixels==20+i,"continuous edit retains panel and focus");}
    SetWindowTextW(edit,L"-");check(current.pixels==39,"intermediate invalid text does not apply");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(101,EN_KILLFOCUS),LPARAM(edit));
    wchar_t value[32]{};GetWindowTextW(edit,value,32);check(std::wstring(value)==L"39","invalid text restored on blur");
    SetDlgItemTextW(panel,103,L"-500");SetDlgItemTextW(panel,104,L"0");
    check(current.offset==-500&&current.lines==0,"signed delay and auto lines apply");
    SendDlgItemMessageW(panel,105,BM_CLICK,0,0);check(!current.outline,"checkbox applies without closing");
    showSubtitleSettings(owner,{},{});check(subtitlePanel::window==panel,"reopen focuses same panel");
    SendMessageW(panel,WM_COMMAND,IDCANCEL,0);check(!subtitlePanel::window,"cancel clears window lifetime");
    showSubtitleSettings(owner,current,{});check(subtitlePanel::window&&IsWindow(subtitlePanel::window),"panel recreates after close");
    DestroyWindow(owner);check(!subtitlePanel::window,"owner destruction releases panel");
    check(updates>=23,"live change notifications delivered");return failures?1:0;
}
