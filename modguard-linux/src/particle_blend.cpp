#include "particle_blend.hpp"

#include <algorithm>
#include <cmath>

namespace modguard {

float BlendWeights::sum() const
{
    return value[0] + value[1] + value[2];
}

BlendWeights blend_weights(float demand)
{
    const float d = std::isnan(demand) ? 0.0F : std::clamp(demand, 0.0F, 1.0F);
    BlendWeights weights;

    // Two linear ramps. The low band is full at demand 0, falls to zero at
    // 0.50, and stays off. The high band is off below 0.50, then rises to
    // full at 1.0. The medium band fills the remainder. Every weight stays
    // in [0, 1] and the sum is exactly 1 for any demand in [0, 1].
    const float low = std::clamp((0.50F - d) / 0.50F, 0.0F, 1.0F);
    const float high = std::clamp((d - 0.50F) / 0.50F, 0.0F, 1.0F);
    const float medium = 1.0F - low - high;

    weights[0] = low;
    weights[1] = medium;
    weights[2] = high;
    return weights;
}

} // namespace modguard
