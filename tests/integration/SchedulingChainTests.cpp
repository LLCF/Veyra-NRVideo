#include "veyra/engine/EngineController.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/Log.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>

int wmain(int argc,wchar_t** argv){
    using namespace veyra::engine;
    using Clock=std::chrono::steady_clock;
    if(argc!=4)return 2; // media, output directory, playback/xess/export
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    const std::filesystem::path directory=argv[2];
    std::filesystem::create_directories(directory);
    veyra::Logger::instance().openFile((directory/L"engine.log").wstring());
    veyra::Logger::instance().setConsoleEnabled(false);
    int failures=0;
    auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;failures+=!ok;};
    if(std::wstring_view(argv[3])==L"export"){
        for(unsigned stopAfter:{1u,3u,5u,0u}){
            std::atomic<bool> cancel=false;unsigned boundaries=0;uint64_t sources=0,encoded=0;
            const auto output=directory/(L"export-"+std::to_wstring(stopAfter)+L".mp4");
            const bool ok=exportVideo(argv[1],output.wstring(),{},false,cancel,
                [](double,const std::wstring&){},12,
                [&]{if(stopAfter&&boundaries++>=stopAfter){cancel=true;return false;}return true;},
                [&](const ExportCounts& c){sources=c.source;encoded=c.encoded;});
            std::cout<<"stopAfter="<<stopAfter<<" sources="<<sources<<" encoded="<<encoded<<std::endl;
            check(stopAfter?(!ok&&cancel&&sources==stopAfter&&!std::filesystem::exists(output)&&std::filesystem::exists(output.wstring()+L".partial")):(ok&&sources==12&&encoded==12&&std::filesystem::exists(output)),stopAfter?"cancel with encoder frames pending":"export following cancellations");
        }
    }else{
        HWND window=CreateWindowExW(0,L"STATIC",L"Scheduling regression",WS_OVERLAPPEDWINDOW,
            0,0,960,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window){CoUninitialize();return 2;}
        ShowWindow(window,SW_SHOWNOACTIVATE);
        {
            EngineController engine;engine.setVolume(0,true);
            auto pump=[] {MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}};
            auto wait=[&](auto predicate,int milliseconds=10000){
                const auto end=Clock::now()+std::chrono::milliseconds(milliseconds);
                while(Clock::now()<end){pump();const auto s=engine.snapshot();if(s.failed)return false;if(predicate(s))return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return false;
            };
            auto delay=[&](int milliseconds){const auto end=Clock::now()+std::chrono::milliseconds(milliseconds);while(Clock::now()<end){pump();std::this_thread::sleep_for(std::chrono::milliseconds(5));}};
            const bool xess=std::wstring_view(argv[3])==L"xess";
            EnhancementSettings settings;
            if(xess){settings.multiplier=2;settings.frameGenerationBackend=FrameGenerationBackend::XeSS;}
            engine.open(window,argv[1],PlayerOptions::from(settings));
            check(wait([](const auto& s){return s.frames>=10;}),"first frames");
            check(engine.snapshot().audioTracks.empty(),"fixture has no audio clock");
            auto interval=[&](int milliseconds,const char* label){
                const auto before=engine.snapshot();const auto start=Clock::now();delay(milliseconds);
                const auto after=engine.snapshot();const double wall=std::chrono::duration<double>(Clock::now()-start).count();
                const double speed=(after.position-before.position)/wall;
                std::cout<<label<<" wall="<<wall<<" media="<<after.position-before.position<<" speed="<<speed<<" skipped="<<after.previewSkipped<<" lateMs="<<after.lateMs<<std::endl;
                check(!after.failed&&speed>.80&&speed<1.20,label);
                return after;
            };
            SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",L"60");
            const auto overloaded=interval(3000,"silent overload keeps wall-clock pace");
            check(overloaded.previewSkipped>0,"expired source opportunities skipped");
            if(xess){
                check(wait([](const auto& s){return s.xessGenerationSuppressed;},5000),"XeSS suppresses sustained overload");
                SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",L"10");
                const auto before=engine.snapshot();
                interval(5000,"XeSS moderate-load pace");
                const auto after=engine.snapshot();
                std::cout<<"XeSS suppressed="<<after.xessGenerationSuppressed<<" generatedDelta="<<after.generated-before.generated<<std::endl;
                check(!after.xessGenerationSuppressed&&after.generated>before.generated+30,"XeSS resumes at sustainable load");
            }
            SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);
            interval(2000,"recovery pace");
            engine.pause(true);check(wait([](const auto& s){return s.transport==TransportState::Paused;}),"pause");
            delay(200);const auto paused=engine.snapshot();delay(400);
            check(std::abs(engine.snapshot().position-paused.position)<.05,"paused position stable");
            engine.seek(12);const auto seekId=engine.snapshot().seekRequested;
            check(wait([&](const auto& s){return s.seekPresented>=seekId&&std::abs(s.position-12)<.15;}),"paused seek");
            delay(300);engine.pause(false);
            check(wait([](const auto& s){return s.transport==TransportState::Playing&&s.position>12.1;}),"resume");
            interval(2000,"resumed pace");
            engine.stop();check(wait([&](const auto&){return engine.idle();}),"stop");
        }
        DestroyWindow(window);
    }
    CoUninitialize();return failures?1:0;
}
