#ifndef MODGUARD_SCS_SDK_MINIMAL_HPP
#define MODGUARD_SCS_SDK_MINIMAL_HPP

// ABI subset of SCS Telemetry SDK 1.14. See LICENSE-SCS-SDK.
#include <cstdint>

extern "C" {

using scs_u8_t = std::uint8_t;
using scs_u32_t = std::uint32_t;
using scs_s32_t = std::int32_t;
using scs_u64_t = std::uint64_t;
using scs_float_t = float;
using scs_string_t = const char *;
using scs_context_t = void *;
using scs_timestamp_t = scs_u64_t;
using scs_result_t = scs_s32_t;
using scs_log_type_t = scs_s32_t;
using scs_event_t = scs_u32_t;
using scs_value_type_t = scs_u32_t;

constexpr scs_u32_t SCS_U32_NIL = static_cast<scs_u32_t>(-1);
constexpr scs_result_t SCS_RESULT_ok = 0;
constexpr scs_result_t SCS_RESULT_unsupported = -1;
constexpr scs_result_t SCS_RESULT_generic_error = -7;
constexpr scs_log_type_t SCS_LOG_TYPE_message = 0;
constexpr scs_log_type_t SCS_LOG_TYPE_warning = 1;
constexpr scs_log_type_t SCS_LOG_TYPE_error = 2;

constexpr scs_u32_t SCS_TELEMETRY_VERSION_1_00 = (1U << 16U);
constexpr scs_u32_t SCS_TELEMETRY_VERSION_1_01 = (1U << 16U) | 1U;

constexpr scs_event_t SCS_TELEMETRY_EVENT_frame_start = 1;
constexpr scs_event_t SCS_TELEMETRY_EVENT_frame_end = 2;
constexpr scs_event_t SCS_TELEMETRY_EVENT_paused = 3;
constexpr scs_event_t SCS_TELEMETRY_EVENT_started = 4;
constexpr scs_event_t SCS_TELEMETRY_EVENT_configuration = 5;

constexpr scs_u32_t SCS_TELEMETRY_FRAME_START_FLAG_timer_restart = 1;
constexpr scs_u32_t SCS_TELEMETRY_CHANNEL_FLAG_each_frame = 1;
constexpr scs_u32_t SCS_TELEMETRY_CHANNEL_FLAG_no_value = 2;

constexpr scs_value_type_t SCS_VALUE_TYPE_bool = 1;
constexpr scs_value_type_t SCS_VALUE_TYPE_s32 = 2;
constexpr scs_value_type_t SCS_VALUE_TYPE_float = 5;
constexpr scs_value_type_t SCS_VALUE_TYPE_fvector = 7;

struct scs_value_fvector_t {
    scs_float_t x;
    scs_float_t y;
    scs_float_t z;
};

struct scs_value_dplacement_t {
    double position[3];
    float orientation[3];
    scs_u32_t padding;
};

struct scs_value_t {
    scs_value_type_t type;
    scs_u32_t padding;
    union {
        struct { scs_u8_t value; } value_bool;
        struct { scs_s32_t value; } value_s32;
        struct { scs_float_t value; } value_float;
        scs_value_fvector_t value_fvector;
        scs_value_dplacement_t size_anchor;
    };
};

struct scs_named_value_t {
    scs_string_t name;
    scs_u32_t index;
    scs_u32_t padding;
    scs_value_t value;
};

struct scs_telemetry_frame_start_t {
    scs_u32_t flags;
    scs_u32_t padding;
    scs_timestamp_t render_time;
    scs_timestamp_t simulation_time;
    scs_timestamp_t paused_simulation_time;
};

struct scs_telemetry_configuration_t {
    scs_string_t id;
    const scs_named_value_t *attributes;
};

using scs_log_t = void (*)(scs_log_type_t, scs_string_t);
using scs_telemetry_event_callback_t = void (*)(scs_event_t, const void *, scs_context_t);
using scs_telemetry_channel_callback_t = void (*)(scs_string_t, scs_u32_t, const scs_value_t *, scs_context_t);
using scs_telemetry_register_for_event_t = scs_result_t (*)(scs_event_t, scs_telemetry_event_callback_t, scs_context_t);
using scs_telemetry_unregister_from_event_t = scs_result_t (*)(scs_event_t);
using scs_telemetry_register_for_channel_t = scs_result_t (*)(scs_string_t, scs_u32_t, scs_value_type_t, scs_u32_t, scs_telemetry_channel_callback_t, scs_context_t);
using scs_telemetry_unregister_from_channel_t = scs_result_t (*)(scs_string_t, scs_u32_t, scs_value_type_t);

struct scs_sdk_init_params_v100_t {
    scs_string_t game_name;
    scs_string_t game_id;
    scs_u32_t game_version;
    scs_u32_t padding;
    scs_log_t log;
};

struct scs_telemetry_init_params_t {};

struct scs_telemetry_init_params_v100_t : scs_telemetry_init_params_t {
    scs_sdk_init_params_v100_t common;
    scs_telemetry_register_for_event_t register_for_event;
    scs_telemetry_unregister_from_event_t unregister_from_event;
    scs_telemetry_register_for_channel_t register_for_channel;
    scs_telemetry_unregister_from_channel_t unregister_from_channel;
};

using scs_telemetry_init_params_v101_t = scs_telemetry_init_params_v100_t;

#if defined(__GNUC__)
#define MODGUARD_EXPORT __attribute__((visibility("default")))
#else
#define MODGUARD_EXPORT
#endif

MODGUARD_EXPORT scs_result_t scs_telemetry_init(scs_u32_t version, const scs_telemetry_init_params_t *params);
MODGUARD_EXPORT void scs_telemetry_shutdown();

} // extern "C"

static_assert(sizeof(scs_value_t) == 48, "Unexpected SCS value ABI");
static_assert(sizeof(scs_named_value_t) == 64, "Unexpected SCS named value ABI");
static_assert(sizeof(scs_sdk_init_params_v100_t) == 32, "Unexpected SCS init ABI");
static_assert(sizeof(scs_telemetry_init_params_v100_t) == 64, "Unexpected telemetry init ABI");
static_assert(sizeof(scs_telemetry_frame_start_t) == 32, "Unexpected frame event ABI");

#define SCS_GAME_ID_EUT2 "eut2"
#define SCS_GAME_ID_ATS "ats"
#define SCS_TELEMETRY_TRUCK_CHANNEL_speed "truck.speed"
#define SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm "truck.engine.rpm"
#define SCS_TELEMETRY_TRUCK_CHANNEL_displayed_gear "truck.displayed.gear"
#define SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle "truck.effective.throttle"
#define SCS_TELEMETRY_TRUCK_CHANNEL_effective_clutch "truck.effective.clutch"
#define SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled "truck.engine.enabled"
#define SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_acceleration "truck.local.acceleration.linear"
#define SCS_TELEMETRY_CONFIG_truck "truck"
#define SCS_TELEMETRY_CONFIG_job "job"
#define SCS_TELEMETRY_CONFIG_ATTRIBUTE_rpm_limit "rpm.limit"
#define SCS_TELEMETRY_CONFIG_ATTRIBUTE_cargo_mass "cargo.mass"
#define SCS_TELEMETRY_CONFIG_ATTRIBUTE_is_cargo_loaded "cargo.loaded"

#endif
