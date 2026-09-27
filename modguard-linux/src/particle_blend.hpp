#ifndef MODGUARD_PARTICLE_BLEND_HPP
#define MODGUARD_PARTICLE_BLEND_HPP

#include <array>
#include <cstddef>

namespace modguard {

// Number of dynamic-blend appearance bands: low, medium, high.
constexpr std::size_t dynamic_band_count = 3;

// Per-band admission weights for the dynamic-blend preset.
//
// The weights blend three appearance bands (small/light, default, large/dark)
// as smoke demand rises. The weights always sum to 1 so the expected total
// emission is unchanged: demand is split across the bands, not added.
struct BlendWeights {
    std::array<float, dynamic_band_count> value{0.0F, 0.0F, 0.0F};

    float sum() const;
    float &operator[](std::size_t index) { return value[index]; }
    float operator[](std::size_t index) const { return value[index]; }
};

// Maps demand in [0, 1] to three band weights:
//
//   demand 0.00 -> low 1.0, medium 0.0, high 0.0
//   demand 0.25 -> low 0.5, medium 0.5, high 0.0
//   demand 0.50 -> low 0.0, medium 1.0, high 0.0
//   demand 0.75 -> low 0.0, medium 0.5, high 0.5
//   demand 1.00 -> low 0.0, medium 0.0, high 1.0
//
// The low band fades out as demand passes 0.5; the high band fades in. Below
// demand 0.5 the visible bands are low and medium. A NaN demand is treated as
// zero. Other out-of-range demand is clamped to [0, 1].
BlendWeights blend_weights(float demand);

} // namespace modguard

#endif
