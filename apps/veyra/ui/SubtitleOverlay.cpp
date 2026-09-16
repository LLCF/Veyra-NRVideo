#include "SubtitleOverlay.h"

#include "Theme.h"
#include "veyra/Log.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace veyra::ui {
namespace {
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_NCHITTEST)return HTTRANSPARENT;
    if(msg==WM_PAINT){PAINTSTRUCT ps{};BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    return DefWindowProcW(h,msg,wp,lp);
}

int toFontStyle(bool bold,bool italic){
    int style=Gdiplus::FontStyleRegular;
    if(bold)style|=Gdiplus::FontStyleBold;
    if(italic)style|=Gdiplus::FontStyleItalic;
    return style;
}
// numpad layout: column 1/2/3 = left/centre/right, row 1/2/3 = bottom/middle/top
Gdiplus::StringAlignment horizontalOf(int alignment){const int column=(alignment-1)%3;return column==0?Gdiplus::StringAlignmentNear:column==2?Gdiplus::StringAlignmentFar:Gdiplus::StringAlignmentCenter;}
Gdiplus::StringAlignment verticalOf(int alignment){const int row=(alignment-1)/3;return row==0?Gdiplus::StringAlignmentFar:row==2?Gdiplus::StringAlignmentNear:Gdiplus::StringAlignmentCenter;}

std::wstring signatureOf(const std::vector<SubtitleLine>& lines,const SubtitleView& view,const RECT& rect){
    std::wstring signature;
    signature.reserve(lines.size()*64);
    for(const auto& line:lines){
        signature+=std::format(L"{}|{:08X}|{:08X}|{}|{:.1f}|{}|{}|{}|",line.text,line.style.primary,line.style.outline,line.style.font,
            line.style.size,line.style.alignment,line.alignOverride,line.secondary?1:0);
    }
    signature+=std::format(L"#{:.2f}|{}|{}|{}|{}|{}",view.scale,view.fontOverride,view.outline?1:0,view.background?1:0,view.bottomMargin,view.blockGap);
    signature+=std::format(L"/{}x{}",rect.right,rect.bottom);
    return signature;
}

struct Line {
    std::unique_ptr<Gdiplus::GraphicsPath> path;
    Gdiplus::RectF bounds{};
    engine::SubtitleStyle style;
    bool positioned=false;
    float positionX=0,positionY=0;
};
} // namespace

HWND createSubtitleOverlay(HWND parent){
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraSubtitleOverlay";RegisterClassW(&wc);
    return CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_CHILD,0,0,1,1,parent,nullptr,wc.hInstance,nullptr);
}

