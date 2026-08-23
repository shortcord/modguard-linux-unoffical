#include "exhaust_model.hpp"

#include <cassert>
#include <cmath>

int main()
{
    modguard::ExhaustModel model;
    modguard::ExhaustInputs input;
    input.throttle = 1.0F;
    input.rpm = 1500.0F;
    input.rpm_limit = 2000.0F;
    input.gear = 6;

    const auto engine_off = model.update(input, 1.0F / 60.0F);
    assert(engine_off.demand == 0.0F);

    input.engine_enabled = true;
    const auto first = model.update(input, 1.0F / 60.0F);
    const auto later = model.update(input, 1.0F / 60.0F);
    assert(std::isfinite(first.demand));
    assert(first.demand > 0.0F);
    assert(first.demand <= 1.0F);
    assert(first.raw_demand >= first.demand);
    assert(later.throttle_smoothed > first.throttle_smoothed);

    modguard::ExhaustModel zero_delta_model;
    modguard::ExhaustInputs zero_delta_input;
    zero_delta_input.engine_enabled = true;
    zero_delta_input.throttle = 1.0F;
    assert(zero_delta_model.update(zero_delta_input, 0.0F).demand == 0.0F);

    input.gear = 7;
    const auto shifted = model.update(input, 1.0F / 60.0F);
    assert(shifted.demand > 0.0F);

    input.throttle = std::nanf("");
    input.rpm = std::nanf("");
    input.acceleration = std::nanf("");
    const auto invalid = model.update(input, 1.0F / 60.0F);
    assert(std::isfinite(invalid.demand));

    model.reset();
    input = {};
    input.engine_enabled = true;
    input.throttle = 1.0F;
    input.rpm = 800.0F;
    input.rpm_limit = 2000.0F;
    input.gear = 1;
    const auto launch = model.update(input, 1.0F / 60.0F);
    assert(launch.demand > 0.0F);

    input.throttle = 0.0F;
    input.gear = 0;
    for (int i = 0; i < 300; ++i)
        model.update(input, 1.0F / 60.0F);
    const auto coast = model.update(input, 1.0F / 60.0F);
    assert(coast.demand < 0.01F);

    modguard::ExhaustConfig disabled_config;
    disabled_config.smoke_hook = false;
    modguard::ExhaustModel disabled(disabled_config);
    assert(disabled.update(input, 1.0F / 60.0F).demand == 0.0F);

    modguard::ExhaustConfig continuous_config;
    continuous_config.continuous_smoke = true;
    modguard::ExhaustModel continuous(continuous_config);
    input.throttle = 0.5F;
    input.rpm = 1500.0F;
    input.gear = 6;
    assert(continuous.update(input, 1.0F / 60.0F).demand > 0.0F);
    return 0;
}
