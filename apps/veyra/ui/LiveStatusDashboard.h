#pragma once
#include <deque>
#include <optional>

namespace veyra::ui::live_status {
struct DashboardHistory {
    std::deque<std::optional<double>> points;
    uint64_t session=0,revision=0;unsigned low=0,good=0;bool overloaded=false;
    void sample(const engine::PlayerSnapshot& s){
        if(session!=s.sessionId||revision!=s.applied.revision){points.clear();low=good=0;overloaded=false;session=s.sessionId;revision=s.applied.revision;}
        const bool active=s.running&&!s.image&&s.transport==engine::TransportState::Playing&&!s.applying;
        auto value=active?s.metrics.flow.enhancementProcessing.mean:std::optional<double>{};
        points.push_back(value);if(points.size()>120)points.pop_front();
        const bool xess=s.applied.multiplier>1&&engine::presentSinkFrameGeneration(s.applied.frameGenerationBackend);
        const double actual=xess?s.metrics.flow.xessSdkSubmitFps:s.metrics.flow.presentSubmitFps;
        const double target=s.nominalSourceFps*(s.captureHalfRate?.5:1)*s.applied.multiplier;
        if(!active||!s.metrics.flow.rateWindowReady||target<=0){low=good=0;overloaded=false;return;}
        if(actual<target*.95){good=0;if(++low>=8)overloaded=true;}
        else{low=0;if(++good>=8)overloaded=false;}
    }
};
inline void paintDashboard(HWND h,HDC dc,int w,int height,const engine::PlayerSnapshot& s,const DashboardHistory& history,bool advanced=false){
    const auto& f=s.metrics.flow;const bool active=s.running&&!s.image&&s.transport==engine::TransportState::Playing;
    auto text=[&](std::wstring str,int x,int y,int width,int ht,int size,COLORREF color){chromeText(dc,h,str,x,y,width,ht,size,color);};
    auto card=[&](int x,int y,int width,int ht){
        AlphaGraphics draw(dc);auto& g=draw.get();g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath p;const float r=float(dip(h,12)),xx=float(dip(h,x)),yy=float(dip(h,y)),ww=float(dip(h,width)),hh=float(dip(h,ht));
        p.AddArc(xx,yy,r,r,180,90);p.AddArc(xx+ww-r,yy,r,r,270,90);p.AddArc(xx+ww-r,yy+hh-r,r,r,0,90);p.AddArc(xx,yy+hh-r,r,r,90,90);p.CloseFigure();
        Gdiplus::SolidBrush bg(Gdiplus::Color(155,29,32,32));g.FillPath(&bg,&p);
        Gdiplus::Pen edge(Gdiplus::Color(40,132,145,140),1);g.DrawPath(&edge,&p);
    };
    text(L"实时处理状态",12,7,w-100,24,12,textColor);
    const int gap=6,left=10,cw=(w-20-gap*3)/4,top=38;
    const diagnostics::GpuStage stages[]={diagnostics::GpuStage::Flow,diagnostics::GpuStage::Nr,diagnostics::GpuStage::Sr,diagnostics::GpuStage::FgBatch};
    const wchar_t* names[]={L"光流耗时",L"NR耗时",L"超分耗时",L"帧生成耗时"};
    for(int i=0;i<4;++i){int x=left+i*(cw+gap);card(x,top,cw,58);text(names[i],x+6,top+7,cw-10,17,9,secondary);
        const auto& a=f.gpuTiming[size_t(stages[i])];std::wstring value=L"—";
        if(active){if(a.mean)value=std::format(L"{:.1f}",*a.mean);else if(s.metrics.gpu[size_t(stages[i])].state==diagnostics::SampleState::NotExecuted)value=(i==3&&s.applied.multiplier>1)?L"等待补帧":L"未开启";}
        // Present-sink FG backends now report an application-side GPU sample;
        // show it instead of a hardcoded placeholder, and only say
        // "sampling" while no sample has arrived yet.
        if(i==3&&s.applied.multiplier>1&&engine::presentSinkFrameGeneration(s.applied.frameGenerationBackend)&&!a.mean)value=L"采样中";
        text(value,x+6,top+27,cw-10,24,14,textColor);
    }
    const int chartTop=104,chartH=std::max(92,height-188),bottom=chartTop+chartH+8,bw=(w-26)/2;
    card(10,chartTop,w-20,chartH);
    const bool xess=s.applied.multiplier>1&&engine::presentSinkFrameGeneration(s.applied.frameGenerationBackend);
    text(advanced?L"精细数据":xess?L"增强处理耗时 · 不含显示补帧":L"总增强处理耗时",20,chartTop+8,w-100,20,10,secondary);
    text(advanced?L"◂":L"▸",w-38,chartTop+5,24,24,14,secondary);
    if(!advanced){
    const auto extra=active?f.enhancementProcessing.mean:std::optional<double>{};
    text(extra?std::format(L"{:.1f}",*extra):L"—",w-103,chartTop+30,85,30,23,textColor);
    text(L"ms · 近1秒平均",w-103,chartTop+62,85,18,9,secondary);
    const int x0=20,x1=w-117,y0=chartTop+35,y1=chartTop+chartH-24;
    if(x1>x0&&y1>y0){
        double peak=10;for(const auto& v:history.points)if(v)peak=std::max(peak,*v*1.15);
        {AlphaGraphics draw(dc);auto& g=draw.get();g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen grid(Gdiplus::Color(50,150,160,155),1),line(Gdiplus::Color(240,203,219,210),float(dip(h,1)));
        grid.SetDashStyle(Gdiplus::DashStyleDash);for(int i=0;i<3;++i){int y=y0+(y1-y0)*i/2;g.DrawLine(&grid,dip(h,x0),dip(h,y),dip(h,x1),dip(h,y));}
        std::optional<Gdiplus::PointF> prev;size_t index=120-history.points.size();
        for(const auto& v:history.points){const float x=float(dip(h,x0))+float(dip(h,x1-x0))*float(index++)/119;
            if(!v){prev.reset();continue;}Gdiplus::PointF p(x,float(dip(h,y1))-float(dip(h,y1-y0))*float(*v/peak));if(prev)g.DrawLine(&line,*prev,p);prev=p;}}
        text(L"30秒前",x0,y1+3,65,16,8,secondary);text(L"现在",x1-28,y1+3,28,16,8,secondary);
    }
    }
    card(10,bottom,bw,58);card(16+bw,bottom,bw,58);
    text(L"待输出画面",20,bottom+6,bw-20,18,10,secondary);
    text(std::format(L"{} 帧{}",active?f.pendingOutputFrames:0,xess?L" *":L""),20,bottom+27,bw-20,23,17,textColor);
    std::wstring status=L"待机";COLORREF color=RGB(145,151,149);
    if(s.failed){status=L"错误";color=RGB(245,86,86);}
    else if(s.remoteRecovering){status=L"恢复中";color=RGB(242,185,65);}
    else if(active&&!s.applying&&f.rateWindowReady){status=history.overloaded?(s.applied.multiplier>1?L"补帧受限":L"处理过载"):L"正常";color=history.overloaded?RGB(242,185,65):RGB(111,211,127);}
    else if(s.applying&&s.running)status=L"调整中";else if(active)status=L"采样中";else if(s.transport==engine::TransportState::Paused)status=L"已暂停";
    text(L"当前状态",26+bw,bottom+6,bw-20,18,10,secondary);
    {AlphaGraphics draw(dc);Gdiplus::SolidBrush dot(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)));draw.get().FillEllipse(&dot,dip(h,27+bw),dip(h,bottom+35),dip(h,8),dip(h,8));}
    text(status,42+bw,bottom+27,bw-38,23,16,textColor);
    text(xess?L"* 帧生成耗时为应用侧计时，不含提供方内部插值":L"增强阶段GPU计时 · 不含音频和呈现等待",12,height-18,w-24,16,8,secondary);
}
}
