#pragma once
#include "veyra/Log.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>

namespace veyra::diagnostics {
// Wall time includes descheduling. No allocation or logging on normal frames.
class CpuStallTrace {
    using Clock = std::chrono::steady_clock;
public:
    explicit CpuStallTrace(const char* component, uint64_t frame = 0)
        : component_(component), frame_(frame) {}
    void mark(const char* stage) {
        const auto now = Clock::now();
        if (count_ < samples_.size()) samples_[count_++] = {stage, ms(now - last_)};
        last_ = now;
    }
    ~CpuStallTrace() {
        const auto total = ms(Clock::now() - start_);
        if (total < 80.0) return;
        mark("tail");
        std::string detail;
        for (size_t i = 0; i < count_; ++i)
            detail += std::format(" {}Ms={:.3f}", samples_[i].name, samples_[i].ms);
        log::info(component_, std::format("frame={} totalMs={:.3f}{}", frame_, total, detail));
    }
private:
    static double ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
    struct Sample { const char* name; double ms; };
    const char* component_;
    uint64_t frame_;
    Clock::time_point start_ = Clock::now(), last_ = start_;
    std::array<Sample, 12> samples_{};
    size_t count_ = 0;
};
}
