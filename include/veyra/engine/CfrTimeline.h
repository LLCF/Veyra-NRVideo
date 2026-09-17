#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>

namespace veyra::engine {
// A rate is only a candidate. All timestamps must fit one common quantizer
// phase; a one-tick-per-frame time base is legal, a missing whole tick is not.
class CfrTimeline {
public:
    CfrTimeline(int num, int den, double quantum, double origin)
        : num_(num), den_(den), quantum_(quantum), origin_(origin), low_(-quantum/2), high_(quantum/2) {}
    bool valid() const {
        return num_ > 0 && den_ > 0 && fps() <= 1000 && fps() >= 1 &&
            std::isfinite(origin_) && std::isfinite(quantum_) && quantum_ > 0 && quantum_ <= 1.0 / fps() + 1e-12;
    }
    double fps() const { return double(num_) / den_; }
    double expected(uint64_t index) const { return origin_ + double(index) * den_ / num_; }
    // Tail-only recovery for containers whose last frame(s) sit off the common
    // quantizer phase: encoders round the final timestamp, and a truncated last
    // GOP loses one grid slot. Both used to abort a multi-hour export at the
    // very end. The encoder writes output frames on the grid from the frame
    // index, so a snapped tail can only change the last frame's display
    // duration; it cannot desync audio or shift any earlier frame.
    // `estimatedFrames` (container duration x rate) confines the exception to
    // the real tail, so a mid-stream gap can never take this path.
    bool tailAccepts(uint64_t index, double pts, uint64_t estimatedFrames) const {
        if(!valid() || !std::isfinite(pts)) return false;
        if(estimatedFrames == 0 || index + 3 < estimatedFrames) return false;
        const double interval = double(den_) / num_;
        return std::abs(pts - expected(index)) <= 1.5 * interval + quantum_;
    }
    // Recovery for containers that are missing whole samples (a recording that
    // dropped a frame but kept its timeline): a source frame landing exactly on
    // a later grid slot is a hole the exporter can fill by repeating the
    // previous frame, instead of aborting a mostly-fine multi-hour export.
    // Returns the number of missing slots, or 0 when this is not a clean
    // whole-slot gap (exactly on grid, ahead by 1..maxSlots slots).
    uint64_t missingSlots(uint64_t index, double pts, double maxSlots = 2.0) const {
        if(!valid() || !std::isfinite(pts) || quantum_ <= 0 || maxSlots < 1.0 || !std::isfinite(maxSlots)) return 0;
        const double interval = double(den_) / num_;
        const double slot = (pts - origin_) / interval;
        const double nearest = std::round(slot);
        // The frame must sit on the grid to the container's own tick accuracy;
        // anything else is real VFR and stays refused.
        if(std::abs(slot - nearest) * interval > quantum_ * 0.5 + 1e-9) return 0;
        const double missing = nearest - double(index);
        if(missing < 1.0 || missing > maxSlots + 1e-9) return 0;
        return uint64_t(std::llround(missing));
    }
    // Re-aligns the grid after a filled gap. Only call with the same (index,
    // pts) that missingSlots() accepted: the stream is grid-aligned again from
    // this frame on, so the phase window restarts here.
    void resync(uint64_t index, double pts) {
        if(!valid() || !std::isfinite(pts)) return;
        origin_ = pts - double(index) * double(den_) / num_;
        low_ = -quantum_ / 2;
        high_ = quantum_ / 2;
        previous_ = pts;
        havePrevious_ = true;
    }
    bool accepts(uint64_t index, double pts) {
        if(!valid() || !std::isfinite(pts) || (havePrevious_ && pts<=previous_))return false;
        const double error=pts-expected(index);
        const double ticksPerFrame=1.0/(fps()*quantum_);
        if(std::abs(ticksPerFrame-std::round(ticksPerFrame))<1e-9 && std::abs(error)>1e-9)return false;
        const double lo=std::max(low_,error-quantum_/2),hi=std::min(high_,error+quantum_/2);
        if(lo>hi+1e-9)return false;
        low_=lo;high_=hi;previous_=pts;havePrevious_=true;return true;
    }
    static std::pair<int,int> select(int nominalNum,int nominalDen,double quantum,std::span<const double> samples) {
        if(samples.empty())return {0,0};
        // Whole-slot holes (a dropped sample in the middle of the scan window)
        // are tolerated the same way the export loop fills them; irregular
        // timing still disqualifies the rate.
        auto fits=[&](int n,int d){
            CfrTimeline t(n,d,quantum,samples.front());uint64_t index=0;
            for(size_t i=0;i<samples.size();++i){
                if(t.accepts(index,samples[i])){++index;continue;}
                if(t.missingSlots(index,samples[i])==0)return false;
                t.resync(index,samples[i]);
                ++index;
            }
            return t.valid();
        };
        // Prefer a declared standard rate when consistent. Otherwise choose a
        // simple, timestamp-consistent standard candidate, then the declaration.
        // Quantized short clips cannot prove a unique exact original rate.
        const std::pair<int,int> standard[]={{24,1},{25,1},{30,1},{48,1},{50,1},{60,1},{100,1},{120,1},{24000,1001},{30000,1001},{60000,1001},{120000,1001}};
        for(auto [n,d]:standard)if(int64_t(n)*nominalDen==int64_t(nominalNum)*d && fits(n,d))return {n,d};
        for(auto [n,d]:standard)if(fits(n,d))return {n,d};
        if(fits(nominalNum,nominalDen))return {nominalNum,nominalDen};
        return {0,0};
    }
private:
    int num_, den_;
    double quantum_, origin_, low_, high_,previous_=0;
    bool havePrevious_=false;
};
}
