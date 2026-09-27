#include "particle_blend.hpp"

#include <cassert>
#include <cmath>
#include <limits>

namespace {

bool near(float a, float b)
{
    return std::fabs(a - b) <= 0.0005F;
}

void check(float demand, float low, float medium, float high)
{
    const modguard::BlendWeights weights = modguard::blend_weights(demand);
    assert(near(weights[0], low));
    assert(near(weights[1], medium));
    assert(near(weights[2], high));
    assert(near(weights.sum(), 1.0F));
}

} // namespace

int main()
{
    // Documented anchor points.
    check(0.00F, 1.0F, 0.0F, 0.0F);
    check(0.25F, 0.5F, 0.5F, 0.0F);
    check(0.50F, 0.0F, 1.0F, 0.0F);
    check(0.75F, 0.0F, 0.5F, 0.5F);
    check(1.00F, 0.0F, 0.0F, 1.0F);

    // Weights stay in [0, 1] and sum to one across the whole range.
    for (int step = 0; step <= 100; ++step) {
        const float demand = static_cast<float>(step) / 100.0F;
        const modguard::BlendWeights weights = modguard::blend_weights(demand);
        assert(weights[0] >= 0.0F && weights[0] <= 1.0F);
        assert(weights[1] >= 0.0F && weights[1] <= 1.0F);
        assert(weights[2] >= 0.0F && weights[2] <= 1.0F);
        assert(near(weights.sum(), 1.0F));
    }

    // Out-of-range and non-finite inputs are clamped, never NaN.
    check(-1.0F, 1.0F, 0.0F, 0.0F);
    check(2.0F, 0.0F, 0.0F, 1.0F);
    check(std::numeric_limits<float>::quiet_NaN(), 1.0F, 0.0F, 0.0F);
    check(std::numeric_limits<float>::infinity(), 0.0F, 0.0F, 1.0F);

    // The low band is off once demand reaches 0.5 and the high band is off
    // below 0.5. This prevents a demand value from emitting two appearance
    // extremes at once.
    assert(near(modguard::blend_weights(0.5F)[0], 0.0F));
    assert(near(modguard::blend_weights(0.5F)[2], 0.0F));
    return 0;
}
