#include "veyra/engine/EngineController.h"
#include "veyra/Log.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

int wmain(int argc,wchar_t** argv){
    using namespace veyra::engine;
    using Clock=std::chrono::steady_clock;
    if(argc<3)return 2;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::filesystem::create_directories(argv[2]);
    veyra::Logger::instance().openFile((std::filesystem::path(argv[2])/L"engine.log").wstring());
    veyra::Logger::instance().setConsoleEnabled(false);
    RECT windowRect{0,0,1155,741};AdjustWindowRect(&windowRect,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowExW(0,L"STATIC",L"Player track regression",WS_OVERLAPPEDWINDOW,0,0,windowRect.right-windowRect.left,windowRect.bottom-windowRect.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ShowWindow(window,SW_SHOWNOACTIVATE);
    int failures=0;
    {
        EngineController engine;engine.setVolume(0,true);
        auto wait=[&](auto predicate,int seconds=25){
            const auto deadline=Clock::now()+std::chrono::seconds(seconds);
            while(Clock::now()<deadline){
                MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                const auto state=engine.snapshot();if(state.failed)return false;if(predicate(state))return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }return false;
        };
        auto check=[&](bool ok,const char* label){const auto s=engine.snapshot();std::cout<<(ok?"PASS ":"FAIL ")<<label<<" frames="<<s.frames<<" generated="<<s.generated<<" position="<<s.position<<" audio="<<s.selectedAudioTrack<<std::endl;if(!ok)std::wcerr<<s.status<<L" / "<<s.backendWarning<<std::endl;failures+=!ok;return ok;};
        const bool liveAmd=argc>3&&std::wstring_view(argv[3])==L"xess-live-amd";
        PlayerOptions options;options.captureReplayForTest=liveAmd;
        if(liveAmd)options.settings.opticalFlowBackend=OpticalFlowBackend::AmdFidelityFx;
        const auto opened=Clock::now();engine.open(window,argv[1],options);
        check(wait([](const auto& s){return s.frames>0;}),"first frame");
        std::cout<<"openMs="<<std::chrono::duration<double,std::milli>(Clock::now()-opened).count()<<std::endl;
        if(argc>3&&(std::wstring_view(argv[3])==L"xess"||liveAmd)){
            for(const bool nrFirst:{true,false,true,false}){
                if(failures)break;
                auto change=[&](bool nr,bool xess){auto s=engine.snapshot();auto settings=s.desired;settings.nr=nr;settings.multiplier=xess?2:1;settings.frameGenerationBackend=FrameGenerationBackend::XeSS;
                    engine.requestSettings(settings);const auto revision=engine.snapshot().desired.revision;
                    return check(wait([&](const auto& v){return !v.applying&&v.applied.revision==revision&&v.nrActive==nr&&v.applied.multiplier==settings.multiplier&&v.frames>=s.frames+30&&(!xess||v.generated>=s.generated+10);},40),nr?(xess?"NR+XeSS output":"NR output"):(xess?"XeSS output":"plain output"));};
                if(!change(false,false)||!change(nrFirst,!nrFirst)||!change(true,true)||!change(false,false))break;
            }
        }else{
            check(wait([](const auto& s){return !s.audioTracks.empty();}),"enumerate audio tracks");
            const auto initial=engine.snapshot();
            check(!engine.selectAudioTrack(initial.sessionId+1,initial.selectedAudioTrack),"reject stale session");
            check(!engine.selectAudioTrack(initial.sessionId,99999),"reject invalid stream");
            for(const bool paused:{false,true}){
                engine.pause(paused);
                for(const auto& track:initial.audioTracks){
                    if(failures)break;
                    const auto before=engine.snapshot();
                    if(track.streamIndex==before.selectedAudioTrack)continue;
                    const auto start=Clock::now();
                    check(engine.selectAudioTrack(before.sessionId,track.streamIndex),"request audio track");
                    check(wait([&](const auto& s){return s.selectedAudioTrack==track.streamIndex&&s.seekPresented>=s.seekRequested&&s.seekRequested>before.seekRequested&&s.transport==(paused?TransportState::Paused:TransportState::Playing)&&s.audioInputChannels==track.channels;}),"switch preserves transport and layout");
                    const auto after=engine.snapshot();check(std::abs(after.position-before.position)<3,"switch preserves position");
                    std::cout<<"switchMs="<<std::chrono::duration<double,std::milli>(Clock::now()-start).count()<<std::endl;
                }
            }
            for(double target:{30.0,1200.0,90.0}){
                if(failures)break;const auto start=Clock::now();engine.seek(target);const auto id=engine.snapshot().seekRequested;
                check(wait([&](const auto& s){return s.seekPresented>=id;}),"seek presents target");
                std::cout<<"seekMs="<<std::chrono::duration<double,std::milli>(Clock::now()-start).count()<<std::endl;
            }
        }
        engine.stop();check(wait([&](const auto&){return engine.idle();}),"stop");
    }
    DestroyWindow(window);CoUninitialize();return failures?1:0;
}
