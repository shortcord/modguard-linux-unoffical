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

    // Heavy loaded combination smokes more than a light unloaded truck.
    modguard::ExhaustModel load_model;
    modguard::ExhaustInputs light;
    light.engine_enabled = true;
    light.throttle = 1.0F;
    light.rpm = 1500.0F;
    light.rpm_limit = 2000.0F;
    light.gear = 6;
    const auto light_demand = load_model.update(light, 1.0F / 60.0F).demand;

    modguard::ExhaustModel heavy_model;
    modguard::ExhaustInputs heavy = light;
    heavy.cargo_loaded = true;
    heavy.cargo_mass = 20000.0F;
    const auto heavy_demand = heavy_model.update(heavy, 1.0F / 60.0F).demand;
    assert(heavy_demand > light_demand);

    // Weighted additive term: a configured RpmWeight raises demand while the
    // gates let smoke through.
    modguard::ExhaustConfig weighted_config;
    weighted_config.rpm_weight = 3.0F;
    modguard::ExhaustModel weighted(weighted_config);
    modguard::ExhaustInputs weighted_input;
    weighted_input.engine_enabled = true;
    weighted_input.throttle = 1.0F;
    weighted_input.rpm = 1500.0F;
    weighted_input.rpm_limit = 2000.0F;
    weighted_input.gear = 6;
    const auto weighted_demand = weighted.update(weighted_input, 1.0F / 60.0F).demand;

    modguard::ExhaustModel unweighted;
    const auto base_demand = unweighted.update(weighted_input, 1.0F / 60.0F).demand;
    assert(weighted_demand > base_demand);

    // Clutch contribution: pressing the clutch adds smoke through
    // ClutchWeight.
    modguard::ExhaustModel clutch_model;
    modguard::ExhaustInputs clutch_input;
    clutch_input.engine_enabled = true;
    clutch_input.throttle = 1.0F;
    clutch_input.rpm = 1500.0F;
    clutch_input.rpm_limit = 2000.0F;
    clutch_input.gear = 6;
    const auto no_clutch = clutch_model.update(clutch_input, 1.0F / 60.0F).demand;
    clutch_input.clutch = 1.0F;
    const auto clutch_pressed = clutch_model.update(clutch_input, 1.0F / 60.0F).demand;
    assert(clutch_pressed > no_clutch);

    // Curved throttle term reacts to ThrottleCurve: a high exponent lowers
    // the contribution at the same partial throttle.
    modguard::ExhaustConfig curve_low;
    curve_low.throttle_curve = -1;
    modguard::ExhaustModel curve_low_model(curve_low);

    modguard::ExhaustConfig curve_high;
    curve_high.throttle_curve = 100;
    modguard::ExhaustModel curve_high_model(curve_high);

    modguard::ExhaustInputs curve_input;
    curve_input.engine_enabled = true;
    curve_input.throttle = 0.5F;
    curve_input.rpm = 1500.0F;
    curve_input.rpm_limit = 2000.0F;
    curve_input.gear = 6;
    const auto curve_low_demand = curve_low_model.update(curve_input, 1.0F / 60.0F).demand;
    const auto curve_high_demand = curve_high_model.update(curve_input, 1.0F / 60.0F).demand;
    assert(curve_low_demand > curve_high_demand);

    // Sustained-throttle event: holding full throttle grows the demand past
    // one second as the steady envelope passes the threshold.
    modguard::ExhaustModel sustained;
    modguard::ExhaustInputs sustained_input;
    sustained_input.engine_enabled = true;
    sustained_input.throttle = 1.0F;
    sustained_input.rpm = 1500.0F;
    sustained_input.rpm_limit = 2000.0F;
    sustained_input.gear = 6;
    const auto sustained_start = sustained.update(sustained_input, 1.0F / 60.0F).demand;
    modguard::ExhaustOutput sustained_later{};
    for (int i = 0; i < 3600; ++i) {
        sustained_later = sustained.update(sustained_input, 1.0F / 60.0F);
    }
    assert(sustained_later.demand > sustained_start);

    // Free-rev at idle: engine on, no throttle, neutral. The gates multiply
    // the smoke to zero, so idle stays clean. The weighted terms matter only
    // while throttle is applied.
    modguard::ExhaustModel freerev;
    modguard::ExhaustInputs freerev_input;
    freerev_input.engine_enabled = true;
    freerev_input.rpm = 900.0F;
    freerev_input.rpm_limit = 2000.0F;
    modguard::ExhaustOutput free_rev{};
    for (int i = 0; i < 60; ++i) {
        free_rev = freerev.update(freerev_input, 1.0F / 60.0F);
    }
    assert(free_rev.demand == 0.0F);
    assert(free_rev.raw_demand == 0.0F);
    return 0;
}
