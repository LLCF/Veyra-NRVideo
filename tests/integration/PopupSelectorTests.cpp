#include "../../apps/veyra/ui/Theme.h"
#include <iostream>
#include <stdexcept>
namespace {
HWND owner=nullptr,combo=nullptr,next=nullptr,previous=nullptr;int action=0,steps=0;std::vector<int> notifications;
void require(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COMMAND&&LOWORD(w)==10){notifications.push_back(HIWORD(w));if(action==8&&HIWORD(w)==CBN_DROPDOWN)SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);if(action==11&&HIWORD(w)==CBN_SELENDOK)DestroyWindow(combo);if(action==12&&HIWORD(w)==CBN_DROPDOWN)DestroyWindow(owner);return 0;}
    if(m==WM_TIMER){using namespace veyra::ui;if(!activeSelector)return 0;auto list=GetDlgItem(activeSelector,1);++steps;
        if(steps>10){cancelPopupSelector();return 0;}
        switch(action){
        case 0:SendMessageW(list,WM_KEYDOWN,VK_DOWN,0);SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);break;
        case 1:SendMessageW(list,WM_KEYDOWN,VK_DOWN,0);SendMessageW(list,WM_KEYDOWN,VK_ESCAPE,0);break;
        case 2:SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);break;
        case 3:SendMessageW(list,WM_KEYDOWN,VK_TAB,0);break;
        case 4:SendMessageW(combo,CB_RESETCONTENT,0,0);SendMessageW(combo,CB_ADDSTRING,0,LPARAM(L"replaced"));SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);break;
        case 5:SendMessageW(list,WM_KEYDOWN,VK_END,0);SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);break;
        case 6:EnableWindow(combo,FALSE);break;
        case 7:ShowWindow(owner,SW_HIDE);break;
        case 9:DestroyWindow(combo);break;
        case 10:DestroyWindow(owner);break;
        case 11:SendMessageW(list,WM_KEYDOWN,VK_DOWN,0);SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);break;
        case 13:{BYTE saved[256]{},shifted[256]{};GetKeyboardState(saved);std::copy_n(saved,256,shifted);shifted[VK_SHIFT]|=0x80;SetKeyboardState(shifted);SendMessageW(list,WM_KEYDOWN,VK_TAB,0);SetKeyboardState(saved);break;}
        case 14:{RECT row{};SendMessageW(list,LB_GETITEMRECT,5,LPARAM(&row));const auto point=MAKELPARAM(row.left+8,(row.top+row.bottom)/2);SendMessageW(list,WM_MOUSEMOVE,0,point);UpdateWindow(list);for(int i=0;i<50;++i)SendMessageW(list,WM_MOUSEMOVE,0,point);require(!GetUpdateRect(list,nullptr,FALSE),"same-row pointer motion must not repeatedly invalidate");SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(list,WM_LBUTTONUP,0,point);break;}
        }return 0;
    }return DefWindowProcW(h,m,w,l);
}
bool notified(int code){return std::find(notifications.begin(),notifications.end(),code)!=notifications.end();}
}
int wmain(){ULONG_PTR gdiplus=0;Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&gdiplus,&input,nullptr);int result=0;
try{
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.lpszClassName=L"VeyraSelectorContract";wc.hInstance=GetModuleHandleW(nullptr);RegisterClassW(&wc);
    for(action=0;action<=14;++action){
        owner=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Veyra popup interaction test",WS_OVERLAPPEDWINDOW,20,20,480,320,nullptr,nullptr,wc.hInstance,nullptr);
        previous=CreateWindowExW(0,L"BUTTON",L"previous",WS_CHILD|WS_VISIBLE|WS_TABSTOP,20,5,100,24,owner,HMENU(9),wc.hInstance,nullptr);
        combo=CreateWindowExW(0,L"COMBOBOX",L"choices",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,20,40,300,200,owner,HMENU(10),wc.hInstance,nullptr);veyra::ui::themeControl(combo);
        next=CreateWindowExW(0,L"BUTTON",L"next",WS_CHILD|WS_VISIBLE|WS_TABSTOP,20,100,100,32,owner,HMENU(11),wc.hInstance,nullptr);
        // First ShowWindow consumes the runner's STARTUPINFO SW_HIDE.
        ShowWindow(owner,SW_HIDE);ShowWindow(owner,SW_SHOW);SetForegroundWindow(owner);SetActiveWindow(owner);
        for(int i=0;i<33;++i){auto label=L"Option "+std::to_wstring(i);SendMessageW(combo,CB_ADDSTRING,0,LPARAM(label.c_str()));}SendMessageW(combo,CB_SETCURSEL,4,0);SetFocus(combo);notifications.clear();steps=0;SetTimer(owner,1,20,nullptr);
        SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);if(IsWindow(owner))KillTimer(owner,1);require(action==8||action==12?steps==0:steps>0&&steps<=10,"bounded popup closed including synchronous cancellation");require(!veyra::ui::popupSelectorOpen()&&!veyra::ui::activeSelector,"popup releases all active state");
        if(action>=9&&action<=12){require(!IsWindow(combo),"destroyed combo stays destroyed");if(action==10||action==12)require(!IsWindow(owner),"destroyed owner stays destroyed");if(IsWindow(owner))DestroyWindow(owner);std::cout<<"PASS popup case "<<action<<"\n";continue;}
        require(!SendMessageW(combo,CB_GETDROPPEDSTATE,0,0),"collapse reports closed");require(notified(CBN_DROPDOWN)&&notified(CBN_CLOSEUP),"native dropdown lifecycle notifications");
        if(action==0||action==5||action==14){require(SendMessageW(combo,CB_GETCURSEL,0,0)==(action==5?32:5),"arrow/end/mouse commits selected option including scrolled items");require(notified(CBN_SELCHANGE)&&notified(CBN_SELENDOK),"commit notifies owning panel");}
        else{require(notified(CBN_SELENDCANCEL)&&!notified(CBN_SELCHANGE),"cancel never submits a setting");if(action!=4)require(SendMessageW(combo,CB_GETCURSEL,0,0)==4,"cancel keeps original selection");}
        if(action==3)require(GetFocus()==next,"Tab advances to next control");
        if(action==13)require(GetFocus()==previous,"Shift+Tab returns to previous control");
        DestroyWindow(owner);
        std::cout<<"PASS popup case "<<action<<"\n";
    }
    std::cout<<"PASS: popup keyboard, scrolling, cancel, collapse, focus traversal, async refresh, disable/hide/destroy\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';result=1;}
if(IsWindow(owner))DestroyWindow(owner);veyra::ui::glassTextTheme().reset();Gdiplus::GdiplusShutdown(gdiplus);return result;
}
