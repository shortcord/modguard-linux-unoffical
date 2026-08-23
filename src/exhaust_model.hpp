#ifndef MODGUARD_EXHAUST_MODEL_HPP
#define MODGUARD_EXHAUST_MODEL_HPP

#include <cstdint>

namespace modguard {

struct ExhaustConfig {
    bool smoke_hook = true;
    bool continuous_smoke = false;
    float final_smoke_multiplier = 0.8F;
    float intensity = 0.3F;
    float load_sensitivity = 1.0F;
    float attack_smoothness = 0.3F;
    float release_smoothness = 0.3F;
    float event_strength = 1.0F;
    float sustained_throttle_strength = 0.5F;
    float throttle_transient_strength = 0.7F;
    float throttle_rise_threshold = 0.05F;
    float throttle_transient_decay = 0.1F;
    int throttle_curve = 45;
    float throttle_weight = 1.0F;
    float rpm_weight = 1.0F;
    float speed_weight = 1.0F;
    float acceleration_weight = 1.0F;
    float gear_weight = 1.0F;
    float clutch_weight = 1.0F;
    float cargo_weight = 1.0F;
    float launch_weight = 1.0F;
    float traction_acceleration_weight = 1.0F;
    float load_effort_weight = 1.0F;
    float free_rev_weight = 1.0F;
    float shift_weight = 1.0F;
    float session_peak_percent = 0.85F;
};

struct ExhaustInputs {
    float throttle = 0.0F;
    float rpm = 0.0F;
    float rpm_limit = 2500.0F;
    float speed = 0.0F;
    float clutch = 0.0F;
    float acceleration = 0.0F;
    float cargo_mass = 0.0F;
    std::int32_t gear = 0;
    bool engine_enabled = false;
    bool cargo_loaded = false;
};

struct ExhaustOutput {
    float demand = 0.0F;
    float raw_demand = 0.0F;
    float throttle_smoothed = 0.0F;
    float rpm_normalized = 0.0F;
    float session_peak = 0.0F;
};

class ExhaustModel {
public:
    explicit ExhaustModel(ExhaustConfig config = {});
    ExhaustOutput update(const ExhaustInputs &inputs, float delta_seconds);
    void reset();

private:
    ExhaustConfig config_;
    float throttle_smoothed_ = 0.0F;
    float rpm_smoothed_ = 0.0F;
    float boost_ = 0.0F;
    float previous_throttle_ = 0.0F;
    float puff_ = 0.0F;
    float shift_transient_ = 0.0F;
    float session_peak_ = 0.0F;
    std::int32_t previous_gear_ = 0;
    bool have_previous_gear_ = false;
};

} // namespace modguard

#endif
