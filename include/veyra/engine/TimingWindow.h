#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <vector>
namespace veyra::engine {
class TimingWindow {
public:
    using Clock=std::chrono::steady_clock;
private:
    struct Sample {double value;Clock::time_point time;};
    mutable std::deque<Sample> values_;
    Clock::duration maxAge_;
    mutable double cached_=0;
    mutable Clock::time_point refresh_{};
    void expire(Clock::time_point now)const{
        while(!values_.empty()&&now-values_.front().time>=maxAge_){values_.pop_front();refresh_={};}
    }
public:
    explicit TimingWindow(Clock::duration maxAge=Clock::duration::max()):maxAge_(maxAge){}
    void add(double v,Clock::time_point now=Clock::now()){if(!std::isfinite(v)||v<0)return;expire(now);values_.push_back({v,now});if(values_.size()>1200)values_.pop_front();}
    void clear(){values_.clear();cached_=0;refresh_={};}
    size_t size()const{return values_.size();}
    double p95(Clock::time_point now=Clock::now())const{
        expire(now);if(values_.empty())return 0;
        if(now<refresh_)return cached_;
        std::vector<double> sorted;sorted.reserve(values_.size());for(const auto& sample:values_)sorted.push_back(sample.value);const size_t i=(sorted.size()-1)*95/100;
        std::nth_element(sorted.begin(),sorted.begin()+i,sorted.end());cached_=sorted[i];refresh_=now+std::chrono::milliseconds(250);return cached_;
    }
};
}
