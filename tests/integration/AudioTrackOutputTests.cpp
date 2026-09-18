#include "veyra/engine/ExportJobManager.h"
#include "veyra/engine/FgCompatibilityProbe.h"
#include "veyra/sink/WasapiAudioSink.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

using namespace veyra::engine;
using Clock=std::chrono::steady_clock;

// Fixture: video plus 440 Hz Spanish and 880 Hz English mono tracks.
bool verifyPcm(veyra::sink::AudioPipeline& audio,int stream,double frequency){
    if(!audio.selectTrack(stream))return false;
    audio.setPaused(true);audio.startThread(nullptr,false,1000);
    const auto deadline=Clock::now()+std::chrono::seconds(10);
    while(audio.bufferedMs()<500&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    audio.stopThread();
    const auto channels=audio.pcmFormat().channels;
    if(!channels||audio.bufferedMs()<500)return false;
    std::vector<float> pcm(24000*channels);double pts=-1;
    const size_t frames=audio.pull(pcm.data(),24000,&pts);
    unsigned crossings=0;double energy=0;
    for(size_t i=0;i<frames;++i){const float value=pcm[i*channels];energy+=value*value;if(i&&value>=0&&pcm[(i-1)*channels]<0)++crossings;}
    const double measured=double(crossings)*48000/frames;
    const bool ok=frames==24000&&std::abs(pts-1000)<30&&energy/frames>.001&&std::abs(measured-frequency)<5;
    std::cout<<(ok?"PASS ":"FAIL ")<<"stream="<<stream<<" Hz="<<measured<<" ptsMs="<<pts<<" frames="<<frames<<std::endl;
    return ok;
}
bool verifyExport(const std::filesystem::path& output){
    veyra::sink::AudioPipeline audio;
    return audio.open(output.wstring())&&audio.tracks().size()==1&&audio.tracks()[0].language=="eng"&&verifyPcm(audio,audio.tracks()[0].streamIndex,880);
}
int wmain(int argc,wchar_t** argv){
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);setFgCompatibilityProbeExecutable(executable);
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(argc==3&&std::wstring_view(argv[1])==L"--export-worker")return runExportWorker(reinterpret_cast<HANDLE>(_wcstoui64(argv[2],nullptr,10)));
    if(argc!=3)return 2;
    std::filesystem::create_directories(argv[2]);
    veyra::Logger::instance().openFile((std::filesystem::path(argv[2])/L"audio-tracks.log").wstring());
    veyra::Logger::instance().setConsoleEnabled(false);
    veyra::sink::AudioPipeline audio;
    if(!audio.open(argv[1])||audio.tracks().size()!=2)return 1;
    const auto tracks=audio.tracks();
    if(tracks[0].language!="spa"||tracks[1].language!="eng")return 1;
    for(unsigned index:{0,1,0,1})if(!verifyPcm(audio,tracks[index].streamIndex,index?880:440))return 1;
    if(audio.selectTrack(99999)||audio.selectedTrack()!=tracks[1].streamIndex)return 1;
    EnhancementSettings settings;settings.nr=settings.sr=false;settings.multiplier=1;
    const auto workerOutput=std::filesystem::path(argv[2])/L"english-worker.mkv";
    {
        ExportJobManager manager;
        if(!manager.start(argv[1],workerOutput.wstring(),settings,false,0,tracks[1].streamIndex))return 1;
        const auto deadline=Clock::now()+std::chrono::seconds(90);
        ExportJobSnapshot state;
        do{state=manager.poll();if(!state.active())break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}while(Clock::now()<deadline);
        std::wcout<<L"worker state="<<int(state.state)<<L" encoded="<<state.encoded<<L" log="<<state.workerLog<<std::endl;
        if(state.state!=ExportState::Succeeded||!verifyExport(workerOutput))return 1;
    }
    const auto legacyOutput=std::filesystem::path(argv[2])/L"english-controller.mkv";
    {
        EngineController engine;auto options=PlayerOptions::from(settings);options.audioStreamIndex=tracks[1].streamIndex;
        engine.startExport(argv[1],legacyOutput.wstring(),options,false);
        const auto deadline=Clock::now()+std::chrono::seconds(90);
        while(!engine.idle()&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(!engine.idle()||engine.snapshot().failed||!verifyExport(legacyOutput))return 1;
    }
    CoUninitialize();return 0;
}