void updateSubtitleOverlay(HWND h,const std::vector<SubtitleLine>& lines,const SubtitleView& view){
    RECT rect{};GetClientRect(h,&rect);
    if(lines.empty()||rect.right<1||rect.bottom<1){ShowWindow(h,SW_HIDE);SetWindowTextW(h,L"");return;}
    const auto signature=signatureOf(lines,view,rect);
    static thread_local std::wstring lastSignature;
    if(lastSignature==signature){ShowWindow(h,SW_SHOWNOACTIVATE);return;}
    const int width=rect.right,height=rect.bottom;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);
    void* bits=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap||!bits){if(bitmap)DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);return;}
    auto previousObject=SelectObject(memory,bitmap);
    memset(bits,0,size_t(width)*height*4);
    {
        using namespace Gdiplus;
        Bitmap canvas(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));
        Graphics graphics(&canvas);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        const float padding=float(dip(h,6));
        const float layoutWidth=std::max(1.0f,float(width)-2*padding);
        FontFamily fallbackFamily(L"Microsoft YaHei UI");   // per call: never survives GdiplusShutdown
        std::vector<Line> laid;
        laid.reserve(lines.size());
        for(const auto& line:lines){
            Line entry;
            entry.path=std::make_unique<Gdiplus::GraphicsPath>();
            entry.style=line.style;
            const double logicalSize=std::max(8.0,(line.style.size>0?line.style.size:24.0)*view.scale);
            const std::wstring family=!view.fontOverride.empty()?view.fontOverride:(line.style.font.empty()?L"Microsoft YaHei UI":line.style.font);
            FontFamily requested(family.c_str());
            FontFamily* use=requested.IsAvailable()?&requested:&fallbackFamily;
            if(!requested.IsAvailable())entry.style.font=L"Microsoft YaHei UI";
            StringFormat format;
            format.SetAlignment(StringAlignmentNear);
            format.SetLineAlignment(StringAlignmentNear);
            format.SetFormatFlags(StringFormatFlagsNoClip|StringFormatFlagsMeasureTrailingSpaces);
            const auto& text=line.text;
            entry.path->AddString(text.c_str(),int(text.size()),use,toFontStyle(line.style.bold,line.style.italic),float(dip(h,logicalSize)),RectF(0,0,layoutWidth,float(height)*2),&format);
            entry.path->GetBounds(&entry.bounds);
            if(line.posX>=0&&line.posY>=0){
                entry.positioned=true;
                entry.positionX=float(line.posX)*float(width);
                entry.positionY=float(line.posY)*float(height);
            }
            laid.push_back(std::move(entry));
        }
        // The caller passes secondary lines first, primary last: the loop below
        // lays them out bottom-up, so the primary cue ends up on the bottom.
        float cursor=float(height)-padding-float(dip(h,view.bottomMargin));
        for(size_t index=laid.size();index-->0;){
            auto& entry=laid[index];
            const int alignment=(entry.style.alignment>=1&&entry.style.alignment<=9)?entry.style.alignment:2;
            const float blockWidth=entry.bounds.Width,blockHeight=entry.bounds.Height;
            float x=padding,y=cursor-blockHeight;
            if(entry.positioned){
                const auto horizontal=horizontalOf(alignment),vertical=verticalOf(alignment);
                x=entry.positionX-(horizontal==StringAlignmentNear?0.0f:horizontal==StringAlignmentFar?blockWidth:blockWidth/2);
                y=entry.positionY-(vertical==StringAlignmentNear?0.0f:vertical==StringAlignmentFar?blockHeight:blockHeight/2);
                y=std::clamp(y,0.0f,std::max(0.0f,float(height)-blockHeight));
                x=std::clamp(x,0.0f,std::max(0.0f,float(width)-blockWidth));
            }else{
                const auto vertical=verticalOf(alignment);
                if(vertical==StringAlignmentNear)y=padding;
                else if(vertical==StringAlignmentCenter)y=std::max(padding,(float(height)-blockHeight)/2);
                const float marginL=float(dip(h,entry.style.marginL));
                const float marginR=float(dip(h,entry.style.marginR));
                const float left=padding+marginL,right=float(width)-padding-marginR;
                const auto horizontal=horizontalOf(alignment);
                x=horizontal==StringAlignmentNear?left:horizontal==StringAlignmentFar?right-blockWidth:left+(right-left-blockWidth)/2;
                if(vertical!=StringAlignmentNear)cursor=y-float(dip(h,view.blockGap));
            }
            Matrix translation;
            translation.Translate(x-entry.bounds.X,y-entry.bounds.Y);
            entry.path->Transform(&translation);
            entry.bounds.X=x;entry.bounds.Y=y;
        }
        for(const auto& entry:laid){
            const auto& style=entry.style;
            const Color primary(Color::MakeARGB(byte((style.primary>>24)&0xFF),byte((style.primary>>16)&0xFF),byte((style.primary>>8)&0xFF),byte(style.primary&0xFF)));
            if(view.background||style.background){
                const float inflate=float(dip(h,2));
                RectF box(entry.bounds.X-inflate,entry.bounds.Y-inflate,entry.bounds.Width+2*inflate,entry.bounds.Height+2*inflate);
                SolidBrush back(Color::MakeARGB(byte((style.back>>24)&0xFF),byte((style.back>>16)&0xFF),byte((style.back>>8)&0xFF),byte(style.back&0xFF)));
                graphics.FillRectangle(&back,box);
            }
            const float shadow=float(dip(h,style.shadow>0?style.shadow:(view.outline?1.8:0.0)));
            if(shadow>0.5f){
                GraphicsPath shadowPath;
                shadowPath.AddPath(entry.path.get(),FALSE);
                Matrix offset;
                offset.Translate(shadow,shadow);
                shadowPath.Transform(&offset);
                SolidBrush shadowBrush(Color(150,0,0,0));
                graphics.FillPath(&shadowBrush,&shadowPath);
            }
            if(view.outline){
                Pen outline(Color::MakeARGB(byte((style.outline>>24)&0xFF),byte((style.outline>>16)&0xFF),byte((style.outline>>8)&0xFF),byte(style.outline&0xFF)),
                            float(dip(h,style.outlineWidth>0?style.outlineWidth:2.4)));
                outline.SetLineJoin(LineJoinRound);
                graphics.DrawPath(&outline,entry.path.get());
            }
            SolidBrush fill(primary);
            graphics.FillPath(&fill,entry.path.get());
        }
        lastSignature=signature;
    }
    SIZE dimensions{width,height};POINT origin{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    if(UpdateLayeredWindow(h,screen,nullptr,&dimensions,memory,&origin,0,&blend,ULW_ALPHA)){
        ShowWindow(h,SW_SHOWNOACTIVATE);
        if(!GetPropW(h,L"subtitle.logged")){
            log::info("subtitle",std::format("layered update succeeded extent={}x{} lines={}",width,height,lines.size()));
            SetPropW(h,L"subtitle.logged",HANDLE(1));
        }
    }else{
        lastSignature.clear();
        if(!GetPropW(h,L"subtitle.error")){
            log::error("subtitle",std::format("UpdateLayeredWindow failed error={}",GetLastError()));
            SetPropW(h,L"subtitle.error",HANDLE(1));
        }
    }
    SelectObject(memory,previousObject);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);
}
} // namespace veyra::ui
