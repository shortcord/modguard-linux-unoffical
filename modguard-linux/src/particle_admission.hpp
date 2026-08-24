#ifndef MODGUARD_PARTICLE_ADMISSION_HPP
#define MODGUARD_PARTICLE_ADMISSION_HPP

#include <array>
#include <cstdint>

namespace modguard {

// Synchronized per-layer admission sampler.
//
// Each frame resets a per-layer ordinal counter. Requests for a given smoke
// layer draw their decision from `(frame, layer, ordinal)`, so matching
// generators on different pipes receive the identical decision while
// consecutive requests in a frame stay independent. A layer index outside
// [0, 2] is clamped to the nearest valid layer.
class LayerSampler {
public:
    void frame_start();
    std::uint32_t sample(int layer);

private:
    std::array<std::uint64_t, 3> sequence_{0, 0, 0};
    std::uint64_t frame_{0};
};

} // namespace modguard

#endif
