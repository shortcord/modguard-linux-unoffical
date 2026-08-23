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

void ExhaustModel::reset()
{
    throttle_smoothed_ = 0.0F;
    rpm_smoothed_ = 0.0F;
    boost_ = 0.0F;
    previous_throttle_ = 0.0F;
    puff_ = 0.0F;
    shift_transient_ = 0.0F;
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
    const float idle_frac = 0.10F;

    // Boost lag (turbo dynamics) — first-order lag on boost demand
    const float boost_demand = throttle_smoothed_ * (0.4F + 0.6F * rpm_norm);
    const float tau_boost = 1.2F;
    boost_ += (boost_demand - boost_) * std::clamp(dt / tau_boost, 0.0F, 1.0F);

    // RPM factor: lugging (low rpm) worsens smoke, high rpm reduces it
    float rpm_factor;
    if (rpm_norm < idle_frac) {
        rpm_factor = 1.0F;
    } else {
        rpm_factor = lerp(1.6F, 0.7F, (rpm_norm - idle_frac) / (1.0F - idle_frac));
    }

    // Effective load: demanded fuel relative to available air
    const float air = rpm_norm * (0.3F + 0.7F * boost_);
    const float load = std::clamp(throttle_smoothed_ / std::max(air, 0.15F), 0.0F, 1.5F);

    // Steady-state smoke from load — near-zero below 50%, sharp rise above
    float smoke_load;
    if (load <= 0.5F) {
        smoke_load = 0.02F * load;
    } else {
        smoke_load = 0.01F + std::pow(load - 0.5F, 1.8F) * 0.9F;
    }
    smoke_load *= rpm_factor;

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

    // Combine smoke contributions
    float smoke = smoke_load;
    smoke += puff_ * 0.7F * config_.throttle_transient_strength;
    smoke += shift_transient_ * 0.5F * config_.shift_weight;
    if (speed < 2.0F && throttle > 0.5F && raw.gear >= 1) {
        smoke += 0.15F * throttle * config_.launch_weight;
    }

    // Cargo effect
    const float cargo = raw.cargo_loaded
        ? clamp01(raw.cargo_mass / 40000.0F) : 0.0F;
    smoke *= 1.0F + 0.25F * cargo * config_.load_sensitivity;

    // Apply config scaling
    smoke *= config_.intensity * config_.final_smoke_multiplier;

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
