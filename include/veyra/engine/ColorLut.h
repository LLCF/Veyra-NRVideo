#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace veyra::engine {
// 3D look-up table in the form the ingest shader samples: size^3 RGB triples in
// 0..1, red index varying fastest (the .cube convention). 1D .cube files are
// expanded to a 3D grid on import so the shader keeps a single code path.
struct ColorLutData {
    int size=0;
    std::string title;
    std::vector<float> rgb;
    // Raw .cube domain as declared by DOMAIN_MIN / DOMAIN_MAX; normalizeCubeDomain
    // maps the grid onto 0..1 and resets these to the default.
    double domainMin[3]={0,0,0},domainMax[3]={1,1,1};
    bool valid()const{return size>=2&&size<=64&&rgb.size()==std::size_t(size)*std::size_t(size)*std::size_t(size)*3;}
};
// Adobe Cube LUT Specification 1.0 parser. Accepts TITLE / LUT_1D_SIZE /
// LUT_3D_SIZE / DOMAIN_MIN / DOMAIN_MAX / comments and the numeric rows; rejects
// anything else (bad counts, NaN, non-monotonic domain) with size == 0.
ColorLutData parseCube(std::string_view text);
// Re-samples a parsed LUT so its domain starts at 0 and spans 1, which is what
// the shader assumes; identity when the domain already is 0..1.
ColorLutData normalizeCubeDomain(const ColorLutData& lut);

// User-facing store: every imported .cube lives as a file under
// <applicationRoot>/runtime_local/luts with a manifest line (name, size, bytes,
// SHA-256, origin) so the same folder can be resolved by the export worker.
class ColorLutStore {
public:
    explicit ColorLutStore(std::filesystem::path dataDirectory);
    const std::filesystem::path& folder()const{return folder_;}
    std::vector<std::wstring> list()const;
    bool validName(std::wstring_view name)const;
    bool importFile(const std::filesystem::path& source,std::wstring& nameOut,std::string& error)const;
    bool resolve(const std::wstring& name,ColorLutData& out)const;
private:
    std::filesystem::path folder_;
};
} // namespace veyra::engine
