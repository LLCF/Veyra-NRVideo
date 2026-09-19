#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

namespace veyra::engine {
// Real-time preview only. Keep headroom for presentation and scheduling;
// use sustained measured work, never a single late CPU observation.
class PreviewFgCapacity {
    struct Cost { double base, perGenerated; };
    std::deque<Cost> costs_;
    unsigned effective_=0, candidate_=0, confirmations_=0;
public:
    void reset(){costs_.clear();effective_=candidate_=confirmations_=0;}
    void observe(double whole,double fg,unsigned calls){
        if(!calls||!std::isfinite(whole)||!std::isfinite(fg)||fg<=0||whole<fg)return;
        costs_.push_back({whole-fg,fg/calls});
        if(costs_.size()>60)costs_.pop_front();
    }
    unsigned select(unsigned requested,double intervalMs){
        requested=std::clamp(requested,2u,6u);
        if(!effective_)effective_=requested;
        effective_=std::min(effective_,requested);
        if(costs_.size()<30||!std::isfinite(intervalMs)||intervalMs<=0)return effective_;
        double base=0,per=0;
        for(auto c:costs_){base+=c.base;per+=c.perGenerated;}
        base/=costs_.size();per/=costs_.size();
        unsigned target=2;
        for(unsigned n=2;n<=requested;++n)
            if(base+per*(n-1)<=intervalMs*(n>effective_?.82:.90))target=n;
        if(target==effective_){candidate_=target;confirmations_=0;return effective_;}
        if(candidate_!=target){candidate_=target;confirmations_=0;}
        if(++confirmations_>=(target<effective_?15u:120u)){
            effective_=target;confirmations_=0;
        }
        return effective_;
    }
};
}
