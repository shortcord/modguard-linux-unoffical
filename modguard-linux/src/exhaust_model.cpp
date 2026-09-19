#include "exhaust_model.hpp"

#include <algorithm>
#include <cmath>

namespace modguard {
namespace {

float clamp01(float value)
{
    return std::clamp(std::isfinite(value) ? value : 0.0F, 0.0F, 1.0F);
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * std::clamp(t, 0.0F, 1.0F);
}

float decay(float value, float delta_seconds, float duration)
{
    if (duration <= 0.0F) {
        return 0.0F;
    }
    return value * std::exp(-delta_seconds / duration);
}

} // namespace

ExhaustModel::ExhaustModel(ExhaustConfig config) : config_(config) {}

void ExhaustModel::apply_config(const ExhaustConfig &config)
{
    // Replace config only; preserve smoothing state so reloads do not glitch.
    config_ = config;
}

void ExhaustModel::reset()
{
    throttle_smoothed_ = 0.0F;
    rpm_smoothed_ = 0.0F;
    boost_ = 0.0F;
    previous_throttle_ = 0.0F;
    puff_ = 0.0F;
    shift_transient_ = 0.0F;
    launch_envelope_ = 0.0F;
    free_rev_envelope_ = 0.0F;
    steady_envelope_ = 0.0F;
    session_peak_ = 0.0F;
    previous_gear_ = 0;
    have_previous_gear_ = false;
}

ExhaustOutput ExhaustModel::update(const ExhaustInputs &raw, float delta_seconds)
{
    if (!raw.engine_enabled) {
        reset();
        return {};
    }

    const float dt = std::clamp(std::isfinite(delta_seconds) ? delta_seconds : 0.0F, 0.0F, 0.25F);
    const float throttle = clamp01(raw.throttle);

    // Throttle smoothing (attack/release)
    const float tau = throttle > throttle_smoothed_
        ? std::max(config_.attack_smoothness, 0.001F)
        : std::max(config_.release_smoothness, 0.001F);
    const float alpha = 1.0F - std::exp(-dt / tau);
    throttle_smoothed_ += alpha * (throttle - throttle_smoothed_);

    // RPM normalization
    const float rpm_limit = raw.rpm_limit > 1.0F && std::isfinite(raw.rpm_limit)
        ? raw.rpm_limit : 2500.0F;
    const float telemetry_alpha = 1.0F - std::exp(-dt / 0.3F);
    rpm_smoothed_ += telemetry_alpha * ((std::isfinite(raw.rpm) ? raw.rpm : 0.0F) - rpm_smoothed_);
    const float rpm_norm = clamp01(rpm_smoothed_ / rpm_limit);

    // Boost lag (turbo dynamics) — first-order lag on boost demand
    const float boost_demand = throttle_smoothed_ * (0.4F + 0.6F * rpm_norm);
    const float tau_boost = 1.2F;
    boost_ += (boost_demand - boost_) * std::clamp(dt / tau_boost, 0.0F, 1.0F);

    // Effective load: demanded fuel relative to available air
    const float air = rpm_norm * (0.3F + 0.7F * boost_);
    const float load = std::clamp(throttle_smoothed_ / std::max(air, 0.15F), 0.0F, 1.5F);

    // Steady-state smoke from load — near-zero below 20%, sharp rise above
    float smoke_load;
    if (load <= 0.2F) {
        smoke_load = 0.02F * load;
    } else {
        smoke_load = 0.01F + std::pow(load - 0.2F, 1.8F) * 0.9F;
    }
    smoke_load = std::max(0.0F, smoke_load);

    // Transient puff (turbo-lag over-fueling)
    const float dthrottle = dt > 0.0F
        ? std::max(0.0F, throttle - previous_throttle_) / dt
        : 0.0F;
    const float puff_target = std::clamp(dthrottle * 0.4F + std::max(0.0F, load - 1.0F) * 1.5F, 0.0F, 1.0F);
    puff_ = std::max(puff_target, decay(puff_, dt, 1.5F));
    previous_throttle_ = throttle;

    // Gear shift puff
    if (have_previous_gear_ && raw.gear != previous_gear_) {
        shift_transient_ = 1.0F;
    } else {
        shift_transient_ = decay(shift_transient_, dt, 0.3F);
    }
    previous_gear_ = raw.gear;
    have_previous_gear_ = true;

    const float speed = std::fabs(std::isfinite(raw.speed) ? raw.speed : 0.0F);

    // Launch envelope: zero-speed-to-throttle event (Windows RE section 6.2).
    // Decays with tau = 1.5 s and stops after the 3 s window.
    if (speed < 1.0F && throttle > 0.5F) {
        launch_envelope_ = 1.0F;
    }
    launch_envelope_ = decay(launch_envelope_, dt, 1.5F);
    if (launch_envelope_ < 0.135F) {
        launch_envelope_ = 0.0F;
    }

    // Free-rev envelope: engine on, no throttle, neutral. Rises while the
    // condition holds and decays quickly (tau = 0.5 s) otherwise.
    const bool free_revving = throttle < 0.05F && raw.gear == 0;
    free_rev_envelope_ = free_revving
        ? std::clamp(free_rev_envelope_ + dt, 0.0F, 1.0F)
        : decay(free_rev_envelope_, dt, 0.5F);

    // Steady envelope: slow follower for the sustained-throttle event. Rise
    // tau = 8 s, immediate release.
    steady_envelope_ = throttle_smoothed_ > steady_envelope_
        ? steady_envelope_ + (throttle_smoothed_ - steady_envelope_) * std::clamp(dt / 8.0F, 0.0F, 1.0F)
        : 0.0F;

    // Combine smoke contributions
    float smoke = smoke_load;

    // Air deficit: smoke from demanding power (throttle) without enough air (boost)
    const float air_deficit = std::max(0.0F, throttle_smoothed_ - boost_);
    smoke += air_deficit * 0.4F * config_.throttle_transient_strength;

    smoke += puff_ * 0.7F * config_.throttle_transient_strength;
    smoke += shift_transient_ * 0.5F * config_.shift_weight;
    smoke += launch_envelope_ * std::max(0.0F, config_.launch_weight);

    // Weighted additive terms recovered from MODGuard (Windows) §6.3. They
    // enter before the gates, with the same clamped normalizations.
    const float throttle_curve_exponent = config_.throttle_curve == -1
        ? 1.0F
        : 1.0F + static_cast<float>(config_.throttle_curve) * 0.01F;
    const float curved_throttle = std::pow(throttle_smoothed_, throttle_curve_exponent);
    smoke += curved_throttle * std::max(0.0F, config_.sustained_throttle_strength) * 0.01F;
    smoke += rpm_norm * std::max(0.0F, config_.rpm_weight);
    smoke += std::clamp(speed / 30.0F, 0.0F, 1.0F) * std::max(0.0F, config_.speed_weight);
    const float accel = clamp01(std::isfinite(raw.acceleration) ? raw.acceleration : 0.0F);
    smoke += accel * std::max(0.0F, config_.acceleration_weight);
    smoke += clamp01(raw.clutch) * std::max(0.0F, config_.clutch_weight);
    smoke += (raw.cargo_loaded ? clamp01(raw.cargo_mass / 30000.0F) : 0.0F)
        * std::max(0.0F, config_.cargo_weight);
    smoke += std::clamp(load, 0.0F, 1.0F) * std::max(0.0F, config_.load_effort_weight);
    smoke += accel * std::max(0.0F, config_.traction_acceleration_weight);
    smoke += free_rev_envelope_ * std::max(0.0F, config_.free_rev_weight);

    // Sustained-throttle event. The role of EventStrength was not recovered
    // from the binary; this follows the documented hypothesis in
    // docs/01-reverse-engineering.md.
    if (steady_envelope_ > std::max(0.0F, config_.sustained_throttle_strength)) {
        smoke += steady_envelope_ * std::max(0.0F, config_.event_strength);
    }

    // Gross weight: truck curb weight plus the trailer and cargo when attached.
    const bool loaded = raw.cargo_loaded;
    const float payload = loaded
        ? std::max(0.0F, config_.trailer_mass_kg) + std::max(0.0F, raw.cargo_mass)
        : 0.0F;
    const float gross = std::max(0.0F, config_.truck_mass_kg) + payload;

    // Load gate rises steeply with total weight: an unloaded truck stays at a low
    // baseline, while a heavy, loaded combination approaches full smoke.
    const float gross_reference = std::max(config_.gross_weight_reference_kg, 1.0F);
    const float gross_norm = clamp01(gross / gross_reference);
    const float load_gate = lerp(std::clamp(config_.load_baseline, 0.0F, 1.0F), 1.0F,
                                 std::pow(gross_norm, std::max(config_.load_curve_power, 0.1F)));

    // Throttle shapes the puff. Diesel smoke comes from over-fueling, which
    // happens mainly under heavy throttle with little boost available.
    const float throttle_gate = std::pow(throttle_smoothed_, std::clamp(config_.throttle_gate_power, 0.1F, 5.0F));

    // Prevent the gates from killing the smoke if throttle is meaningfully applied.
    // If throttle > 0.1, we ensure the gates don't drop below a baseline visibility threshold.
    float combined_gate = load_gate * throttle_gate;
    if (throttle_smoothed_ > 0.1F) {
        combined_gate = std::max(combined_gate, 0.2F * throttle_smoothed_);
    }

    smoke *= combined_gate;
    smoke *= config_.intensity * config_.final_smoke_multiplier;

    // Heavy-load top end: the emission ceiling climbs with gross weight so a
    // loaded, working truck can reach far more smoke than a light one. The load
    // curve already suppresses the light baseline; this lift widens the spread.
    const float load_boost = lerp(std::max(0.0F, config_.load_boost_min),
                                  std::max(0.0F, config_.load_boost_max), load_gate);
    smoke *= load_boost * std::max(0.0F, config_.load_sensitivity) + (1.0F - std::max(0.0F, config_.load_sensitivity));

    if (config_.continuous_smoke) {
        smoke = std::max(smoke, 0.05F * std::max(0.0F, config_.intensity)
                                     * std::max(0.0F, config_.final_smoke_multiplier));
    }
    if (!config_.smoke_hook) {
        smoke = 0.0F;
    }
    smoke = std::max(0.0F, std::isfinite(smoke) ? smoke : 0.0F);
    session_peak_ = std::max(session_peak_ * config_.session_peak_percent, smoke);

    return {clamp01(smoke), smoke, throttle_smoothed_, rpm_norm, session_peak_};
}

} // namespace modguard
