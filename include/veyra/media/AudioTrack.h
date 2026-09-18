#pragma once
#include <string>

namespace veyra::media {
struct AudioTrack {
    int streamIndex = -1;
    std::string language, title, codec;
    unsigned channels = 0;
};
}
