#ifndef MODGUARD_PARTICLE_ADMISSION_HPP
#define MODGUARD_PARTICLE_ADMISSION_HPP

#include "particle_signature.hpp"

#include <array>
#include <cstdint>

namespace modguard {

// Synchronized per-identity admission sampler.
//
// Each frame resets a per-identity ordinal counter. Requests for a given smoke
// identity draw their decision from `(frame, identity, ordinal)`, so matching
// generators on different pipes receive the identical decision while
// consecutive requests in a frame stay independent. An identity index outside
// [0, particle_identity_max] is clamped to the nearest valid stream.
class LayerSampler {
public:
    void frame_start();
    std::uint32_t sample(int identity);

private:
    std::array<std::uint64_t, particle_identity_stream_count> sequence_{};
    std::uint64_t frame_{0};
};

} // namespace modguard

#endif
