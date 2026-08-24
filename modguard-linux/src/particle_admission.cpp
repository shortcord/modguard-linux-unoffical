#include "particle_admission.hpp"

namespace modguard {

namespace {

std::uint64_t mix(std::uint64_t value)
{
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

} // namespace

void LayerSampler::frame_start()
{
    ++frame_;
    sequence_.fill(0);
}

std::uint32_t LayerSampler::sample(int layer)
{
    const auto clamped = static_cast<std::size_t>(layer);
    const std::size_t index = clamped < sequence_.size() ? clamped : sequence_.size() - 1U;
    const std::uint64_t ordinal = sequence_[index]++;
    return static_cast<std::uint32_t>(
        mix((frame_ << 24U) | (static_cast<std::uint64_t>(index) << 8U) | ordinal) &
        0xffffffU);
}

} // namespace modguard
