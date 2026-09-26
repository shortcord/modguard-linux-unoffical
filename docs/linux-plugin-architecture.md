# Linux Plugin Architecture

This document describes the native Linux SCS Telemetry plugin. It tells how the plugin loads and how it connects to the game. It also tells how data flows through it, how it is built and tested, and how diagnostics are published. It is the implementation reference for `modguard-linux/`. It is independent of the reverse-engineering research in `01-reverse-engineering.md` through `05-linux-reimplementation.md`.

The source of truth for current Linux behavior is the source code under `modguard-linux/src/` and the tests under `modguard-linux/tests/`. Use `../reverse-enginnering/MODGuard_RE.md` only for the analyzed Windows plugin.

---

## 1. Layout

```
modguard-linux/
├── CMakeLists.txt          Build definition
├── README.md               Plugin user guide
├── LICENSE-SCS-SDK         MIT license for the vendored SCS SDK subset
├── modguard_linux.ini.example   Commented configuration template
├── include/
│   └── scs_sdk_minimal.hpp Vendored SCS Telemetry SDK 1.14 ABI subset
├── src/
│   ├── plugin.cpp          Exported C ABI, events, channels, diagnostics
│   ├── config.{hpp,cpp}    INI configuration loader
│   ├── config_runtime.{hpp,cpp}  Single-slot pending-config handoff
│   ├── config_watcher.{hpp,cpp}  inotify watcher for the ini file
│   ├── exhaust_model.{hpp,cpp}   exhaust_demand_v0 model
│   ├── game_detector.{hpp,cpp}   ELF build-ID and profile detection
│   ├── game_profiles.{hpp,cpp}   Supported-game profile table
│   ├── particle_hook.{hpp,cpp}   Inline game-memory hook
│   ├── particle_signature.{hpp,cpp}  Smoke-layer classifiers
│   └── particle_admission.{hpp,cpp}  Synchronized admission sampler
├── tests/                  Standalone CTest executables
└── tools/
    ├── listen.py           Diagnostic Unix datagram listener
    └── modguard_check.cpp  Standalone binary-support checker
```

Five static libraries are built from the source and linked into the shared object:

- `exhaust_model` — `exhaust_model.cpp`, `config.cpp`.
- `config_runtime` — `config_runtime.cpp`. It links `exhaust_model`.
- `config_watcher` — `config_watcher.cpp`. It links `config_runtime` and `Threads`.
- `game_detector` — `game_detector.cpp`, `game_profiles.cpp`.
- `particle_hook` — `particle_hook.cpp`, `particle_signature.cpp`, `particle_admission.cpp`. It links `dl` and `game_detector`.

The shared library `modguard_linux.so` is built from `plugin.cpp` and links the static libraries. See `modguard-linux/CMakeLists.txt:8-45`.

---

## 2. Vendored SCS SDK ABI

`include/scs_sdk_minimal.hpp` vendors only the ABI subset of SCS Telemetry SDK 1.14 that the plugin uses. It is not the full upstream SDK. The MIT license for these definitions stays in `LICENSE-SCS-SDK`.

The header defines:

- Fixed-width type aliases (`scs_u32_t`, `scs_s32_t`, `scs_float_t`, `scs_string_t`, `scs_timestamp_t`, `scs_result_t`, etc.).
- Result, log-type, event, value-type, channel-flag, and version constants.
- Value and event structs (`scs_value_t`, `scs_named_value_t`, `scs_telemetry_frame_start_t`, `scs_telemetry_configuration_t`).
- Function-pointer typedefs for the SDK callbacks.
- The init params structs `scs_telemetry_init_params_v100_t` and the v101 alias.
- Game and channel name macros (`SCS_GAME_ID_ATS`, `SCS_GAME_ID_EUT2`, `SCS_TELEMETRY_TRUCK_CHANNEL_*`, `SCS_TELEMETRY_CONFIG_*`).
- `MODGUARD_EXPORT` for `scs_telemetry_init` and `scs_telemetry_shutdown`.

The code checks ABI-critical sizes at compile time: `sizeof(scs_value_t) == 48`, `sizeof(scs_named_value_t) == 64`, `sizeof(scs_sdk_init_params_v100_t) == 32`, `sizeof(scs_telemetry_init_params_v100_t) == 64`, `sizeof(scs_telemetry_frame_start_t) == 32` (`scs_sdk_minimal.hpp:131-135`).

