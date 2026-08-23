#include "scs_sdk_minimal.hpp"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

std::vector<scs_event_t> events;
std::vector<std::string> channels;
std::vector<std::string> logs;
unsigned event_unregistrations = 0;
unsigned channel_unregistrations = 0;

struct EventCallback {
    scs_event_t event;
    scs_telemetry_event_callback_t callback;
    scs_context_t context;
};

struct ChannelCallback {
    std::string name;
    scs_value_type_t type;
    scs_telemetry_channel_callback_t callback;
    scs_context_t context;
};

std::vector<EventCallback> event_callbacks;
std::vector<ChannelCallback> channel_callbacks;

void fake_log(scs_log_type_t, scs_string_t message)
{
    logs.emplace_back(message);
}

scs_result_t register_event(scs_event_t event, scs_telemetry_event_callback_t callback, scs_context_t context)
{
    events.push_back(event);
    event_callbacks.push_back({event, callback, context});
    return SCS_RESULT_ok;
}

scs_result_t unregister_event(scs_event_t)
{
    ++event_unregistrations;
    return SCS_RESULT_ok;
}

scs_result_t register_channel(scs_string_t name, scs_u32_t index, scs_value_type_t type,
                              scs_u32_t flags, scs_telemetry_channel_callback_t callback,
                              scs_context_t context)
{
    assert(index == SCS_U32_NIL);
    assert((flags & SCS_TELEMETRY_CHANNEL_FLAG_each_frame) != 0);
    channels.emplace_back(name);
    channel_callbacks.push_back({name, type, callback, context});
    return SCS_RESULT_ok;
}

scs_result_t unregister_channel(scs_string_t, scs_u32_t, scs_value_type_t)
{
    ++channel_unregistrations;
    return SCS_RESULT_ok;
}

EventCallback &event_callback(scs_event_t event)
{
    for (auto &entry : event_callbacks) {
        if (entry.event == event) return entry;
    }
    assert(false);
    return event_callbacks[0];
}

ChannelCallback &channel_callback(const char *name)
{
    for (auto &entry : channel_callbacks) {
        if (entry.name == name) return entry;
    }
    assert(false);
    return channel_callbacks[0];
}

void send_float(const char *name, float number)
{
    auto &entry = channel_callback(name);
    scs_value_t value{};
    value.type = SCS_VALUE_TYPE_float;
    value.value_float.value = number;
    entry.callback(name, SCS_U32_NIL, &value, entry.context);
}

scs_telemetry_init_params_v100_t make_params(const char *game_id)
{
    scs_telemetry_init_params_v100_t params{};
    params.common.game_name = "Test game";
    params.common.game_id = game_id;
    params.common.log = fake_log;
    params.register_for_event = register_event;
    params.unregister_from_event = unregister_event;
    params.register_for_channel = register_channel;
    params.unregister_from_channel = unregister_channel;
    return params;
}

} // namespace

int main()
{
    const std::string socket_path = "/tmp/modguard-plugin-test-" + std::to_string(getuid()) + ".sock";
    unlink(socket_path.c_str());
    const int receiver = socket(AF_UNIX, SOCK_DGRAM, 0);
    assert(receiver >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strcpy(address.sun_path, socket_path.c_str());
    assert(bind(receiver, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0);
    timeval timeout{1, 0};
    assert(setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    assert(setenv("MODGUARD_EXHAUST_SOCKET", socket_path.c_str(), 1) == 0);
    assert(setenv("MODGUARD_CONFIG", "/tmp/modguard-plugin-test-missing.ini", 1) == 0);

    auto params = make_params(SCS_GAME_ID_ATS);
    assert(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_01, &params) == SCS_RESULT_ok);
    assert(events.size() == 5);
    assert(channels.size() == 7);
    assert(channels[0] == SCS_TELEMETRY_TRUCK_CHANNEL_speed);
    assert(!logs.empty());

    scs_named_value_t truck_attributes[2]{};
    truck_attributes[0].name = SCS_TELEMETRY_CONFIG_ATTRIBUTE_rpm_limit;
    truck_attributes[0].index = SCS_U32_NIL;
    truck_attributes[0].value.type = SCS_VALUE_TYPE_float;
    truck_attributes[0].value.value_float.value = 2100.0F;
    scs_telemetry_configuration_t truck_configuration{SCS_TELEMETRY_CONFIG_truck, truck_attributes};
    auto &configuration = event_callback(SCS_TELEMETRY_EVENT_configuration);
    configuration.callback(configuration.event, &truck_configuration, configuration.context);

    send_float(SCS_TELEMETRY_TRUCK_CHANNEL_speed, 12.0F);
    send_float(SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, 1400.0F);
    send_float(SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle, 0.8F);
    scs_value_t engine{};
    engine.type = SCS_VALUE_TYPE_bool;
    engine.value_bool.value = 1;
    auto &engine_callback = channel_callback(SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled);
    engine_callback.callback(engine_callback.name.c_str(), SCS_U32_NIL, &engine, engine_callback.context);

    auto &started = event_callback(SCS_TELEMETRY_EVENT_started);
    started.callback(started.event, nullptr, started.context);
    scs_telemetry_frame_start_t frame{};
    frame.paused_simulation_time = 200000;
    auto &frame_start = event_callback(SCS_TELEMETRY_EVENT_frame_start);
    frame_start.callback(frame_start.event, &frame, frame_start.context);
    auto &frame_end = event_callback(SCS_TELEMETRY_EVENT_frame_end);
    frame_end.callback(frame_end.event, nullptr, frame_end.context);

    char message[1024]{};
    const auto received = recv(receiver, message, sizeof(message) - 1, 0);
    assert(received > 0);
    const std::string json(message, static_cast<std::size_t>(received));
    assert(json.find("\"schema\":\"exhaust_demand_v0\"") != std::string::npos);
    assert(json.find("\"raw_demand\":") != std::string::npos);
    assert(json.find("\"rpm_limit\":2100.0") != std::string::npos);
    assert(json.find("nan") == std::string::npos);

    engine_callback.callback(engine_callback.name.c_str(), SCS_U32_NIL, nullptr,
                             engine_callback.context);
    frame.paused_simulation_time = 400000;
    frame_start.callback(frame_start.event, &frame, frame_start.context);
    frame_end.callback(frame_end.event, nullptr, frame_end.context);
    const auto unavailable_received = recv(receiver, message, sizeof(message) - 1, 0);
    assert(unavailable_received > 0);
    const std::string unavailable_json(message, static_cast<std::size_t>(unavailable_received));
    assert(unavailable_json.find("\"demand\":0.0000") != std::string::npos);
    assert(unavailable_json.find("\"engine\":false") != std::string::npos);
    scs_telemetry_shutdown();
    assert(event_unregistrations == 5);
    assert(channel_unregistrations == 7);

    events.clear();
    channels.clear();
    event_callbacks.clear();
    channel_callbacks.clear();
    params = make_params(SCS_GAME_ID_EUT2);
    assert(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_00, &params) == SCS_RESULT_ok);
    scs_telemetry_shutdown();

    params = make_params("unsupported");
    assert(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_01, &params) == SCS_RESULT_unsupported);
    assert(scs_telemetry_init(0, &params) == SCS_RESULT_unsupported);
    close(receiver);
    unlink(socket_path.c_str());
    return 0;
}
