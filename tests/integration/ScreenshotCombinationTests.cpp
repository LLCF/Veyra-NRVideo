#include "veyra/engine/EngineController.h"
#include "veyra/Log.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/sink/ImageExportSink.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <iostream>
using namespace veyra::engine;
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    const std::filesystem::path dir=argv[2];std::filesystem::create_directories(dir);
    veyra::Logger::instance().openFile((dir/L"engine.log").wstring());
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    HWND window=CreateWindowExW(0,L"STATIC",L"Screenshot combination regression",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,80,960,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    int failures=0;
    for(bool nr:{false,true})for(bool hdr:{false,true}){
        EngineController engine;engine.setVolume(0,true);EnhancementSettings settings;settings.nr=nr;settings.videoHdr.enabled=hdr;
        engine.open(window,argv[1],PlayerOptions::from(settings));
        auto wait=[&](auto predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
            while(std::chrono::steady_clock::now()<end){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}const auto s=engine.snapshot();if(s.failed)return false;if(predicate(s))return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}return false;};
        bool ok=wait([&](const auto& s){return s.frames>=12&&(!nr||s.nrActive);});
        for(bool paused:{false,true}){
            if(paused){engine.pause(true);ok=wait([](const auto& s){return s.transport==TransportState::Paused;})&&ok;}
            const auto base=dir/(std::to_wstring(nr)+L"-"+std::to_wstring(hdr)+L"-"+std::to_wstring(paused)+L".png");
            auto actual=base;const bool hdrOutput=hdr&&veyra::gfx::PresentSink::hdrDisplayActive(window);if(hdrOutput)actual.replace_extension(L".jxr");
            engine.saveFrame(base.wstring());
            const bool saved=wait([&](const auto&){return std::filesystem::exists(actual)&&std::filesystem::file_size(actual)>100;});
            veyra::sink::RgbaImage decoded;const bool reopened=saved&&veyra::sink::loadImage(actual.wstring(),decoded)&&decoded.width>0&&decoded.height>0;
            std::cout<<"SCREENSHOT nr="<<nr<<" hdrRequested="<<hdr<<" hdrOutput="<<hdrOutput<<" paused="<<paused<<" saved="<<saved<<" reopened="<<reopened<<std::endl;ok=reopened&&ok;
        }
        failures+=!ok;engine.stop();
    }
    DestroyWindow(window);CoUninitialize();return failures?1:0;
}