The plugin supports only telemetry SDK versions 1.00 and 1.01 (`plugin.cpp:330-332`).

---

## 3. Plugin lifecycle

The exported C ABI consists of two functions in `plugin.cpp`:

- `scs_telemetry_init(scs_u32_t version, const scs_telemetry_init_params_t *params_base)` (`plugin.cpp:327`)
- `scs_telemetry_shutdown()` (`plugin.cpp:410`)

### 3.1 Init flow

`scs_telemetry_init` performs these steps in order:

1. **Version guard.** Accepts only `SCS_TELEMETRY_VERSION_1_00` and `SCS_TELEMETRY_VERSION_1_01`. Otherwise returns `SCS_RESULT_unsupported` (`plugin.cpp:330-332`).
2. **Null-params guard.** Returns `SCS_RESULT_generic_error` if `params_base` is null (`plugin.cpp:333-335`).
3. **Game guard.** Accepts only `SCS_GAME_ID_ATS` (`"ats"`) and `SCS_GAME_ID_EUT2` (`"eut2"`). Otherwise logs an error and returns `SCS_RESULT_unsupported` (`plugin.cpp:342-348`).
4. **State reset.** Resets `inputs`, loads configuration via `modguard::load_config(game_id)`, constructs the `modguard::ExhaustModel` from the loaded exhaust config, and zeroes timing, sequence, and the `paused` flag (`plugin.cpp:350-357`).
5. **Event registration.** Registers five event callbacks (`plugin.cpp:82-88`, `plugin.cpp:359-364`):
   - `SCS_TELEMETRY_EVENT_frame_start` -> `on_frame_start`
   - `SCS_TELEMETRY_EVENT_frame_end` -> `on_frame_end`
   - `SCS_TELEMETRY_EVENT_paused` -> `on_pause`
   - `SCS_TELEMETRY_EVENT_started` -> `on_pause` (treated as unpause)
   - `SCS_TELEMETRY_EVENT_configuration` -> `on_configuration`
6. **Channel registration.** Registers seven truck channels with `SCS_TELEMETRY_CHANNEL_FLAG_each_frame | SCS_TELEMETRY_CHANNEL_FLAG_no_value` (`plugin.cpp:286-306`, `plugin.cpp:365-371`):

   | Channel constant | SDK name | Type |
   | --- | --- | --- |
   | `speed` | `truck.speed` | `float` |
   | `rpm` | `truck.engine.rpm` | `float` |
   | `throttle` | `truck.effective.throttle` | `float` |
   | `gear` | `truck.displayed.gear` | `s32` |
   | `clutch` | `truck.effective.clutch` | `float` |
   | `engine_enabled` | `truck.engine.enabled` | `bool` |
   | `acceleration` | `truck.local.acceleration.linear` | `fvector` |

   If any event or channel registration fails, all registrations are rolled back via `unregister_callbacks()` and init returns `SCS_RESULT_generic_error` (`plugin.cpp:365-377`).
7. **Diagnostic socket.** `open_output()` creates a non-blocking `SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC` Unix socket (`plugin.cpp:92-115`).
8. **Configuration logging.** Logs the loaded config path or a warning that defaults are in use (`plugin.cpp:380-385`).
9. **Particle hook.** If `smoke_hook` is disabled by `SmokeHook=0`, logs a warning and skips installation. Otherwise calls `modguard::install_particle_hook(game_id, game_version)` and logs success or a fingerprint-mismatch warning (`plugin.cpp:379-396`). See `linux-particle-hook.md` for the hook contract.
10. **Config watcher.** Starts a `modguard::ConfigWatcher` on the loaded config path (`plugin.cpp:399-405`). The watcher runs in its own thread and publishes reloaded config to the plugin thread. See section 6.2.

### 3.2 Shutdown flow

`scs_telemetry_shutdown` (`plugin.cpp:380`):

1. Zeroes particle demand and clears eligibility (`set_particle_demand(0, false)`).
2. Unregisters all channels and events via `unregister_callbacks()`.
3. Removes the particle hook (`modguard::remove_particle_hook()`), which restores original bytes and unmaps trampolines.
4. Stops the config watcher (`config_watcher.reset()`). The destructor joins the watcher thread before the other resources close (`plugin.cpp:410-421`). See section 6.2.
5. Closes the diagnostic socket.
6. Logs shutdown and clears the saved SDK function pointers and log callback.

