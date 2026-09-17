// Named colour looks (T5-b): storage, atomic save, corrupt preservation and the
// .vpcolor export/import round trip. Pure CPU.
#include "veyra/engine/ColorLookStore.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
namespace {
using namespace veyra::engine;
int failures=0;
void check(bool ok,const std::string& label){
    std::printf("%s %s\n",ok?"PASS":"FAIL",label.c_str());
    if(!ok)++failures;
}
ColorSettings sample(){
    ColorSettings colour;
    colour.enabled=true;
    colour.exposure=0.75f;colour.contrast=-20.0f;colour.vibrance=15.0f;
    colour.mixerSaturation[3]=40.0f;colour.grading[1]={210.0f,30.0f,-10.0f};
    colour.curves[0].count=3;colour.curves[0].points[1]={0.3f,0.4f};colour.curves[0].points[2]={1,1};
    colour.setLutName(L"probe.cube");colour.lutStrength=80.0f;colour.lutInputSpace=1;
    return colour;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2){std::printf("usage: color_look_tests <tempdir>\n");return 2;}
    const std::filesystem::path directory(argv[1]);
    std::error_code ec;std::filesystem::create_directories(directory,ec);
    {
        ColorLookStore store(directory);
        check(store.load(),"a missing store loads empty");
        const auto colour=sample();
        check(store.put(L"夜景",colour,false),"saving a named look works");
        check(store.put(L"夜景",colour,false)==false,"duplicate names are refused without replace");
        ColorLookStore reloaded(directory);
        check(reloaded.load()&&reloaded.entries().size()==1,"the look survives a reload");
        check(reloaded.entries()[0].color==colour,"every colour field round trips");
        check(reloaded.entries()[0].color.lutNameString()==L"probe.cube","the referenced LUT name round trips");
        check(store.exportFile(0,directory/"夜景.vpcolor"),"export writes a .vpcolor file");
        ColorLookStore other(directory/"other");
        std::wstring imported;
        check(other.load()&&other.importFile(directory/"夜景.vpcolor",imported)&&imported==L"夜景",
            "importing the exported file restores the name");
        check(other.entries().size()==1&&other.entries()[0].color==colour,"imported parameters match");
        {
            std::ofstream broken(directory/"broken.vpcolor",std::ios::binary);
            broken<<"VEYRA_COLOR_LOOK 1\n\"x\" 1 2 3\n";
        }
        std::wstring name;check(!other.importFile(directory/"broken.vpcolor",name)&&!other.error().empty(),"a truncated import is rejected");
        check(store.put(L"日景",sample(),false)&&store.entries().size()==2,"a second look is stored");
        check(store.erase(0)&&store.entries().size()==1&&store.entries()[0].name==L"日景","delete removes only the selected look");
        {
            std::ofstream corrupt(directory/"color-looks.v1",std::ios::binary);
            corrupt<<"NOT_A_STORE 9\n";
        }
        ColorLookStore damaged(directory);
        check(!damaged.load()&&damaged.corrupt(),"a corrupt store reports corruption");
        check(!damaged.save(),"a corrupt store refuses to overwrite the original");
        std::ifstream check_(directory/"color-looks.v1",std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(check_)),std::istreambuf_iterator<char>());
        check(data=="NOT_A_STORE 9\n","the corrupt original is preserved byte for byte");
    }
    if(failures){std::printf("FAIL: colour looks (%d checks)\n",failures);return 1;}
    std::printf("PASS: named colour looks, atomic save, corrupt preservation, .vpcolor round trip\n");
    return 0;
}
