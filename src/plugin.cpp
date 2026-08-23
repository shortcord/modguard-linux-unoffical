#include "config.hpp"
#include "exhaust_model.hpp"
#include "particle_hook.hpp"
#include "scs_sdk_minimal.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

modguard::ExhaustInputs inputs;
modguard::ExhaustModel model;
modguard::LoadedConfig loaded_config;
scs_log_t game_log = nullptr;
scs_timestamp_t current_time = 0;
scs_timestamp_t previous_time = 0;
scs_timestamp_t last_publish_time = 0;
float delta_seconds = 0.0F;
std::uint64_t sequence = 0;
bool paused = true;
int output_socket = -1;
sockaddr_un output_address{};
socklen_t output_address_length = 0;
scs_telemetry_unregister_from_event_t unregister_event = nullptr;
scs_telemetry_unregister_from_channel_t unregister_channel = nullptr;
unsigned registered_event_count = 0;
unsigned registered_channel_count = 0;

enum class Channel {
    speed,
    rpm,
    throttle,
    gear,
    clutch,
    engine_enabled,
    acceleration
};

Channel speed_channel = Channel::speed;
Channel rpm_channel = Channel::rpm;
Channel throttle_channel = Channel::throttle;
Channel gear_channel = Channel::gear;
Channel clutch_channel = Channel::clutch;
Channel engine_channel = Channel::engine_enabled;
Channel acceleration_channel = Channel::acceleration;

void log(scs_log_type_t type, const char *message)
{
    if (game_log) {
        game_log(type, message);
    }
}

void close_output()
{
    if (output_socket >= 0) {
        close(output_socket);
        output_socket = -1;
    }
}

struct ChannelRegistration {
    const char *name;
    scs_value_type_t type;
    Channel *channel;
};

constexpr scs_event_t event_registrations[] = {
    SCS_TELEMETRY_EVENT_frame_start,
    SCS_TELEMETRY_EVENT_frame_end,
    SCS_TELEMETRY_EVENT_paused,
    SCS_TELEMETRY_EVENT_started,
    SCS_TELEMETRY_EVENT_configuration
};

void unregister_callbacks();

void open_output()
{
    close_output();
    const char *configured_path = std::getenv("MODGUARD_EXHAUST_SOCKET");
    std::string path;
    if (configured_path && configured_path[0] == '/') {
        path = configured_path;
    } else {
        path = "/tmp/modguard-exhaust-" + std::to_string(getuid()) + ".sock";
    }
    if (path.size() >= sizeof(output_address.sun_path)) {
        log(SCS_LOG_TYPE_warning, "[MODGuard Linux] Exhaust socket path is too long; output disabled");
        return;
    }

    output_socket = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (output_socket < 0) {
        log(SCS_LOG_TYPE_warning, "[MODGuard Linux] Could not create exhaust output socket");
        return;
    }
    output_address.sun_family = AF_UNIX;
    std::memcpy(output_address.sun_path, path.c_str(), path.size() + 1);
    output_address_length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
}

void publish(const modguard::ExhaustOutput &output)
{
    if (output_socket < 0 || current_time - last_publish_time < 100000) {
        return;
    }
    last_publish_time = current_time;
    char message[384];
    const auto finite = [](float value) { return std::isfinite(value) ? value : 0.0F; };
    const int length = std::snprintf(
        message, sizeof(message),
        "{\"schema\":\"exhaust_demand_v0\",\"seq\":%llu,\"time_us\":%llu,"
        "\"demand\":%.4f,\"raw_demand\":%.4f,\"session_peak\":%.4f,"
        "\"throttle\":%.4f,\"rpm\":%.1f,\"rpm_limit\":%.1f,"
        "\"speed\":%.3f,\"gear\":%d,\"engine\":%s,\"cargo_loaded\":%s,"
        "\"cargo_mass\":%.1f,\"hook_active\":%s}",
        static_cast<unsigned long long>(sequence++),
        static_cast<unsigned long long>(current_time), finite(output.demand), finite(output.raw_demand),
        finite(output.session_peak), finite(inputs.throttle),
        finite(inputs.rpm), finite(inputs.rpm_limit), finite(inputs.speed), inputs.gear,
        inputs.engine_enabled ? "true" : "false",
        inputs.cargo_loaded ? "true" : "false", finite(inputs.cargo_mass),
        modguard::particle_hook_active() ? "true" : "false");
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(message)) {
        return;
    }
    (void)sendto(output_socket, message, static_cast<std::size_t>(length), MSG_DONTWAIT,
                 reinterpret_cast<const sockaddr *>(&output_address), output_address_length);
}

void on_frame_start(scs_event_t, const void *event_info, scs_context_t)
{
    if (!event_info) {
        return;
    }
    const auto *frame = static_cast<const scs_telemetry_frame_start_t *>(event_info);
    current_time = frame->paused_simulation_time;
    if ((frame->flags & SCS_TELEMETRY_FRAME_START_FLAG_timer_restart) != 0 ||
        previous_time == 0 || current_time < previous_time) {
        delta_seconds = 0.0F;
        model.reset();
    } else {
        delta_seconds = static_cast<float>(current_time - previous_time) / 1000000.0F;
    }
    previous_time = current_time;
}

