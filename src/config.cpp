#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>

namespace modguard {
namespace {

std::string trim(std::string value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

bool parse_float(const std::string &text, float &target)
{
    char *end = nullptr;
    errno = 0;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(value)) {
        return false;
    }
    target = value;
    return true;
}

bool parse_int(const std::string &text, int &target)
{
    char *end = nullptr;
    errno = 0;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE ||
        value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        return false;
    }
    target = static_cast<int>(value);
    return true;
}

bool parse_bool(const std::string &text, bool &target)
{
    std::string value = text;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        target = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        target = false;
        return true;
    }
    return false;
}

void assign(ExhaustConfig &config, const std::string &key, const std::string &value)
{
    if (key == "SmokeHook") parse_bool(value, config.smoke_hook);
    else if (key == "ContinuousSmoke") parse_bool(value, config.continuous_smoke);
    else if (key == "FinalSmokeMultiplier") parse_float(value, config.final_smoke_multiplier);
    else if (key == "Intensity") parse_float(value, config.intensity);
    else if (key == "LoadSensitivity") parse_float(value, config.load_sensitivity);
    else if (key == "TruckMassKg") parse_float(value, config.truck_mass_kg);
    else if (key == "TrailerMassKg") parse_float(value, config.trailer_mass_kg);
    else if (key == "LoadCurvePower") parse_float(value, config.load_curve_power);
    else if (key == "GrossWeightReferenceKg") parse_float(value, config.gross_weight_reference_kg);
    else if (key == "LoadBaseline") parse_float(value, config.load_baseline);
    else if (key == "ThrottleGatePower") parse_float(value, config.throttle_gate_power);
    else if (key == "RpmLeanFraction") parse_float(value, config.rpm_lean_fraction);
    else if (key == "RpmLeanPower") parse_float(value, config.rpm_lean_power);
    else if (key == "LoadBoostMin") parse_float(value, config.load_boost_min);
    else if (key == "LoadBoostMax") parse_float(value, config.load_boost_max);
    else if (key == "AttackSmoothness") parse_float(value, config.attack_smoothness);
    else if (key == "ReleaseSmoothness") parse_float(value, config.release_smoothness);
    else if (key == "EventStrength") parse_float(value, config.event_strength);
    else if (key == "SustainedThrottleStrength") parse_float(value, config.sustained_throttle_strength);
    else if (key == "ThrottleTransientStrength") parse_float(value, config.throttle_transient_strength);
    else if (key == "ThrottleRiseThreshold") parse_float(value, config.throttle_rise_threshold);
    else if (key == "ThrottleTransientDecay") parse_float(value, config.throttle_transient_decay);
    else if (key == "ThrottleCurve") parse_int(value, config.throttle_curve);
    else if (key == "ThrottleWeight") parse_float(value, config.throttle_weight);
    else if (key == "RpmWeight") parse_float(value, config.rpm_weight);
    else if (key == "SpeedWeight") parse_float(value, config.speed_weight);
    else if (key == "AccelerationWeight") parse_float(value, config.acceleration_weight);
    else if (key == "GearWeight") parse_float(value, config.gear_weight);
    else if (key == "ClutchWeight") parse_float(value, config.clutch_weight);
    else if (key == "CargoWeight") parse_float(value, config.cargo_weight);
    else if (key == "LaunchWeight") parse_float(value, config.launch_weight);
    else if (key == "TractionAccelerationWeight") parse_float(value, config.traction_acceleration_weight);
    else if (key == "LoadEffortWeight") parse_float(value, config.load_effort_weight);
    else if (key == "FreeRevWeight") parse_float(value, config.free_rev_weight);
    else if (key == "ShiftWeight") parse_float(value, config.shift_weight);
    else if (key == "SessionPeakPercent") parse_float(value, config.session_peak_percent);
}

void sanitize(ExhaustConfig &config)
{
    config.throttle_curve = std::clamp(config.throttle_curve, -1, 100);
    config.attack_smoothness = std::max(0.0F, config.attack_smoothness);
    config.release_smoothness = std::max(0.0F, config.release_smoothness);
    config.throttle_rise_threshold = std::clamp(config.throttle_rise_threshold, 0.0F, 1.0F);
    config.throttle_transient_decay = std::max(0.0F, config.throttle_transient_decay);
    config.session_peak_percent = std::clamp(config.session_peak_percent, 0.0F, 1.0F);
    config.truck_mass_kg = std::clamp(config.truck_mass_kg, 0.0F, 50000.0F);
    config.trailer_mass_kg = std::clamp(config.trailer_mass_kg, 0.0F, 50000.0F);
    config.load_curve_power = std::clamp(config.load_curve_power, 0.1F, 10.0F);
    config.gross_weight_reference_kg = std::clamp(config.gross_weight_reference_kg, 1.0F, 1000000.0F);
    config.load_baseline = std::clamp(config.load_baseline, 0.0F, 1.0F);
    config.throttle_gate_power = std::clamp(config.throttle_gate_power, 0.1F, 5.0F);
    config.rpm_lean_fraction = std::clamp(config.rpm_lean_fraction, 0.1F, 1.0F);
    config.rpm_lean_power = std::clamp(config.rpm_lean_power, 0.1F, 5.0F);
    config.load_boost_min = std::clamp(config.load_boost_min, 0.0F, 100.0F);
    config.load_boost_max = std::clamp(config.load_boost_max, 0.0F, 100.0F);
}

} // namespace

LoadedConfig load_config_file(const std::string &path)
{
    LoadedConfig result;
    result.path = path;
    std::ifstream input(path);
    if (!input) {
        return result;
    }
    result.found = true;

    bool realistic_smoke = false;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            realistic_smoke = trim(line.substr(1, line.size() - 2)) == "RealisticSmoke";
            continue;
        }
        if (!realistic_smoke) {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        std::string value = trim(line.substr(separator + 1));
        const auto comment = value.find_first_of(";#");
        if (comment != std::string::npos) {
            value = trim(value.substr(0, comment));
        }
        assign(result.exhaust, trim(line.substr(0, separator)), value);
    }
    sanitize(result.exhaust);
    return result;
}

LoadedConfig load_config(const char *game_id)
{
    if (const char *override_path = std::getenv("MODGUARD_CONFIG")) {
        if (override_path[0] != '\0') {
            return load_config_file(override_path);
        }
    }
    const char *home = std::getenv("HOME");
    if (!home || home[0] == '\0') {
        return {};
    }
    const std::string game = game_id && std::string(game_id) == "ats"
        ? "American Truck Simulator" : "Euro Truck Simulator 2";
    return load_config_file(std::string(home) + "/.local/share/" + game +
                            "/mod/modguard_linux.ini");
}

} // namespace modguard