`unregister_callbacks()` (`plugin.cpp:308-323`) walks the registration arrays in reverse and calls `unregister_from_channel` / `unregister_from_event` for each entry. The counters `registered_channel_count` and `registered_event_count` drive the loops. Thus shutdown never removes a callback that failed to register.

---

## 4. Event callbacks

All event callbacks live in the anonymous namespace of `plugin.cpp`.

### 4.1 `on_frame_start`

`on_frame_start` (`plugin.cpp:156`) reads `scs_telemetry_frame_start_t`:

- `current_time` is set from `paused_simulation_time`.
- If the `timer_restart` flag is set, or `previous_time` is zero, or time went backwards, `delta_seconds` is zeroed and `model.reset()` is called. This removes stale smoothing state after a save load or a pause.
- Otherwise `delta_seconds = (current_time - previous_time) / 1e6`.
- `modguard::particle_hook_frame_start()` advances the per-frame admission sampler if the hook is active.

### 4.2 `on_frame_end`

`on_frame_end` (`plugin.cpp:174`) runs only when not paused:

1. If the config watcher published a new config, `pending_config.take()` returns it and `model.apply_config(*next)` replaces the model config (`plugin.cpp:174-186`). See section 6.2.
2. `model.update(inputs, delta_seconds)` produces an `ExhaustOutput`.
3. `modguard::set_particle_demand(output.demand, inputs.engine_enabled)` publishes the demand to the hook atomics.
4. `publish(output)` sends a diagnostic datagram if the throttle interval elapsed.

### 4.3 `on_pause`

`on_pause` (`plugin.cpp:188`) handles both `paused` and `started` events. On pause, it resets the model and zeroes particle demand so smoke collapses cleanly.

### 4.4 `on_configuration`

`on_configuration` (`plugin.cpp:243`) reads two configuration IDs:

- `truck`: reads `rpm.limit` (clamped to `[100, 10000]`). If absent, the default `2500.0F` stays.
- `job`: reads `cargo.mass` (clamped to `>= 0`) and `cargo.loaded` (bool). If absent, cargo mass is zeroed and cargo is marked unloaded.

The handler walks `configuration->attributes` until a null `name` terminator. Only the two known config IDs are inspected. All others are ignored.

### 4.5 `on_channel`

`on_channel` (`plugin.cpp:197`) dispatches on the `Channel` enum stored in the registration context. When the value pointer is null, the input field is reset to a safe zero. When present, the value is accepted only if its `type` matches the expected type. The acceleration channel computes the Euclidean magnitude of the `fvector` and stores it as a scalar.

---

## 5. Diagnostic output

### 5.1 Socket

`open_output()` (`plugin.cpp:92`) binds a non-blocking Unix datagram socket. The destination path is:

- `MODGUARD_EXHAUST_SOCKET` if set and absolute, otherwise
- `/tmp/modguard-exhaust-<uid>.sock`.

If the path is too long for `sun_path`, output is disabled with a warning. The socket uses `SOCK_NONBLOCK | SOCK_CLOEXEC` so it can never block the frame thread. It is not inherited by child processes.

### 5.2 Datagram schema

`publish()` (`plugin.cpp:117`) sends at most one datagram per 100 ms of simulation time. The JSON shape is:

```json
{"schema":"exhaust_demand_v0","seq":42,"time_us":123456,
 "demand":0.83,"raw_demand":1.25,"session_peak":1.25,
 "throttle":0.70,"rpm":1450.0,"rpm_limit":2100.0,
 "speed":18.4,"gear":8,"engine":true,"cargo_loaded":true,
 "cargo_mass":18500.0,"hook_active":true,
 "particle_constructors":12,"particle_candidates":720,
 "particle_registered":640,"smoke_requests":600,
 "admitted_requests":531,"suppressed_requests":69}
```

Field notes:

- `seq` is a monotonically increasing `uint64`.
- `time_us` is the SDK `paused_simulation_time` in microseconds.
- `demand` is clamped to `[0,1]`. `raw_demand` is the unclamped model output kept for diagnostics.
- All floats pass through `std::isfinite` so the JSON never contains `nan` or `inf`.
- `hook_active` reflects `modguard::particle_hook_active()`.
- `particle_constructors` counts calls to the hooked generator constructor.
- `particle_candidates` counts calls that have the profile node size and a valid candidate pointer.
- `particle_registered` counts candidates that match the constructor registry.
- `smoke_requests` counts registered candidates that match a smoke layer signature.
- `admitted_requests` counts smoke requests that reach the original game function.
- `suppressed_requests` counts smoke requests that the demand gate rejects.

