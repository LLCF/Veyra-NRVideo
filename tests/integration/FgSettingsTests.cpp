#include "veyra/engine/EngineController.h"
#include "veyra/engine/FgCompatibilityProbe.h"
#include "veyra/Log.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

int wmain(int argc,wchar_t** argv) {
    using namespace veyra::engine;
    using namespace std::chrono_literals;
    wchar_t executable[32768]{};
    if(!GetModuleFileNameW(nullptr,executable,32768))return 2;
    setFgCompatibilityProbeExecutable(executable);
    if(argc==3&&std::wstring_view(argv[1])==L"--fg-compat-probe")
        return runFgCompatibilityProbe(reinterpret_cast<HANDLE>(_wcstoui64(argv[2],nullptr,10)));
    if(argc!=3&&argc!=4)return 2;
    const bool probeFailure=argc==4&&std::wstring_view(argv[3])==L"probe-failure";
    if(argc==4&&!probeFailure)return 2;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com))return 2;
    std::filesystem::create_directories(argv[2]);
    veyra::Logger::instance().openFile((std::filesystem::path(argv[2])/L"engine.log").wstring());
    veyra::Logger::instance().setConsoleEnabled(false);
    HWND window=CreateWindowExW(0,L"STATIC",L"FG settings test",WS_POPUP,0,0,960,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window){CoUninitialize();return 2;}
    int failures=0;
    {
        EngineController engine;
        engine.setVolume(0,true);
        auto wait=[&](auto predicate,int seconds=30,bool allowFailed=false){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
            while(std::chrono::steady_clock::now()<deadline){
                MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                const auto s=engine.snapshot();
                if(s.failed&&!allowFailed)return false;
                if(predicate(s))return true;
                std::this_thread::sleep_for(10ms);
            }
            return false;
        };
        auto check=[&](bool ok,const char* label){
            const auto s=engine.snapshot();
            std::cout<<(ok?"PASS ":"FAIL ")<<label<<" desired="<<s.desired.multiplier
                <<" applied="<<s.applied.multiplier<<" frames="<<s.frames<<" generated="<<s.generated
                <<" cap="<<s.fgMultiFrameMax<<" failed="<<s.failed<<std::endl;
            if(!ok)std::wcerr<<s.status<<L" / "<<s.backendWarning<<std::endl;
            failures+=!ok;return ok;
        };
        engine.open(window,argv[1],{});
        check(wait([](const auto& s){return s.frames>=12&&!s.fgActive&&s.generated==0;}),"plain playback");
        if(probeFailure&&!failures){
            const auto before=engine.snapshot();
            auto settings=before.desired;
            settings.nr=settings.sr=false;
            settings.frameGenerationBackend=FrameGenerationBackend::Dlss;
            settings.multiplier=6;
            const auto started=std::chrono::steady_clock::now();
            check(engine.requestSettings(settings),"request failing provider");
            check(wait([&](const auto& s){return !s.applying&&!s.fgActive&&!s.backendWarning.empty()&&
                s.applied.multiplier==1&&s.frames>=before.frames+12&&s.generated==before.generated;},15),
                "failed probe restores advancing playback within 15 seconds");
            std::cout<<"Probe recovery elapsedMs="<<std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now()-started).count()<<std::endl;
        }
        for(const unsigned multiplier:{2u,6u,1u,6u}){
            if(probeFailure)break;
            if(failures)break;
            const auto beforeSwitch=engine.snapshot();
            auto settings=beforeSwitch.desired;
            settings.nr=settings.sr=false;
            settings.frameGenerationBackend=FrameGenerationBackend::Dlss;
            settings.multiplier=multiplier;
            if(!check(engine.requestSettings(settings),"request multiplier"))break;
            const auto revision=engine.snapshot().desired.revision;
            if(!check(wait([&](const auto& s){return !s.applying&&s.applied.revision==revision&&
                s.applied.multiplier==multiplier&&s.fgActive==(multiplier>1)&&s.frames>=beforeSwitch.frames+12&&
                (multiplier==1||(s.generated>=beforeSwitch.generated+10&&s.fgMultiFrameMax>=int(multiplier)-1));},70),"requested multiplier produces new output"))break;
            if(multiplier!=6)continue;
            engine.pause(true);
            check(wait([](const auto& s){return s.transport==TransportState::Paused;}),"pause");
            const auto before=engine.snapshot();
            engine.seek(before.position+2.0);
            const auto request=engine.snapshot().seekRequested;
            if(!check(wait([&](const auto& s){return s.seekPresented>=request;}),"paused seek presents requested frame"))break;
            engine.pause(false);
            const auto generated=engine.snapshot().generated;
            check(wait([&](const auto& s){return s.transport==TransportState::Playing&&s.fgActive&&
                s.applied.multiplier==6&&s.generated>generated+10;}),"6X resumes after history reset");
        }
        engine.stop();
        check(wait([&](const auto&){return engine.idle();},20,true),"engine drains and stops");
    }
    DestroyWindow(window);
    CoUninitialize();
    std::cout<<"This test checks settings, output counts and lifecycle; not target-card support or interpolation quality.\n";
    return failures?1:0;
}
