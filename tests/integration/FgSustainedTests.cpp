#include "veyra/engine/EngineController.h"
#include "veyra/engine/FgCompatibilityProbe.h"
#include "veyra/Log.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

int wmain(int argc,wchar_t** argv){
    using namespace veyra::engine;
    using Clock=std::chrono::steady_clock;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr,executable,32768);
    setFgCompatibilityProbeExecutable(executable);
    if(argc==3&&std::wstring_view(argv[1])==L"--fg-compat-probe")
        return runFgCompatibilityProbe(reinterpret_cast<HANDLE>(_wcstoui64(argv[2],nullptr,10)));
    if(argc!=7&&argc!=8)return 2; // source, log directory, multiplier, admission, seconds, effects, optional minimum target ratio
    const unsigned multiplier=_wtoi(argv[3]);
    const int seconds=_wtoi(argv[5]);
    const double minimumRatio=argc==8?_wtof(argv[7]):0;
    if((multiplier!=4&&multiplier!=6)||seconds<30||seconds>240)return 2;
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    std::filesystem::create_directories(argv[2]);
    veyra::Logger::instance().openFile((std::filesystem::path(argv[2])/L"engine.log").wstring());
    veyra::Logger::instance().setConsoleEnabled(false);
    RECT rect{0,0,2191,1187};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowExW(0,L"STATIC",L"DLSS sustained regression",WS_OVERLAPPEDWINDOW,
        0,0,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window){CoUninitialize();return 2;}
    ShowWindow(window,SW_SHOWNOACTIVATE);
    bool ok=true;
    {
        EngineController engine;engine.setVolume(0,true);
        EnhancementSettings settings;settings.multiplier=multiplier;
        const std::wstring_view effects=argv[6];
        settings.nr=effects==L"on"||effects==L"nr";
        settings.sr=effects==L"on"||effects==L"sr";
        auto options=PlayerOptions::from(settings);
        const std::wstring source=argv[1];
        options.captureReplayForTest=!source.starts_with(L"capture:")&&!source.starts_with(L"capture2:");
        options.captureReplayDisableFgAdmissionForTest=std::wstring_view(argv[4])==L"off";
        engine.open(window,source,options);
        const auto opened=Clock::now();auto started=Clock::time_point{};
        int reported=-1;bool stalled=false,cleared=false;
        uint64_t beforeRecovery=0;double recoveredRate=0;unsigned recoveredSamples=0;
        while(Clock::now()-opened<std::chrono::seconds(seconds+35)){
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            const auto s=engine.snapshot();
            if(s.failed){std::wcerr<<s.status<<L" / "<<s.backendWarning<<std::endl;ok=false;break;}
            if(started==Clock::time_point{}&&s.frames>=10)started=Clock::now();
            if(started!=Clock::time_point{}){
                const auto elapsed=int(std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-started).count());
                if(elapsed>=seconds)break;
                if(elapsed>=15&&!stalled){SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",L"55");stalled=true;}
                if(elapsed>=17&&!cleared){SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);cleared=true;beforeRecovery=s.generated;}
                if(elapsed!=reported){
                    reported=elapsed;const auto& f=s.metrics.flow;const auto& c=f.counters;
                    std::cout<<"t="<<elapsed<<" multiplier="<<s.applied.multiplier<<" frames="<<s.frames<<" generated="<<s.generated
                        <<" sourceFps="<<f.sourceCompletedFps<<" submitFps="<<f.presentSubmitFps<<" validFps="<<f.validGeneratedFps
                        <<" limited="<<s.fgBudgetLimited<<" skipped="<<c.fgSkippedBeforeEval<<" expired="<<c.generatedExpiredAfterEval
                        <<" presentMs="<<s.presentCpuP95Ms<<" slotMs="<<f.slotWaitPerFrameMs.value_or(-1);
                    for(const auto stage:{veyra::diagnostics::GpuStage::Nr,veyra::diagnostics::GpuStage::Sr,veyra::diagnostics::GpuStage::Flow,veyra::diagnostics::GpuStage::FgBatch})
                        std::cout<<" gpu"<<int(stage)<<"="<<f.gpuTiming[size_t(stage)].p95.value_or(-1);
                    std::cout<<std::endl;
                    if(elapsed>=22){recoveredRate+=f.presentSubmitFps;++recoveredSamples;}
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);
        const auto s=engine.snapshot();
        ok=ok&&cleared&&s.fgActive&&s.applied.multiplier==multiplier&&s.generated>beforeRecovery+100;
        const double mean=recoveredSamples?recoveredRate/recoveredSamples:0;
        const bool throughput=minimumRatio<=0||(recoveredSamples&&mean>=s.nominalSourceFps*multiplier*minimumRatio);
        std::cout<<"SUSTAINED lifecycle="<<ok<<" recoveredMeanSubmitFps="<<mean
            <<" minimumTargetRatio="<<minimumRatio<<" throughput="<<throughput
            <<" nominalTarget="<<s.nominalSourceFps*multiplier<<" frames="<<s.frames<<" generated="<<s.generated<<std::endl;
        ok=ok&&throughput;
        engine.stop();
        const auto stopping=Clock::now();
        while(!engine.idle()&&Clock::now()-stopping<std::chrono::seconds(15))std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ok=ok&&engine.idle();
    }
    DestroyWindow(window);CoUninitialize();
    std::cout<<"Submission rates are not scanout rates. Lifecycle recovery does not assert target throughput or image quality.\n";
    return ok?0:1;
}
