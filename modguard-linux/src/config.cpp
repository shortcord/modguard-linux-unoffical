#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

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

enum class FieldType { Bool, Int, Float };

struct Field {
    std::string_view key;
    FieldType type;
    std::size_t offset;
};

#define MODGUARD_FIELD(name, type) \
    { #name, FieldType::type, offsetof(ExhaustConfig, name) }

constexpr Field kFields[] = {
    MODGUARD_FIELD(smoke_hook, Bool),
    MODGUARD_FIELD(continuous_smoke, Bool),
    MODGUARD_FIELD(final_smoke_multiplier, Float),
    MODGUARD_FIELD(intensity, Float),
    MODGUARD_FIELD(load_sensitivity, Float),
    MODGUARD_FIELD(truck_mass_kg, Float),
    MODGUARD_FIELD(trailer_mass_kg, Float),
    MODGUARD_FIELD(load_curve_power, Float),
    MODGUARD_FIELD(gross_weight_reference_kg, Float),
    MODGUARD_FIELD(load_baseline, Float),
    MODGUARD_FIELD(throttle_gate_power, Float),
    MODGUARD_FIELD(load_boost_min, Float),
    MODGUARD_FIELD(load_boost_max, Float),
    MODGUARD_FIELD(lifetime_multiplier, Float),
    MODGUARD_FIELD(attack_smoothness, Float),
    MODGUARD_FIELD(release_smoothness, Float),
    MODGUARD_FIELD(event_strength, Float),
    MODGUARD_FIELD(sustained_throttle_strength, Float),
    MODGUARD_FIELD(throttle_transient_strength, Float),
    MODGUARD_FIELD(throttle_rise_threshold, Float),
    MODGUARD_FIELD(throttle_transient_decay, Float),
    MODGUARD_FIELD(throttle_curve, Int),
    MODGUARD_FIELD(throttle_weight, Float),
    MODGUARD_FIELD(rpm_weight, Float),
    MODGUARD_FIELD(speed_weight, Float),
    MODGUARD_FIELD(acceleration_weight, Float),
    MODGUARD_FIELD(clutch_weight, Float),
    MODGUARD_FIELD(cargo_weight, Float),
    MODGUARD_FIELD(launch_weight, Float),
    MODGUARD_FIELD(traction_acceleration_weight, Float),
    MODGUARD_FIELD(load_effort_weight, Float),
    MODGUARD_FIELD(free_rev_weight, Float),
    MODGUARD_FIELD(shift_weight, Float),
    MODGUARD_FIELD(session_peak_percent, Float),
};

#undef MODGUARD_FIELD

std::string normalize_key(const std::string &key)
{
    // Convert PascalCase ini keys to lowercase snake_case field names.
    // Example: "SmokeHook" -> "smoke_hook", "TruckMassKg" -> "truck_mass_kg".
    std::string out;
    out.reserve(key.size() + 4);
    for (std::size_t i = 0; i < key.size(); ++i) {
        const char c = key[i];
        if (c >= 'A' && c <= 'Z') {
            if (i > 0) {
                const char prev = key[i - 1];
                const bool boundary = !(prev >= 'A' && prev <= 'Z');
                if (boundary) {
                    out.push_back('_');
                }
            }
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

void assign(ExhaustConfig &config, const std::string &key, const std::string &value)
{
    const std::string normalized = normalize_key(key);
    for (const Field &field : kFields) {
        if (field.key != normalized) {
            continue;
        }
        void *target = reinterpret_cast<char *>(&config) + field.offset;
        switch (field.type) {
        case FieldType::Bool:
            parse_bool(value, *static_cast<bool *>(target));
            break;
        case FieldType::Int:
            parse_int(value, *static_cast<int *>(target));
            break;
        case FieldType::Float:
            parse_float(value, *static_cast<float *>(target));
            break;
        }
        return;
    }
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
    config.load_boost_min = std::clamp(config.load_boost_min, 0.0F, 100.0F);
    config.load_boost_max = std::clamp(config.load_boost_max, 0.0F, 100.0F);
    config.lifetime_multiplier = std::clamp(config.lifetime_multiplier, 0.0F, 60.0F);
    // Weighted-sum terms from the Windows formula. Negative weights would make
    // more throttle or load reduce smoke, so the safe floor is zero.
    config.throttle_weight = std::max(0.0F, config.throttle_weight);
    config.rpm_weight = std::max(0.0F, config.rpm_weight);
    config.speed_weight = std::max(0.0F, config.speed_weight);
    config.acceleration_weight = std::max(0.0F, config.acceleration_weight);
    config.clutch_weight = std::max(0.0F, config.clutch_weight);
    config.cargo_weight = std::max(0.0F, config.cargo_weight);
    config.launch_weight = std::max(0.0F, config.launch_weight);
    config.traction_acceleration_weight = std::max(0.0F, config.traction_acceleration_weight);
    config.load_effort_weight = std::max(0.0F, config.load_effort_weight);
    config.free_rev_weight = std::max(0.0F, config.free_rev_weight);
    config.sustained_throttle_strength = std::max(0.0F, config.sustained_throttle_strength);
    config.event_strength = std::max(0.0F, config.event_strength);
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
