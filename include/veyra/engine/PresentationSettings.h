#pragma once
#include <algorithm>
#include <cstdint>
namespace veyra::engine {
enum class PacingMode : unsigned { LowQueue, Even, Reflex };
enum class DisplaySync : unsigned { Tearing, Vsync, Automatic };
struct PresentationSettings {
    bool enabled=false;
    PacingMode mode=PacingMode::LowQueue;
    DisplaySync display=DisplaySync::Tearing;
    bool operator==(const PresentationSettings&) const = default;
    bool valid()const{return unsigned(mode)<=2&&unsigned(display)<=2;}
};
// Media time remains the authority. Spacing adds at most one interval from
// the current decision; stale generated frames are discarded by the caller.
class PresentationCadence {
    int64_t last_=0;
public:
    void reset(){last_=0;}
    int64_t due(int64_t mediaDeadline,int64_t outputInterval,unsigned catchUpPercent=10)const{
        if(!last_)return mediaDeadline;
        // Bounded catch-up absorbs wakeup/Present jitter without moving the
        // media grid forward every frame. File MFG allows 20% recovery;
        // other callers retain 10%. No caller can remove the spacing bound.
        const auto recovery=(outputInterval/100)*std::min(catchUpPercent,20u)
            +(outputInterval%100)*std::min(catchUpPercent,20u)/100;
        return std::max(mediaDeadline,last_+outputInterval-recovery);
    }
    void submitted(int64_t now){last_=now;}
};
}
