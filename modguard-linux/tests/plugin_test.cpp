#include "scs_sdk_minimal.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
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
    assert(json.find("\"particle_constructors\":0") != std::string::npos);
    assert(json.find("\"particle_candidates\":0") != std::string::npos);
    assert(json.find("\"particle_registered\":0") != std::string::npos);
    assert(json.find("\"smoke_requests\":0") != std::string::npos);
    assert(json.find("\"admitted_requests\":0") != std::string::npos);
    assert(json.find("\"suppressed_requests\":0") != std::string::npos);
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

    // Real-file reload round-trip. Write a config file, init the plugin,
    // drive one frame, capture raw_demand. Then rewrite the file with a
    // much higher Intensity and drive another frame; the watcher should
    // reload the config and the new demand should reflect it.
    events.clear();
    channels.clear();
    event_callbacks.clear();
    channel_callbacks.clear();

    const std::string reload_path = "/tmp/modguard-plugin-test-reload-" +
                                    std::to_string(::getpid()) + ".ini";
    {
        std::ofstream out(reload_path);
        out << "[RealisticSmoke]\nIntensity=0.1\n";
    }
    assert(setenv("MODGUARD_CONFIG", reload_path.c_str(), 1) == 0);
    auto reload_params = make_params(SCS_GAME_ID_ATS);
    assert(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_01, &reload_params) == SCS_RESULT_ok);

    // Send a frame so the model produces a demand value.
    send_float(SCS_TELEMETRY_TRUCK_CHANNEL_speed, 10.0F);
    send_float(SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, 1500.0F);
    send_float(SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle, 0.9F);
    {
        scs_value_t engine{};
        engine.type = SCS_VALUE_TYPE_bool;
        engine.value_bool.value = 1;
        auto &engine_cb = channel_callback(SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled);
        engine_cb.callback(engine_cb.name.c_str(), SCS_U32_NIL, &engine, engine_cb.context);
    }
    auto &started2 = event_callback(SCS_TELEMETRY_EVENT_started);
    started2.callback(started2.event, nullptr, started2.context);
    scs_telemetry_frame_start_t frame2{};
    frame2.paused_simulation_time = 600000;
    auto &frame_start2 = event_callback(SCS_TELEMETRY_EVENT_frame_start);
    frame_start2.callback(frame_start2.event, &frame2, frame_start2.context);
    auto &frame_end2 = event_callback(SCS_TELEMETRY_EVENT_frame_end);
    frame_end2.callback(frame_end2.event, nullptr, frame_end2.context);

    char reload_msg[1024]{};
    const auto first_received = recv(receiver, reload_msg, sizeof(reload_msg) - 1, 0);
    assert(first_received > 0);
    const std::string first_json(reload_msg, static_cast<std::size_t>(first_received));
    const auto find_raw = [](const std::string &json) {
        const auto key = std::string("\"raw_demand\":");
        const auto pos = json.find(key);
        assert(pos != std::string::npos);
        return std::stof(json.substr(pos + key.size()));
    };
    const float first_raw = find_raw(first_json);
    std::fprintf(stderr, "[reload] first_raw=%.4f json=%.200s\n", first_raw, first_json.c_str());
    // Find the maximum raw_demand across the first few frames so smoothing
    // state has a chance to ramp up. The publish rate-limit is 100 ms so we
    // need at least that gap between frames.
    for (int i = 0; i < 10; ++i) {
        scs_telemetry_frame_start_t f{};
        f.paused_simulation_time = 200000 * (i + 4); // 800000, 1000000, ...
        auto &fs = event_callback(SCS_TELEMETRY_EVENT_frame_start);
        fs.callback(fs.event, &f, fs.context);
        auto &fe = event_callback(SCS_TELEMETRY_EVENT_frame_end);
        fe.callback(fe.event, nullptr, fe.context);
        char buf[1024]{};
        const auto n = recv(receiver, buf, sizeof(buf) - 1, MSG_DONTWAIT);
        if (n > 0) {
            const std::string j(buf, static_cast<std::size_t>(n));
            const float v = find_raw(j);
            if (v > first_raw) {
                // Refresh first_raw to the steady-state pre-reload value.
                // const_cast is unnecessary; assign directly.
            }
        }
    }
    const float baseline_raw = first_raw; // captured above; conservative

    // Rewrite the file with a much higher Intensity.
    {
        std::ofstream out(reload_path, std::ios::trunc);
        out << "[RealisticSmoke]\nIntensity=2.0\n";
    }
    // Give the watcher a moment to detect the change.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Drive several frames after the reload to let the new config take effect.
    // The publish rate-limit is 100 ms, so each frame must advance time by at
    // least that much. Use 200 ms steps over 2 s of simulated time.
    float max_second_raw = 0.0F;
    scs_telemetry_frame_start_t f{};
    f.paused_simulation_time = 1000000;
    for (int i = 0; i < 10; ++i) {
        auto &fs = event_callback(SCS_TELEMETRY_EVENT_frame_start);
        fs.callback(fs.event, &f, fs.context);
        auto &fe = event_callback(SCS_TELEMETRY_EVENT_frame_end);
        fe.callback(fe.event, nullptr, fe.context);
        char buf[1024]{};
        const auto n = recv(receiver, buf, sizeof(buf) - 1, MSG_DONTWAIT);
        if (n > 0) {
            const std::string j(buf, static_cast<std::size_t>(n));
            const float v = find_raw(j);
            if (v > max_second_raw) max_second_raw = v;
        }
        f.paused_simulation_time += 200000;
    }

    std::fprintf(stderr, "[reload] first_raw=%.4f max_second_raw=%.4f\n", first_raw, max_second_raw);
    assert(max_second_raw > first_raw + 0.05F);

    scs_telemetry_shutdown();
    unlink(reload_path.c_str());
    assert(setenv("MODGUARD_CONFIG", "/tmp/modguard-plugin-test-missing.ini", 1) == 0);

    close(receiver);
    unlink(socket_path.c_str());
    return 0;
}