void on_frame_end(scs_event_t, const void *, scs_context_t)
{
    if (!paused) {
        const auto output = model.update(inputs, delta_seconds);
        modguard::set_particle_demand(output.demand, inputs.engine_enabled);
        publish(output);
    }
}

void on_pause(scs_event_t event, const void *, scs_context_t)
{
    paused = event == SCS_TELEMETRY_EVENT_paused;
    if (paused) {
        model.reset();
        modguard::set_particle_demand(0.0F, false);
    }
}

void on_channel(scs_string_t, scs_u32_t, const scs_value_t *value, scs_context_t context)
{
    if (!context) {
        return;
    }
    const Channel channel = *static_cast<Channel *>(context);
    if (!value) {
        switch (channel) {
        case Channel::speed: inputs.speed = 0.0F; break;
        case Channel::rpm: inputs.rpm = 0.0F; break;
        case Channel::throttle: inputs.throttle = 0.0F; break;
        case Channel::gear: inputs.gear = 0; break;
        case Channel::clutch: inputs.clutch = 0.0F; break;
        case Channel::engine_enabled: inputs.engine_enabled = false; break;
        case Channel::acceleration: inputs.acceleration = 0.0F; break;
        }
        return;
    }
    switch (channel) {
    case Channel::speed:
        if (value->type == SCS_VALUE_TYPE_float) inputs.speed = value->value_float.value;
        break;
    case Channel::rpm:
        if (value->type == SCS_VALUE_TYPE_float) inputs.rpm = value->value_float.value;
        break;
    case Channel::throttle:
        if (value->type == SCS_VALUE_TYPE_float) inputs.throttle = value->value_float.value;
        break;
    case Channel::gear:
        if (value->type == SCS_VALUE_TYPE_s32) inputs.gear = value->value_s32.value;
        break;
    case Channel::clutch:
        if (value->type == SCS_VALUE_TYPE_float) inputs.clutch = value->value_float.value;
        break;
    case Channel::engine_enabled:
        if (value->type == SCS_VALUE_TYPE_bool) inputs.engine_enabled = value->value_bool.value != 0;
        break;
    case Channel::acceleration:
        if (value->type == SCS_VALUE_TYPE_fvector) {
            const auto &vector = value->value_fvector;
            inputs.acceleration = std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
        }
        break;
    }
}

void on_configuration(scs_event_t, const void *event_info, scs_context_t)
{
    if (!event_info) {
        return;
    }
    const auto *configuration = static_cast<const scs_telemetry_configuration_t *>(event_info);
    if (!configuration->id || !configuration->attributes) {
        return;
    }
    const bool truck = std::strcmp(configuration->id, SCS_TELEMETRY_CONFIG_truck) == 0;
    const bool job = std::strcmp(configuration->id, SCS_TELEMETRY_CONFIG_job) == 0;
    if (truck) inputs.rpm_limit = 2500.0F;
    if (job) {
        inputs.cargo_mass = 0.0F;
        inputs.cargo_loaded = false;
    }
    for (const scs_named_value_t *attribute = configuration->attributes;
         (truck || job) && attribute->name; ++attribute) {
        if (truck && std::strcmp(attribute->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_rpm_limit) == 0 &&
            attribute->value.type == SCS_VALUE_TYPE_float) {
            const float value = attribute->value.value_float.value;
            if (std::isfinite(value)) inputs.rpm_limit = std::clamp(value, 100.0F, 10000.0F);
        } else if (job && std::strcmp(attribute->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_cargo_mass) == 0 &&
                   attribute->value.type == SCS_VALUE_TYPE_float) {
            const float value = attribute->value.value_float.value;
            if (std::isfinite(value)) inputs.cargo_mass = std::max(0.0F, value);
        } else if (job && std::strcmp(attribute->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_is_cargo_loaded) == 0 &&
                   attribute->value.type == SCS_VALUE_TYPE_bool) {
            inputs.cargo_loaded = attribute->value.value_bool.value != 0;
        }
    }
}

bool register_event(const scs_telemetry_init_params_v100_t &params, scs_event_t event,
                    scs_telemetry_event_callback_t callback)
{
    if (params.register_for_event(event, callback, nullptr) != SCS_RESULT_ok) {
        return false;
    }
    ++registered_event_count;
    return true;
}

bool register_channel(const scs_telemetry_init_params_v100_t &params, const char *name,
                      scs_value_type_t type, Channel *channel)
{
    const scs_u32_t flags = SCS_TELEMETRY_CHANNEL_FLAG_each_frame |
                            SCS_TELEMETRY_CHANNEL_FLAG_no_value;
    if (params.register_for_channel(name, SCS_U32_NIL, type, flags, on_channel, channel) != SCS_RESULT_ok) {
        return false;
    }
    ++registered_channel_count;
    return true;
}

