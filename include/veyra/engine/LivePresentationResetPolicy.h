#pragma once

#include "veyra/pipeline/FramePacket.h"
#include <cstdint>

namespace veyra::engine {

// A missing future input breaks processing history, not an earlier A/B pair
// whose generated textures are still leased. Hard boundaries invalidate both.
constexpr bool presentationGenerationResetRequired(bool explicitReset, pipeline::FrameFlags flags) {
    return explicitReset || pipeline::breaksHistory(
        flags & ~static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Drop));
}

constexpr bool presentationDrainRequired(bool explicitReset, pipeline::FrameFlags flags) {
    return explicitReset
        || pipeline::hasFrameFlag(flags, pipeline::FrameFlagBits::Resize)
        || pipeline::hasFrameFlag(flags, pipeline::FrameFlagBits::DeviceLost);
}

constexpr bool generatedPresentationCurrent(uint64_t jobGeneration, uint64_t currentGeneration) {
    return jobGeneration == currentGeneration;
}

} // namespace veyra::engine
