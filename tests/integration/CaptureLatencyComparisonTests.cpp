#include "veyra/engine/EngineController.h"
#include "veyra/Log.h"
#include "veyra/source/CaptureCardSource.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

int wmain(int argc, wchar_t** argv) {
    using namespace veyra::engine;
    using Clock = std::chrono::steady_clock;
    if (argc != 3) return 2;
    const int seconds = _wtoi(argv[2]);
    if (seconds < 3 || seconds > 120) return 2;
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS", L"0");
    veyra::Logger::instance().openFile((dir / L"engine.log").wstring());
    veyra::Logger::instance().setConsoleEnabled(false);
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    const auto devices = veyra::source::CaptureCardSource::deviceDetails();
    const auto device = std::find_if(devices.begin(), devices.end(), [](const auto& d) { return d.name == L"VC-007PRO"; });
    if (device == devices.end()) { std::cout << "FAIL VC-007PRO missing" << std::endl; return 3; }
    const auto formats = veyra::source::CaptureCardSource::formatsByPath(device->path);
    const auto format = std::find_if(formats.begin(), formats.end(), [](const auto& f) {
        return f.width == 3840 && f.height == 2160 && std::abs(f.fps - 30) < .01 && f.label.find(L"NV12") != std::wstring::npos;
    });
    if (format == formats.end()) { std::cout << "FAIL 4K30 NV12 missing" << std::endl; return 3; }
    const auto audios = veyra::source::CaptureCardSource::deviceDetails(true);
    const auto audio = std::find_if(audios.begin(), audios.end(), [](const auto& a) {
        return a.wasapi && a.name.find(L"HDMI (VC-007PRO)") != std::wstring::npos;
    });
    if (audio == audios.end()) { std::cout << "FAIL identical WASAPI input unavailable" << std::endl; return 3; }
    const auto path = veyra::source::CaptureCardSource::makeCapturePath(unsigned(device - devices.begin()), *device,
        format->index, veyra::source::kCaptureAudioWasapi, &*audio);
    HWND window = CreateWindowExW(0, L"STATIC", L"Veyra historical capture comparison", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        40, 40, 1280, 760, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 2;
    MONITORINFOEXW monitor{}; monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    DEVMODEW dm{}; dm.dmSize = sizeof(dm); EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &dm);
    std::cout << "capture=VC-007PRO input=3840x2160 fps=" << format->fps << " NV12 formatIndex=" << format->index
        << " audio=WASAPI-muted NR=realtime1080 FG=DLSS4X SR=off sync=default-off window=1280x760 display="
        << dm.dmPelsWidth << 'x' << dm.dmPelsHeight << '@' << dm.dmDisplayFrequency << std::endl;
    int failures = 0;
    auto check = [&](bool ok, const char* label) { std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl; failures += !ok; };
    auto pump = [] { MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } };
    {
        EngineController engine;
        engine.setVolume(0, true);
        EnhancementSettings settings; settings.nr = true; settings.sr = false; settings.multiplier = 4;
        engine.open(window, path, PlayerOptions::from(settings));
        auto wait = [&](auto predicate, int ms) {
            const auto until = Clock::now() + std::chrono::milliseconds(ms);
            while (Clock::now() < until) {
                pump(); const auto s = engine.snapshot();
                if (s.failed) return false;
                if (predicate(s)) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return false;
        };
        const bool started = wait([](const auto& s) { return s.frames > 10 && s.nrActive && s.fgActive; }, 25000);
        check(started, "first frames with NR and DLSS");
        const auto warm = Clock::now();
        const bool warmed = started && wait([&](const auto& s) {
            return Clock::now() - warm >= std::chrono::seconds(10) && s.nrActive && s.fgActive && s.applied.multiplier == 4;
        }, 20000);
        check(warmed, "ten second warmup");
        if (warmed) {
            std::ofstream csv(dir / L"snapshots.csv");
            csv << std::setprecision(10);
            csv << "wall,frames,received,dropped,generated,nr,fg,multiplier,limited,callbackFps,submitFps,ageMs,ageP95,readAgeMs,realPresented,generatedPresented,fgSkipped,fgExpired,slotWaits,readyMeanMs,deadlineMeanMs,presentMeanMs,colorMeanMs,flowMeanMs,nrMeanMs,residualMeanMs,fgMeanMs,graphMeanMs\n";
            const auto first = engine.snapshot();
            auto last = first;
            const auto start = Clock::now();
            bool active = true;
            while (Clock::now() - start < std::chrono::seconds(seconds) && !last.failed) {
                pump(); last = engine.snapshot();
                const auto& flow = last.metrics.flow; const auto& c = flow.counters;
                active = active && last.nrActive && last.fgActive && last.applied.multiplier == 4 && !last.srActive;
                auto cpu = [&](veyra::diagnostics::CpuStage stage) { return flow.cpuTiming[size_t(stage)].mean.value_or(-1); };
                auto gpu = [&](veyra::diagnostics::GpuStage stage) { return flow.gpuTiming[size_t(stage)].mean.value_or(-1); };
                using C = veyra::diagnostics::CpuStage; using G = veyra::diagnostics::GpuStage;
                csv << std::chrono::duration<double>(Clock::now() - start).count() << ',' << last.frames << ',' << last.captureReceived
                    << ',' << last.captureDropped << ',' << last.generated << ',' << last.nrActive << ',' << last.fgActive
                    << ',' << last.applied.multiplier << ',' << last.fgBudgetLimited << ',' << last.captureFps << ',' << last.submissionFps.value_or(0)
                    << ',' << last.captureAgeMs << ',' << last.captureAgeP95Ms << ',' << last.captureReadAgeMs << ',' << c.realPresented
                    << ',' << c.generatedPresented << ',' << c.fgSkippedBeforeEval << ',' << c.generatedExpiredAfterEval << ',' << flow.slotReuseWaitCount
                    << ',' << cpu(C::ReadyWait) << ',' << cpu(C::DeadlineWait) << ',' << cpu(C::Present)
                    << ',' << gpu(G::Color) << ',' << gpu(G::Flow) << ',' << gpu(G::Nr) << ',' << gpu(G::Residual)
                    << ',' << gpu(G::FgBatch) << ',' << flow.enhancementProcessing.mean.value_or(-1) << '\n';
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            std::cout << "elapsed=" << elapsed << " receivedDelta=" << last.captureReceived - first.captureReceived
                << " droppedDelta=" << last.captureDropped - first.captureDropped << " nrDelta=" << last.nrEvaluated - first.nrEvaluated
                << " generatedDelta=" << last.generated - first.generated << " realPresentedDelta="
                << last.metrics.flow.counters.realPresented - first.metrics.flow.counters.realPresented << std::endl;
            check(!last.failed && elapsed >= seconds && last.frames > first.frames + 10, "full measurement");
            check(active && last.nrEvaluated > first.nrEvaluated && last.generated > first.generated, "NR and DLSS4X continuously active");
        }
        engine.stop(); check(wait([&](const auto&) { return engine.idle(); }, 15000), "clean stop");
    }
    DestroyWindow(window); CoUninitialize();
    return failures ? 1 : 0;
}
