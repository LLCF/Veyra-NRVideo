// Adobe .cube parsing, validation and the runtime LUT store (T5). Pure CPU so
// the importer can be verified without a GPU or a capture device.
#include "veyra/engine/ColorLut.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
namespace {
using namespace veyra::engine;
int failures=0;
void check(bool ok,const std::string& label){
    std::printf("%s %s\n",ok?"PASS":"FAIL",label.c_str());std::fflush(stdout);
    if(!ok)++failures;
}
std::string cube3d(int size,bool halve=true){
    std::string text="TITLE \"test\"\n# comment\nLUT_3D_SIZE "+std::to_string(size)+"\n";
    for(int b=0;b<size;++b)for(int g=0;g<size;++g)for(int r=0;r<size;++r){
        const float scale=halve?0.5f:1.0f;
        text+=std::to_string(float(r)/(size-1)*scale)+" "+std::to_string(float(g)/(size-1)*scale)+" "+std::to_string(float(b)/(size-1)*scale)+"\n";
    }
    return text;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2){std::printf("usage: color_lut_tests <tempdir>\n");return 2;}
    const std::filesystem::path directory(argv[1]);
    std::error_code ec;std::filesystem::create_directories(directory,ec);

    // 1. A well-formed 2x2x2 LUT parses, keeps red-fastest ordering and halves.
    {
        const auto lut=parseCube(cube3d(2));
        check(lut.valid()&&lut.size==2&&lut.rgb.size()==24,"parses a 2x2x2 cube");
        check(lut.rgb[0]==0.0f&&lut.rgb[3]==0.5f&&lut.rgb[4]==0.0f&&lut.rgb[23]==0.5f,"red index varies fastest and values are kept");
    }
    // 2. Size 33 (the common export size) round-trips its corners.
    {
        const auto lut=parseCube(cube3d(33,false));
        const int last=32;
        const std::size_t corner=(std::size_t(last)*33*33+std::size_t(last)*33+std::size_t(last))*3;
        check(lut.valid()&&lut.size==33,"parses a 33^3 cube");
        check(std::abs(lut.rgb[corner]-1.0f)<1e-6f&&std::abs(lut.rgb[corner+1]-1.0f)<1e-6f,"33^3 keeps its top corner");
    }
    // 3. Malformed input is rejected instead of silently truncated.
    {
        check(!parseCube("LUT_3D_SIZE 2\n0 0 0\n0 0 1\n").valid(),"row count mismatch is rejected");
        check(!parseCube("LUT_3D_SIZE 0\n").valid(),"invalid size is rejected");
        check(!parseCube("LUT_1D_SIZE 2\n0 0 nan\n1 1 1\n").valid(),"non-finite values are rejected");
        check(!parseCube("LUT_3D_SIZE 2\n0 0 0 0\n").valid(),"extra columns are rejected");
        check(parseCube("LUT_3D_SIZE 2\n0 0 0\n0 0 1\n0 1 0\n0 1 1\n1 0 0\n1 0 1\n1 1 0\n1 1 1\n").valid(),"a full valid file parses");
    }
    // 4. A 1D LUT expands to a 3D grid with the same ramp.
    {
        const auto lut=parseCube("LUT_1D_SIZE 2\n0 0 0\n1 1 1\n");
        check(lut.valid()&&lut.size==32,"1D LUTs expand to a 32^3 grid");
        const std::size_t corner=(std::size_t(31)*32*32+std::size_t(31)*32+std::size_t(31))*3;
        check(std::abs(lut.rgb[corner]-1.0f)<1e-5f,"the expanded ramp reaches white");
    }
    // 5. DOMAIN_MIN/MAX is normalised onto 0..1.
    {
        std::string text="LUT_3D_SIZE 2\nDOMAIN_MIN 0.1 0.1 0.1\nDOMAIN_MAX 0.9 0.9 0.9\n";
        text+="0.1 0.1 0.1\n0.9 0.1 0.1\n0.1 0.9 0.1\n0.9 0.9 0.1\n0.1 0.1 0.9\n0.9 0.1 0.9\n0.1 0.9 0.9\n0.9 0.9 0.9\n";
        const auto raw=parseCube(text);
        const auto normalized=normalizeCubeDomain(raw);
        const std::size_t top=(std::size_t(1)*2*2+std::size_t(1)*2+std::size_t(1))*3;
        check(raw.valid()&&std::abs(raw.rgb[top]-0.9f)<1e-5f,"raw parse keeps the declared domain values");
        check(normalized.valid()&&std::abs(normalized.rgb[0]-0.1f)<1e-5f&&std::abs(normalized.rgb[top]-0.9f)<1e-5f,
            "grid spans the declared domain and is handed to the shader as-is");
    }
    // 6. The store imports, lists and resolves by name, and refuses junk.
    {
        const auto source=directory/"halve.cube";
        std::ofstream(source,std::ios::binary)<<cube3d(2);
        ColorLutStore store(directory);
        std::wstring name;std::string error;
        const bool imported=store.importFile(source,name,error);
        check(imported&&name==L"halve.cube","import copies the file and returns its name");
        auto names=store.list();
        check(names.size()==1&&names[0]==L"halve.cube","list reports the imported lut");
        ColorLutData resolved;
        check(store.resolve(name,resolved)&&resolved.size==2,"resolve returns the parsed grid");
        check(std::filesystem::exists(store.folder()/"manifest.v1"),"import writes a manifest line");
        const auto bad=directory/"bad.cube";
        std::ofstream(bad,std::ios::binary)<<"LUT_3D_SIZE 2\n0 0 0\n";
        std::wstring badName;std::string badError;
        check(!store.importFile(bad,badName,badError)&&!badError.empty(),"import rejects a malformed cube");
        check(!store.resolve(L"../escape.cube",resolved),"resolve refuses names with path separators");
    }
    if(failures){std::printf("FAIL: colour lut (%d checks)\n",failures);return 1;}
    std::printf("PASS: colour lut parsing, domain normalisation and store\n");
    return 0;
}
