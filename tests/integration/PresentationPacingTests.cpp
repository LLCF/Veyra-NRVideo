#include "veyra/engine/EngineController.h"
#include "veyra/Log.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
int wmain(int argc,wchar_t** argv){
    using namespace veyra::engine;using Clock=std::chrono::steady_clock;
    // media, evidence directory, mode(-1=off), display(0..2), multiplier, seconds, lifecycle/replay/xess
    if(argc<7)return 2;
    const int mode=_wtoi(argv[3]),display=_wtoi(argv[4]),multiplier=_wtoi(argv[5]),seconds=std::clamp(_wtoi(argv[6]),3,120);
    if(mode< -1||mode>2||display<0||display>2)return 2;
    const std::wstring scenario=argc>7?argv[7]:L"";
    if(scenario==L"legacy"){if(mode!=-1)return 2;SetEnvironmentVariableW(L"VEYRA_TEST_LEGACY_SWAPCHAIN",L"1");}
    std::filesystem::path dir=argv[2];std::filesystem::create_directories(dir);
    SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS",L"1");
    veyra::Logger::instance().openFile((dir/L"engine.log").wstring());veyra::Logger::instance().setConsoleEnabled(false);
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    HWND window=CreateWindowExW(0,L"STATIC",L"Veyra pacing acceptance",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,1280,760,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 2;
    MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    DEVMODEW dm{};dm.dmSize=sizeof(dm);EnumDisplaySettingsW(monitor.szDevice,ENUM_CURRENT_SETTINGS,&dm);
    std::cout<<"display="<<dm.dmPelsWidth<<"x"<<dm.dmPelsHeight<<" refreshHz="<<dm.dmDisplayFrequency<<std::endl;
    int failures=0;auto check=[&](bool value,const char* name){std::cout<<(value?"PASS ":"FAIL ")<<name<<std::endl;failures+=!value;};
    {
        EngineController engine;engine.setVolume(0,true);
        PresentationSettings p{mode>=0,PacingMode(std::max(0,mode)),DisplaySync(display)};engine.requestPresentation(p);
        EnhancementSettings effects;effects.multiplier=unsigned(multiplier);if(scenario==L"xess")effects.frameGenerationBackend=FrameGenerationBackend::XeSS;
        if(scenario==L"effects"){effects.nr=true;effects.sr=true;effects.srTarget=veyra::pipeline::SrTarget::Qhd;}
        auto options=PlayerOptions::from(effects);options.captureReplayForTest=scenario==L"replay";
        engine.open(window,argv[1],options);
        auto pump=[] {MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}};
        auto wait=[&](auto predicate,int ms=15000){auto end=Clock::now()+std::chrono::milliseconds(ms);while(Clock::now()<end){pump();auto s=engine.snapshot();if(s.failed)return false;if(predicate(s))return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;};
        check(wait([](auto& s){return s.frames>10;}),"first frames");
        std::ofstream samples(dir/L"snapshots.csv");samples<<"wall,position,frames,generated,lateMs,queueReadyMs,presentMs,skipped,effective,mode,sync,revision\n";
        const auto start=Clock::now();auto last=engine.snapshot();const auto first=last;
        while(Clock::now()-start<std::chrono::seconds(seconds)&&!last.failed){
            pump();last=engine.snapshot();samples<<std::chrono::duration<double>(Clock::now()-start).count()<<','<<last.position<<','<<last.frames<<','<<last.generated<<','<<last.lateMs<<','<<last.schedulingWaitP95Ms<<','<<last.presentCpuP95Ms<<','<<last.previewSkipped<<','<<last.presentationEffective.enabled<<','<<unsigned(last.presentationEffective.mode)<<','<<unsigned(last.presentationEffective.display)<<','<<last.presentationRevision<<'\n';
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        const auto speed=(last.position-first.position)/std::chrono::duration<double>(Clock::now()-start).count();
        std::cout<<"mediaSpeed="<<speed<<" processedDelta="<<last.frames-first.frames<<std::endl;
        check(!last.failed&&last.frames>first.frames+10&&speed>.8&&speed<1.2,"sustained playback at media speed");
        if(multiplier>1)check(last.fgActive&&last.generated>0,"real provider generated frames");
        if(scenario==L"effects")check(last.nrActive&&last.srActive&&last.nrEvaluated>first.nrEvaluated,"NR and SR remain active with pacing");
        if(mode>=0&&scenario!=L"xess")check(last.presentationEffective.enabled,"optional pacing active");
        if(mode==2&&multiplier==1)check(last.presentationEffective.mode==PacingMode::Reflex,"native Reflex active");
        if(mode==2&&multiplier>1&&scenario!=L"xess")check(last.presentationEffective.mode==PacingMode::LowQueue,"Reflex with FG explicitly falls back to low queue");
        if(mode>=0&&scenario==L"xess")check(!last.presentationEffective.enabled&&last.fgActive,"provider remains pacing owner");
        if(scenario==L"lifecycle"){
            engine.pause(true);check(wait([](auto& s){return s.transport==TransportState::Paused;}),"pause");
            p.enabled=false;engine.requestPresentation(p);check(wait([](auto& s){return !s.presentationEffective.enabled;}),"disable while paused");
            engine.seek(5);auto seek=engine.snapshot().seekRequested;check(wait([&](auto& s){return s.seekPresented>=seek;}),"paused seek");
            engine.pause(false);check(wait([](auto& s){return s.position>5.3;}),"resume");
            for(unsigned i=0;i<3;++i){const auto revision=engine.snapshot().presentationRevision;p={true,PacingMode(i),DisplaySync(i)};engine.requestPresentation(p);check(wait([&](auto& s){return s.presentationRevision>revision&&s.presentation==p&&s.presentationEffective.enabled&&s.presentationEffective.mode==(i==2&&multiplier>1?PacingMode::LowQueue:PacingMode(i))&&s.presentationEffective.display==DisplaySync(i);}),"live mode change applied");}
            SetWindowPos(window,nullptr,30,30,960,600,SWP_NOZORDER);p.enabled=false;engine.requestPresentation(p);check(wait([](auto& s){return !s.presentationEffective.enabled;}),"off after resize");
            const auto before=engine.snapshot().frames;check(wait([&](auto& s){return s.frames>before+10;}),"off keeps playing");
        }
        if(scenario==L"overload"){
            SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",L"80");
            auto before=engine.snapshot();const auto begin=Clock::now();
            check(wait([&](auto& s){return s.position>before.position+2.5;}),"overload progresses");
            const auto after=engine.snapshot();const auto rate=(after.position-before.position)/std::chrono::duration<double>(Clock::now()-begin).count();
            std::cout<<"overloadSpeed="<<rate<<" skippedDelta="<<after.previewSkipped-before.previewSkipped<<std::endl;
            check(rate>.8&&rate<1.2&&after.previewSkipped>before.previewSkipped,"overload stays real time with reported skips");
            p.enabled=false;engine.requestPresentation(p);
            check(wait([](auto& s){return !s.presentationEffective.enabled;},1000),"disable during overload within one second");
            SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);
            before=engine.snapshot();check(wait([&](auto& s){return s.frames>before.frames+20;}),"recover after overload");
        }
        engine.stop();check(wait([&](auto&){return engine.idle();}),"clean stop");
    }
    DestroyWindow(window);CoUninitialize();return failures?1:0;
}
