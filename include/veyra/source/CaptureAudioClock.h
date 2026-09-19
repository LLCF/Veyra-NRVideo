#pragma once
#include <optional>

namespace veyra::source {
// Untimed driver samples still contain real audio. Continue by decoded sample
// duration; arrival time is only an anchor after open or a discontinuity.
class CaptureAudioClock {
    double next_=0;
    bool anchored_=false;
public:
    double observe(std::optional<double> timestamp,double arrival,double duration,bool discontinuity){
        const double start=timestamp?*timestamp:(!anchored_||discontinuity?arrival:next_);
        next_=start+duration;anchored_=true;return start;
    }
};
}