The particle counters are cumulative for the current hook installation. The hook callbacks update them with relaxed atomic operations.

The constructor registry contains all particle generators. Thus, a nonzero `particle_registered` value does not prove that an exhaust generator registered. The counter sequence narrows the failed stage, but it does not identify a generator by itself.

The datagram is sent with `MSG_DONTWAIT`. Loss is expected and never affects particle control or the game process. If the JSON exceeds the 1024-byte message buffer, it is dropped silently.

### 5.3 Listener

`tools/listen.py` binds a Unix datagram socket to the same path and prints each datagram. It removes any stale socket file before binding and on exit. It requires `MODGUARD_EXHAUST_SOCKET` to be absolute if set.

> If the JSON schema changes, update `modguard-linux/README.md`, `tools/listen.py`, and `tests/plugin_test.cpp` in the same change (see `AGENTS.md`).

---

## 6. Configuration loader and reload

This section describes how the plugin reads the configuration at startup and how it applies a change while the game runs.

### 6.1 Load

`config.{hpp,cpp}` parse the `[RealisticSmoke]` INI schema. See `linux-exhaust-model.md` for the full key table, defaults, and sanitize ranges. The loader contract is:

- `load_config(game_id)` (`config.cpp:241`):
  - If `MODGUARD_CONFIG` is set and non-empty, loads that path.
  - Otherwise loads `~/.local/share/<game>/mod/modguard_linux.ini` where `<game>` is `American Truck Simulator` for `ats` and `Euro Truck Simulator 2` for `eut2`.
  - If `HOME` is unset or empty, returns defaults with `found=false`.
- `load_config_file(path)` (`config.cpp:202`):
  - Returns `found=false` if the file cannot be opened.
  - Skips blank lines and lines that start with `;` or `#`.
  - Only keys inside a `[RealisticSmoke]` section are read. Other sections are ignored.
  - Inline comments after a value (`;` or `#`) are stripped.
  - Keys are case-sensitive. Unknown keys are ignored.
  - Parsers: `parse_bool` accepts `1/true/yes/on` and `0/false/no/off` (case-insensitive). `parse_float` rejects non-finite and trailing garbage. `parse_int` rejects range overflow.
  - After parsing, `sanitize()` clamps every numeric field to its safe range.

The config path that `load_config` returns is the path that the watcher uses.

### 6.2 Reload

The plugin can apply a configuration change while the game runs. You do not restart the game. Edit `modguard_linux.ini` and save it. The plugin applies the new values on the next simulation frame.

Two classes own the reload:

- `ConfigSlot` (`config_runtime.{hpp,cpp}`) is the handoff between the watcher thread and the plugin thread. It holds one pending `ExhaustConfig` pointer.
- `ConfigWatcher` (`config_watcher.{hpp,cpp}`) watches the ini file and publishes a new `ExhaustConfig` when the file changes.

#### 6.2.1 Trigger events

`ConfigWatcher` uses `inotify`. At startup, `inotify` cannot watch a path that does not exist. Thus the watcher first polls for the file to appear, then adds the watch. The watch mask is `IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF | IN_ATTRIB` (`config_watcher.cpp:24`). These events cover the usual save methods:

- `IN_CLOSE_WRITE` — an editor writes and closes the file.
- `IN_MOVE_SELF` and `IN_DELETE_SELF` — an atomic save renames a new file over the old one.
- `IN_ATTRIB` — a metadata change, such as `touch`.

For an atomic save, the file can briefly be a different inode or absent. The watcher then drops the watch, waits for the file to reappear, and adds the watch again. The watcher then reloads the config.

#### 6.2.2 Reload path

On a trigger, the watcher thread parses the file with `load_config_file`. It publishes a heap-allocated `ExhaustConfig` into the `ConfigSlot`. The slot holds one config at a time. Thus a burst of edits coalesces to the latest config.

The plugin thread takes the pending config at the top of `on_frame_end` and calls `model.apply_config`. `apply_config` replaces the config values only. It preserves the smoothing state, so a reload does not make a visible glitch (`exhaust_model.cpp:31-35`).

#### 6.2.3 Failure behavior

- If the file is missing, the watcher logs a warning and keeps the previous config.
- If the file cannot be opened, `load_config_file` returns `found=false` and the watcher keeps the previous config.
- If a value is invalid, that key falls back to its default. The watcher still publishes the rest of the file. This is the tested semantic (see `config_watcher_test.cpp`).
- Sanitize clamping always runs, so a reload can never apply an out-of-range value.

