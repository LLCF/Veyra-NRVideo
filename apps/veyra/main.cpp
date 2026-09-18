#include <windows.h>
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/engine/FgCompatibilityProbe.h"
#include <shellapi.h>
int runVeyraApp(HINSTANCE,int);
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv&&argc==3&&std::wstring_view(argv[1])==L"--fg-compat-probe"){
        const auto mapping=reinterpret_cast<HANDLE>(_wcstoui64(argv[2],nullptr,10));LocalFree(argv);
        return veyra::engine::runFgCompatibilityProbe(mapping);
    }
    if(argv)LocalFree(argv);
    wchar_t executable[32768]{};
    if(GetModuleFileNameW(nullptr,executable,32768))veyra::engine::setFgCompatibilityProbeExecutable(executable);
    wchar_t logOverride[32768]{};
    const auto length=GetEnvironmentVariableW(L"VEYRA_LOG_FILE",logOverride,32768);
    auto logPath=length>0&&length<32768?std::filesystem::path(logOverride):veyra::runtime::logsDirectory()/L"veyra-app.log";
    // Preserve the failure preceding a restart. Concurrent GUI/test instances
    // must not truncate the active log or silently lose their own diagnostics.
    if(!veyra::Logger::instance().openFile(logPath.wstring(),true)){
        logPath=logPath.parent_path()/(L"veyra-app-"+std::to_wstring(GetCurrentProcessId())+L".log");
        (void)veyra::Logger::instance().openFile(logPath.wstring(),true);
    }
    veyra::log::info("app", "Veyra GUI session started");
    veyra::Logger::instance().flush();
    const int result=runVeyraApp(instance,show);
    veyra::log::info("app", "Veyra GUI session stopped");
    veyra::Logger::instance().flush();
    return result;
}
