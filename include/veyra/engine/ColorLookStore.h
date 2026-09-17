#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "veyra/engine/ColorSettings.h"
namespace veyra::engine {
// A named colour look: the full ColorSettings block plus its referenced LUT.
// Looks live in <data>/color-looks.v1 and can be exported/imported as a single
// .vpcolor file for sharing.
struct ColorLook {
    std::wstring name;
    ColorSettings color;
};
class ColorLookStore {
public:
    explicit ColorLookStore(std::filesystem::path dataDirectory);
    bool load();
    bool save();
    const std::vector<ColorLook>& entries()const{return entries_;}
    const std::wstring& error()const{return error_;}
    bool corrupt()const{return corrupt_;}
    bool put(std::wstring name,const ColorSettings& colour,bool replace);
    bool erase(size_t index);
    bool exportFile(size_t index,const std::filesystem::path& target);
    bool importFile(const std::filesystem::path& source,std::wstring& nameOut);
private:
    std::filesystem::path path_;
    std::vector<ColorLook> entries_;
    bool corrupt_=false;
    std::wstring error_;
};
} // namespace veyra::engine
