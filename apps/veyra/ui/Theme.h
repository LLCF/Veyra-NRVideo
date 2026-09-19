#pragma once
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <objbase.h>
#include <ocidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "GlassMaterial.h"
#include "LucideIcons.h"
#pragma comment(lib,"uxtheme.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"gdiplus.lib")
#include <dwmapi.h>
namespace veyra::ui {
inline constexpr COLORREF background=RGB(9,10,11),panel=RGB(27,28,30),raised=RGB(40,41,43),line=RGB(58,60,63),textColor=RGB(236,238,240),secondary=RGB(192,196,202),accent=RGB(230,122,49),cinema=RGB(32,48,62),cinemaPanel=RGB(23,34,43);
inline HBRUSH bgBrush(){static auto b=CreateSolidBrush(background);return b;}
inline HBRUSH panelBrush(){static auto b=CreateSolidBrush(panel);return b;}
inline HBRUSH raisedBrush(){static auto b=CreateSolidBrush(raised);return b;}
// Only the bounded smoke harness can set this; normal windows use Windows DPI.
inline UINT smokeLayoutDpi=0;
inline UINT layoutDpi(HWND h){return smokeLayoutDpi?smokeLayoutDpi:std::max(96u,GetDpiForWindow(h));}
inline int dip(HWND h,int v){return MulDiv(v,layoutDpi(h),96);}
inline HFONT makeFont(HWND h,int size=14,int weight=FW_NORMAL){return CreateFontW(-dip(h,size),0,0,0,weight,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");}
inline void titleTheme(HWND h){BOOL enabled=TRUE;DwmSetWindowAttribute(h,20,&enabled,sizeof(enabled));DWORD corner=2;DwmSetWindowAttribute(h,33,&corner,sizeof(corner));COLORREF border=0xfffffffe;DwmSetWindowAttribute(h,34,&border,sizeof(border));}
// Compose controls and dashboard in memory; never expose the erase/fill pass.
class PaintBuffer {
    HWND window_;HDC target_;AlphaRaster raster_;PAINTSTRUCT ps_{};
    bool external_=false;
public:
    RECT rect{};HDC dc;
    explicit PaintBuffer(HWND window,HDC supplied=nullptr):window_(window),external_(supplied!=nullptr){target_=supplied?supplied:BeginPaint(window_,&ps_);GetClientRect(window_,&rect);if(external_)ps_.rcPaint=rect;dc=raster_.create(target_,rect.right,rect.bottom)?raster_.dc:target_;}
    ~PaintBuffer(){if(dc==raster_.dc){GdiFlush();const auto& r=ps_.rcPaint;BitBlt(target_,r.left,r.top,r.right-r.left,r.bottom-r.top,dc,r.left,r.top,SRCCOPY);}if(!external_)EndPaint(window_,&ps_);}
};
// Native controls can draw synchronously from state setters, bypassing
// WM_PAINT. Let their model update with redraw disabled, then invalidate
// for our buffered painter. Do not accidentally show originally hidden HWNDs.
inline LRESULT updateControlModel(HWND h,UINT m,WPARAM w,LPARAM l){
    const bool suppress=(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)&&!GetPropW(h,L"SysSetRedraw")&&!GetPropW(h,L"Veyra.ModelUpdate");
    if(suppress){SetPropW(h,L"Veyra.ModelUpdate",HANDLE(1));DefSubclassProc(h,WM_SETREDRAW,FALSE,0);}
    const auto result=DefSubclassProc(h,m,w,l);
    if(suppress&&IsWindow(h)){DefSubclassProc(h,WM_SETREDRAW,TRUE,0);RemovePropW(h,L"Veyra.ModelUpdate");InvalidateRect(h,nullptr,FALSE);}
    return result;
}
inline bool controlVisualChange(UINT m){
    return m==WM_SETTEXT||m==WM_ENABLE||m==WM_SETFONT||m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_UPDATEUISTATE||m==WM_THEMECHANGED;
}
struct GlassTextTheme {
    HTHEME value=nullptr;bool opened=false;
    void reset(){if(value)CloseThemeData(value);value=nullptr;opened=false;}
    HTHEME get(){if(!opened){value=OpenThemeData(nullptr,L"CompositedWindow::Window");opened=true;}return value;}
    ~GlassTextTheme(){reset();}
};
inline GlassTextTheme& glassTextTheme(){static GlassTextTheme theme;return theme;}
inline int glassText(HDC dc,const wchar_t* text,int count,RECT* rect,UINT flags){
    DTTOPTS options{sizeof(options)};options.dwFlags=DTT_COMPOSITED|DTT_TEXTCOLOR;options.crText=GetTextColor(dc);
    auto theme=glassTextTheme().get();if(theme&&SUCCEEDED(DrawThemeTextEx(theme,dc,0,0,text,count,flags,rect,&options)))return rect->bottom-rect->top;
    // Theme services may be unavailable. Use a coverage mask, never raw GDI
    // text directly on the alpha surface (GDI would erase its alpha channel).
    AlphaRaster ink;if(!ink.create(dc,rect->right-rect->left,rect->bottom-rect->top))return 0;
    auto old=SelectObject(ink.dc,GetCurrentObject(dc,OBJ_FONT));SetTextColor(ink.dc,RGB(255,255,255));SetBkMode(ink.dc,TRANSPARENT);RECT local{0,0,ink.width,ink.height};int result=DrawTextW(ink.dc,text,count,&local,flags);SelectObject(ink.dc,old);GdiFlush();auto color=GetTextColor(dc);
    for(size_t i=0;i<size_t(ink.width)*ink.height;++i){auto pixel=ink.pixels[i];auto alpha=std::max({pixel&255,(pixel>>8)&255,(pixel>>16)&255});ink.pixels[i]=(alpha<<24)|(GetRValue(color)*alpha/255<<16)|(GetGValue(color)*alpha/255<<8)|GetBValue(color)*alpha/255;}
    AlphaGraphics drawing(dc);Gdiplus::Bitmap bitmap(ink.width,ink.height,ink.width*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(ink.pixels));drawing.get().DrawImage(&bitmap,int(rect->left),int(rect->top));return result;
}
inline COLORREF surface(HWND h){auto prop=GetPropW(h,L"veyra.surface");return prop?COLORREF(uintptr_t(prop)-1):panel;}
inline void surface(HWND h,COLORREF c){if(surface(h)==c)return;SetPropW(h,L"veyra.surface",HANDLE(uintptr_t(c)+1));InvalidateRect(h,nullptr,FALSE);}
inline Gdiplus::Color color(COLORREF c,BYTE a=255){return Gdiplus::Color(a,GetRValue(c),GetGValue(c),GetBValue(c));}
inline void rounded(Gdiplus::GraphicsPath& p,Gdiplus::RectF r,float radius){float d=std::min({radius*2,r.Width,r.Height});p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);p.AddArc(r.X,r.GetBottom()-d,d,d,90,90);p.CloseFigure();}
inline void roundRect(HDC dc,RECT r,COLORREF c,int radius){AlphaGraphics drawing(dc);auto& g=drawing.get();g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Gdiplus::GraphicsPath path;rounded(path,{float(r.left),float(r.top),float(r.right-r.left-1),float(r.bottom-r.top-1)},float(radius));Gdiplus::SolidBrush brush(color(c));g.FillPath(&brush,&path);}
inline void fillSurface(HDC dc,RECT r,HWND h){if(copyGlass(dc,r,h))return;auto b=CreateSolidBrush(surface(h));FillRect(dc,&r,b);DeleteObject(b);}
inline void translucentRound(HDC dc,RECT r,BYTE alpha,int radius){AlphaGraphics drawing(dc);auto& g=drawing.get();g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Gdiplus::GraphicsPath path;rounded(path,{float(r.left),float(r.top),float(r.right-r.left-1),float(r.bottom-r.top-1)},float(radius));Gdiplus::SolidBrush tint(Gdiplus::Color(alpha,228,235,242));g.FillPath(&tint,&path);}
inline LRESULT colors(UINT msg,WPARAM w,LPARAM l=0){HDC dc=reinterpret_cast<HDC>(w);HWND h=reinterpret_cast<HWND>(l);wchar_t cls[32]{};GetClassNameW(h,cls,32);const bool edit=_wcsicmp(cls,L"EDIT")==0;const bool flat=GetPropW(h,L"veyra.flat")!=nullptr;COLORREF bg=edit&&!flat?raised:surface(h);SetTextColor(dc,edit?textColor:secondary);SetBkColor(dc,bg);SetBkMode(dc,TRANSPARENT);if(edit&&GetPropW(h,L"Veyra.EditPrint")){SetBkColor(dc,RGB(0,0,0));return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));}SetDCBrushColor(dc,bg);return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));}
inline LRESULT CALLBACK labelPaint(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PaintBuffer paint(h,reinterpret_cast<HDC>(w));fillSurface(paint.dc,paint.rect,h);wchar_t text[4096]{};GetWindowTextW(h,text,4096);auto old=SelectObject(paint.dc,HFONT(SendMessageW(h,WM_GETFONT,0,0)));SetBkMode(paint.dc,TRANSPARENT);SetTextColor(paint.dc,IsWindowEnabled(h)?secondary:RGB(120,125,132));auto style=GetWindowLongPtrW(h,GWL_STYLE);auto type=style&SS_TYPEMASK;UINT flags=(style&SS_NOPREFIX)?DT_NOPREFIX:0;flags|=type==SS_CENTER?DT_CENTER:type==SS_RIGHT?DT_RIGHT:DT_LEFT;if(style&SS_CENTERIMAGE)flags|=DT_SINGLELINE|DT_VCENTER;else if(type==SS_SIMPLE||type==SS_LEFTNOWORDWRAP)flags|=DT_SINGLELINE;else flags|=DT_WORDBREAK;if(style&SS_ENDELLIPSIS)flags|=DT_END_ELLIPSIS;glassText(paint.dc,text,-1,&paint.rect,flags);SelectObject(paint.dc,old);return 0;}
    return controlVisualChange(m)?updateControlModel(h,m,w,l):DefSubclassProc(h,m,w,l);
}
inline void icon(HWND h,Icon icon,bool caption=false){if(INT_PTR(GetPropW(h,L"veyra.icon"))==INT_PTR(icon)&&(GetPropW(h,L"veyra.caption")!=nullptr)==caption)return;SetPropW(h,L"veyra.icon",HANDLE(INT_PTR(icon)));if(caption)SetPropW(h,L"veyra.caption",HANDLE(1));else RemovePropW(h,L"veyra.caption");InvalidateRect(h,nullptr,FALSE);}
inline LRESULT CALLBACK buttonPaint(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(m==BM_SETCHECK&&SendMessageW(h,BM_GETCHECK,0,0)==LRESULT(w))return 0;
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_MOUSEMOVE){if(!GetPropW(h,L"veyra.hover")){SetPropW(h,L"veyra.hover",HANDLE(1));TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);InvalidateRect(h,nullptr,FALSE);}}
    if(m==WM_MOUSELEAVE){RemovePropW(h,L"veyra.hover");InvalidateRect(h,nullptr,FALSE);}
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PaintBuffer paint(h,reinterpret_cast<HDC>(w));HDC dc=paint.dc;RECT r=paint.rect;fillSurface(dc,r,h);
        const bool marked=GetPropW(h,L"veyra.accent")!=nullptr,selected=GetPropW(h,L"veyra.selected")!=nullptr,hover=GetPropW(h,L"veyra.hover")!=nullptr,down=SendMessageW(h,BM_GETSTATE,0,0)&BST_PUSHED,check=SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED,ghost=GetPropW(h,L"veyra.ghost")!=nullptr;
        const auto ico=static_cast<Icon>(INT_PTR(GetPropW(h,L"veyra.icon")));bool caption=GetPropW(h,L"veyra.caption")!=nullptr;
        COLORREF fill=marked?accent:(down||selected?line:hover?raised:surface(h));if(glassBackdrop(h)&&!marked){if(down||selected||hover)translucentRound(dc,r,down?42:selected?31:18,dip(h,8));}else if(!ghost||hover||selected||marked)roundRect(dc,r,fill,ico==Icon::Play||ico==Icon::Pause?std::min(r.right,r.bottom)/2:dip(h,8));
        wchar_t value[512]{};GetWindowTextW(h,value,512);auto f=reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0));auto old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);COLORREF ink=!IsWindowEnabled(h)?RGB(90,96,103):marked?RGB(24,31,36):(selected||check)?accent:hover?textColor:secondary;SetTextColor(dc,ink);
        if(ico!=Icon::None){drawIcon(dc,ico,caption?float(dip(h,18)):r.right/2.f,r.bottom/2.f,float(dip(h,ico==Icon::Play||ico==Icon::Pause?22:17)),ink);if(caption){r.left=dip(h,34);glassText(dc,value,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}}
        else if((GetWindowLongPtrW(h,GWL_STYLE)&BS_TYPEMASK)==BS_AUTOCHECKBOX){RECT toggle{r.right-dip(h,40),r.bottom/2-dip(h,8),r.right-dip(h,10),r.bottom/2+dip(h,8)};roundRect(dc,toggle,check?accent:line,dip(h,8));RECT knob{toggle.left+dip(h,check?16:3),toggle.top+dip(h,3),toggle.left+dip(h,check?26:13),toggle.top+dip(h,13)};roundRect(dc,knob,check?panel:secondary,dip(h,5));r.left=dip(h,12);r.right-=dip(h,46);glassText(dc,value,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}else glassText(dc,value,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(GetFocus()==h&&!(SendMessageW(h,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){RECT outline{};GetClientRect(h,&outline);InflateRect(&outline,-3,-3);AlphaGraphics focusDrawing(dc);auto& focus=focusDrawing.get();focus.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Gdiplus::GraphicsPath path;rounded(path,{float(outline.left),float(outline.top),float(outline.right-outline.left),float(outline.bottom-outline.top)},float(dip(h,6)));Gdiplus::Pen stroke(color(accent,145),1);focus.DrawPath(&stroke,&path);}SelectObject(dc,old);return 0;}
    // Click dispatch can enter a modal popup through BN_CLICKED. WM_SETREDRAW
    // removes WS_VISIBLE, so never hold suppression across input dispatch.
    // Native pressed-state changes still use the buffered BM_SETSTATE path.
    if(controlVisualChange(m)||m==BM_SETCHECK||m==BM_SETSTATE||m==BM_SETSTYLE||m==WM_MOUSEMOVE||m==WM_MOUSELEAVE)return updateControlModel(h,m,w,l);
    return DefSubclassProc(h,m,w,l);
}
}
#include "PopupSelector.h"
namespace veyra::ui {
inline LRESULT CALLBACK comboPaint(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(m==CB_SETCURSEL&&SendMessageW(h,CB_GETCURSEL,0,0)==LRESULT(w))return LRESULT(w);
    if(m==CB_GETDROPPEDSTATE)return GetPropW(h,L"Veyra.ComboOpen")!=nullptr;
    if(m==CB_SHOWDROPDOWN&&!w){if(GetPropW(h,L"Veyra.ComboOpen")){SetPropW(h,L"Veyra.ComboCancel",HANDLE(1));cancelPopupSelector();}return 0;}
    if(m==WM_LBUTTONDOWN||m==CB_SHOWDROPDOWN&&w||m==WM_KEYDOWN&&(w==VK_F4||w==VK_SPACE||w==VK_RETURN)||m==WM_SYSKEYDOWN&&w==VK_DOWN){if(IsWindowEnabled(h))comboSelector(h);return 0;}
    // The custom selector owns pointer interaction; native hot tracking draws
    // directly into the window DC and would overwrite the buffered artwork.
    if(m==WM_LBUTTONUP||m==WM_LBUTTONDBLCLK||m==WM_MOUSEMOVE||m==WM_MOUSELEAVE)return 0;
    if(m==WM_ERASEBKGND)return 1;if(m==WM_PAINT||m==WM_PRINTCLIENT){PaintBuffer paint(h,reinterpret_cast<HDC>(w));HDC dc=paint.dc;RECT r=paint.rect;fillSurface(dc,r,h);if(glassBackdrop(h))translucentRound(dc,r,16,dip(h,7));else roundRect(dc,r,raised,dip(h,7));int index=int(SendMessageW(h,CB_GETCURSEL,0,0));wchar_t value[512]{};if(index>=0&&SendMessageW(h,CB_GETLBTEXTLEN,index,0)<512)SendMessageW(h,CB_GETLBTEXT,index,LPARAM(value));auto old=SelectObject(dc,HFONT(SendMessageW(h,WM_GETFONT,0,0)));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(h)?secondary:RGB(85,88,92));RECT label=r;label.left+=dip(h,12);label.right-=dip(h,26);glassText(dc,value,-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);{AlphaGraphics drawing(dc);auto& g=drawing.get();g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Gdiplus::Pen pen(color(secondary),float(dip(h,1)));float x=float(r.right-dip(h,15)),y=r.bottom/2.f;g.DrawLine(&pen,x-dip(h,3),y-dip(h,1),x,y+dip(h,2));g.DrawLine(&pen,x,y+dip(h,2),x+dip(h,3),y-dip(h,1));}SelectObject(dc,old);return 0;}
    if(controlVisualChange(m)||m==CB_SETCURSEL||m==CB_RESETCONTENT||m==CB_ADDSTRING||m==CB_DELETESTRING||m==WM_KEYDOWN||m==WM_CHAR)return updateControlModel(h,m,w,l);
    return DefSubclassProc(h,m,w,l);
}
inline LRESULT CALLBACK trackPaint(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(m==WM_MOUSEWHEEL||m==WM_MOUSEHWHEEL)return 0; // Scrolling must not seek or change volume/parameters.
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PaintBuffer paint(h,reinterpret_cast<HDC>(w));HDC dc=paint.dc;RECT r=paint.rect;fillSurface(dc,r,h);int minimum=int(SendMessageW(h,TBM_GETRANGEMIN,0,0)),maximum=int(SendMessageW(h,TBM_GETRANGEMAX,0,0));float progress=float(SendMessageW(h,TBM_GETPOS,0,0)-minimum)/std::max(1,maximum-minimum);int margin=dip(h,6),y=r.bottom/2,x=margin+int((r.right-margin*2)*progress);RECT rail{margin,y-dip(h,1),r.right-margin,y+dip(h,2)},fill=rail;fill.right=x;roundRect(dc,rail,line,dip(h,2));roundRect(dc,fill,IsWindowEnabled(h)?accent:secondary,dip(h,2));RECT dot{x-dip(h,5),y-dip(h,5),x+dip(h,5),y+dip(h,5)};roundRect(dc,dot,IsWindowEnabled(h)?accent:line,dip(h,5));InflateRect(&dot,-dip(h,2),-dip(h,2));roundRect(dc,dot,surface(h),dip(h,3));return 0;}
    if(m==WM_LBUTTONDOWN||m==WM_MOUSEMOVE&&GetCapture()==h||m==WM_LBUTTONUP&&GetCapture()==h){if(!IsWindowEnabled(h))return 0;if(m==WM_LBUTTONDOWN){SetFocus(h);SetCapture(h);}RECT r{};GetClientRect(h,&r);int minimum=int(SendMessageW(h,TBM_GETRANGEMIN,0,0)),maximum=int(SendMessageW(h,TBM_GETRANGEMAX,0,0));float fraction=std::clamp(float(GET_X_LPARAM(l)-dip(h,6))/std::max(1L,r.right-dip(h,12)),0.0f,1.0f);int value=minimum+int(fraction*(maximum-minimum));SendMessageW(h,TBM_SETPOS,TRUE,value);SendMessageW(GetParent(h),WM_HSCROLL,MAKEWPARAM(m==WM_LBUTTONUP?TB_THUMBPOSITION:TB_THUMBTRACK,value),LPARAM(h));if(m==WM_LBUTTONUP)ReleaseCapture();return 0;}
    if(controlVisualChange(m)||m==TBM_SETPOS||m==TBM_SETRANGE||m==WM_SIZE||m==WM_KEYDOWN||m==WM_KEYUP)return updateControlModel(h,m,w,l);
    return DefSubclassProc(h,m,w,l);
}
// Keep the native EDIT model (selection, IME, scrolling, accessibility), but
// composite its printed glyphs onto the same alpha surface as its parent.
inline LRESULT CALLBACK editGlassPaint(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(!glassBackdrop(h))return DefSubclassProc(h,m,w,l);
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_KEYDOWN&&w=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(h,EM_SETSEL,0,-1);return 0;}
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PaintBuffer paint(h,reinterpret_cast<HDC>(w));fillSurface(paint.dc,paint.rect,h);AlphaRaster ink;
        if(ink.create(paint.dc,paint.rect.right,paint.rect.bottom)){SetPropW(h,L"Veyra.EditPrint",HANDLE(1));DefSubclassProc(h,WM_PRINTCLIENT,WPARAM(ink.dc),PRF_CLIENT);RemovePropW(h,L"Veyra.EditPrint");GdiFlush();
            for(size_t i=0;i<size_t(ink.width)*ink.height;++i){auto p=ink.pixels[i];auto maximum=std::max({p&255,(p>>8)&255,(p>>16)&255});auto alpha=std::min(255u,maximum*255/240);ink.pixels[i]=(p&0xffffff)|(alpha<<24);}
            AlphaGraphics drawing(paint.dc);Gdiplus::Bitmap bitmap(ink.width,ink.height,ink.width*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(ink.pixels));drawing.get().DrawImage(&bitmap,0,0);
        }return 0;}
    if(m==WM_SETTEXT||m==WM_CHAR||m==WM_KEYDOWN||m==WM_LBUTTONDOWN||m==WM_LBUTTONUP||m==WM_MOUSEMOVE&&(w&MK_LBUTTON)||m==WM_HSCROLL||m==WM_VSCROLL||m==EM_SETSEL||m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_PASTE||m==WM_CUT||m==WM_CLEAR||m==WM_UNDO||m==EM_UNDO||m==EM_REPLACESEL||m==EM_SCROLL||m==EM_LINESCROLL||m==WM_MOUSEWHEEL||m==WM_IME_COMPOSITION||m==WM_IME_ENDCOMPOSITION||m==WM_SIZE||m==WM_SETFONT||m==WM_ENABLE)return updateControlModel(h,m,w,l);
    if(controlVisualChange(m))return updateControlModel(h,m,w,l);
    return DefSubclassProc(h,m,w,l);
}
inline void themeControl(HWND h){wchar_t cls[32]{};GetClassNameW(h,cls,32);SetWindowTheme(h,L"DarkMode_Explorer",nullptr);if(_wcsicmp(cls,L"BUTTON")==0)SetWindowSubclass(h,buttonPaint,900,0);else if(_wcsicmp(cls,L"COMBOBOX")==0){SetWindowLongPtrW(h,GWL_EXSTYLE,GetWindowLongPtrW(h,GWL_EXSTYLE)&~WS_EX_CLIENTEDGE);SetWindowSubclass(h,comboPaint,901,0);SendMessageW(h,CB_SETITEMHEIGHT,WPARAM(-1),dip(h,30));}else if(_wcsicmp(cls,TRACKBAR_CLASSW)==0)SetWindowSubclass(h,trackPaint,902,0);else if(_wcsicmp(cls,L"EDIT")==0)SetWindowSubclass(h,editGlassPaint,903,0);else if(_wcsicmp(cls,L"STATIC")==0){auto type=GetWindowLongPtrW(h,GWL_STYLE)&SS_TYPEMASK;if(type==SS_LEFT||type==SS_CENTER||type==SS_RIGHT||type==SS_SIMPLE||type==SS_LEFTNOWORDWRAP)SetWindowSubclass(h,labelPaint,904,0);}}
inline void marked(HWND h,bool on=true){if(on)SetPropW(h,L"veyra.accent",HANDLE(1));else RemovePropW(h,L"veyra.accent");InvalidateRect(h,nullptr,FALSE);}
inline void ghost(HWND h,bool on=true){if(on)SetPropW(h,L"veyra.ghost",HANDLE(1));else RemovePropW(h,L"veyra.ghost");InvalidateRect(h,nullptr,FALSE);}
inline void selected(HWND h,bool on){if((GetPropW(h,L"veyra.selected")!=nullptr)==on)return;if(on)SetPropW(h,L"veyra.selected",HANDLE(1));else RemovePropW(h,L"veyra.selected");InvalidateRect(h,nullptr,FALSE);}
inline void setText(HWND h,const std::wstring& value){wchar_t old[4096]{};GetWindowTextW(h,old,4096);if(value!=old)SetWindowTextW(h,value.c_str());}
}