The watcher stops when `scs_telemetry_shutdown` resets it. The destructor joins the watcher thread, so a reload can never race shutdown.

---

## 7. Build system

`CMakeLists.txt` requires CMake 3.16 and a C++17 compiler with no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`). These targets are defined:

| Target | Type | Sources | Links |
| --- | --- | --- | --- |
| `exhaust_model` | static | `exhaust_model.cpp`, `config.cpp` | — |
| `config_runtime` | static | `config_runtime.cpp` | `exhaust_model` |
| `config_watcher` | static | `config_watcher.cpp` | `config_runtime`, `Threads` |
| `particle_hook` | static | `particle_hook.cpp`, `particle_signature.cpp`, `particle_admission.cpp` | `dl`, `game_detector` |
| `game_detector` | static | `game_detector.cpp`, `game_profiles.cpp` | — |
| `modguard_linux` | shared | `plugin.cpp` | `exhaust_model`, `config_runtime`, `config_watcher`, `particle_hook`, `Threads` |

The static libraries are built with `POSITION_INDEPENDENT_CODE ON` and `CXX_VISIBILITY_PRESET hidden`. Thus only the two exported `extern "C"` symbols are visible from the shared object. All targets compile with `-Wall -Wextra -Wpedantic`. The build also creates `modguard_check` from `tools/modguard_check.cpp`, which links `game_detector`.

Build and install:

```bash
cmake -S modguard-linux -B modguard-linux/build -DCMAKE_BUILD_TYPE=Release
cmake --build modguard-linux/build --parallel
ctest --test-dir modguard-linux/build --output-on-failure
```

The output is `modguard-linux/build/modguard_linux.so`. The install rule places it in `<prefix>/plugins/`. Do not commit build output.

---

## 8. Test suite

Tests are standalone executables that use `assert` and are registered with CTest. They must not require an installed game, network access, or proprietary binaries. Particle-signature tests use synthetic memory layouts.

| Test | Source | Covers |
| --- | --- | --- |
| `exhaust_model_test` | `tests/exhaust_model_test.cpp` | Engine-off zero, smoothing, shift transient, NaN inputs, launch, coast decay, `SmokeHook=0`, continuous smoke, heavy-vs-light load, weighted terms, free-rev gate |
| `config_test` | `tests/config_test.cpp` | Section filtering, bool variants, inline comments, sanitize clamping, invalid values, missing file |
| `game_detector_test` | `tests/game_detector_test.cpp` | ELF build-ID detection, profile selection, code-byte checks, unsupported and corrupt binaries |
| `particle_signature_test` | `tests/particle_signature_test.cpp` | ETS2 1.55/1.60/1.61 and ATS 1.55/1.60/1.61/1.61.2 classification, vtable mismatches, non-smoke layers, profile selection, build-ID mismatches |
| `particle_admission_test` | `tests/particle_admission_test.cpp` | Cross-sampler determinism, per-layer independence, per-call independence |
| `lifetime_override_test` | `tests/lifetime_override_test.cpp` | Lifetime-multiplier scaling and idempotence |
| `hook_patch_test` | `tests/hook_patch_test.cpp` | Byte check before patch, fail-closed on mismatch, restore failure rollback |
| `plugin_test` | `tests/plugin_test.cpp` | Full init/shutdown cycle, event and channel registration counts, configuration callback, datagram JSON shape, NaN exclusion, unsupported game/version rejection |
| `config_watcher_test` | `tests/config_watcher_test.cpp` | `ConfigSlot` coalescing and ownership, file-appears-later reload, live edit reload, bad-value fallback, prompt `stop` |

`plugin_test` compiles `plugin.cpp` directly and drives it with fake SDK registration functions. It binds a real Unix datagram socket to receive the published JSON and asserts on its contents. Thus it checks the diagnostic schema end-to-end.

Run the complete CTest suite after any change to C++ code or CMake files. Add or update a focused test when behavior changes (per `AGENTS.md`).

---

## 9. Related documents

- `linux-exhaust-model.md` — the demand model and the full configuration table.
- `linux-particle-hook.md` — the inline game-memory hook and admission sampler.
- `mod-smoke-variants.md` — the clean smoke-mod presets and packaging.
- `../reverse-enginnering/MODGuard_RE.md` — the analyzed Windows plugin (research reference only).