const ChannelRegistration channel_registrations[] = {
    {SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_VALUE_TYPE_float, &speed_channel},
    {SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, SCS_VALUE_TYPE_float, &rpm_channel},
    {SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle, SCS_VALUE_TYPE_float, &throttle_channel},
    {SCS_TELEMETRY_TRUCK_CHANNEL_displayed_gear, SCS_VALUE_TYPE_s32, &gear_channel},
    {SCS_TELEMETRY_TRUCK_CHANNEL_effective_clutch, SCS_VALUE_TYPE_float, &clutch_channel},
    {SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled, SCS_VALUE_TYPE_bool, &engine_channel},
    {SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_acceleration, SCS_VALUE_TYPE_fvector, &acceleration_channel}
};

void unregister_callbacks()
{
    if (unregister_channel) {
        while (registered_channel_count > 0) {
            --registered_channel_count;
            const auto &registration = channel_registrations[registered_channel_count];
            (void)unregister_channel(registration.name, SCS_U32_NIL, registration.type);
        }
    }
    if (unregister_event) {
        while (registered_event_count > 0) {
            --registered_event_count;
            (void)unregister_event(event_registrations[registered_event_count]);
        }
    }
}

} // namespace

extern "C" scs_result_t scs_telemetry_init(
    scs_u32_t version, const scs_telemetry_init_params_t *params_base)
{
    if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01) {
        return SCS_RESULT_unsupported;
    }
    if (!params_base) {
        return SCS_RESULT_generic_error;
    }
    const auto &params = *static_cast<const scs_telemetry_init_params_v100_t *>(params_base);
    game_log = params.common.log;
    unregister_event = params.unregister_from_event;
    unregister_channel = params.unregister_from_channel;
    registered_event_count = 0;
    registered_channel_count = 0;
    if (!params.common.game_id ||
        (std::strcmp(params.common.game_id, SCS_GAME_ID_ATS) != 0 &&
         std::strcmp(params.common.game_id, SCS_GAME_ID_EUT2) != 0)) {
        log(SCS_LOG_TYPE_error, "[MODGuard Linux] Unsupported game");
        game_log = nullptr;
        return SCS_RESULT_unsupported;
    }

    inputs = {};
    loaded_config = modguard::load_config(params.common.game_id);
    model = modguard::ExhaustModel(loaded_config.exhaust);
    current_time = previous_time = last_publish_time = 0;
    delta_seconds = 0.0F;
    sequence = 0;
    paused = true;

    const bool events_ok =
        register_event(params, SCS_TELEMETRY_EVENT_frame_start, on_frame_start) &&
        register_event(params, SCS_TELEMETRY_EVENT_frame_end, on_frame_end) &&
        register_event(params, SCS_TELEMETRY_EVENT_paused, on_pause) &&
        register_event(params, SCS_TELEMETRY_EVENT_started, on_pause) &&
        register_event(params, SCS_TELEMETRY_EVENT_configuration, on_configuration);
    bool channels_ok = true;
    for (const auto &registration : channel_registrations) {
        if (!register_channel(params, registration.name, registration.type, registration.channel)) {
            channels_ok = false;
            break;
        }
    }
    if (!events_ok || !channels_ok) {
        log(SCS_LOG_TYPE_error, "[MODGuard Linux] Could not register required telemetry callbacks");
        unregister_callbacks();
        game_log = nullptr;
        return SCS_RESULT_generic_error;
    }

    open_output();
    if (loaded_config.found) {
        const std::string message = "[MODGuard Linux] Loaded configuration from " + loaded_config.path;
        log(SCS_LOG_TYPE_message, message.c_str());
    } else {
        log(SCS_LOG_TYPE_warning, "[MODGuard Linux] Configuration not found; using defaults");
    }
    if (!loaded_config.exhaust.smoke_hook) {
        log(SCS_LOG_TYPE_warning, "[MODGuard Linux] Particle hook disabled by SmokeHook=0");
    } else if (modguard::install_particle_hook(params.common.game_id, params.common.game_version)) {
        log(SCS_LOG_TYPE_message, "[MODGuard Linux] Exhaust particle hook installed");
    } else if (std::strcmp(params.common.game_id, SCS_GAME_ID_EUT2) == 0) {
        log(SCS_LOG_TYPE_warning,
            "[MODGuard Linux] Particle hook not installed: unsupported ETS2 executable fingerprint");
    } else {
        log(SCS_LOG_TYPE_warning,
            "[MODGuard Linux] Particle hook not installed: unsupported ATS executable fingerprint");
    }
    log(SCS_LOG_TYPE_message, "[MODGuard Linux] Exhaust telemetry client initialized");
    return SCS_RESULT_ok;
}

extern "C" void scs_telemetry_shutdown()
{
    modguard::set_particle_demand(0.0F, false);
    unregister_callbacks();
    modguard::remove_particle_hook();
    close_output();
    log(SCS_LOG_TYPE_message, "[MODGuard Linux] Exhaust telemetry client shut down");
    unregister_event = nullptr;
    unregister_channel = nullptr;
    game_log = nullptr;
}
