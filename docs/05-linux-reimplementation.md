# Linux Reimplementation — Porting Guide

This document gives a concrete plan to reimplement MODGuard on Linux (native or Proton). It comes from the static analysis of `MODGuard.dll` and the game binaries.

---

## 1. Goals & Constraints

### 1.1 Goals

1. A Linux-native MODGuard replacement works with **ATS** and **ETS2**. It runs under **Proton** or natively.
2. The implementation preserves the original RealisticSmoke formula exactly. There is no behavioural regression.
3. The implementation avoids anti-cheat and DRM detection. It uses only public APIs.
4. The implementation stays small and auditable.

### 1.2 Constraints

- **No MinHook on Linux**: `MinHook` (x86-64 inline hotpatching) is Windows-only. Proton runs the Windows binary via Wine, so MinHook works *inside* the Windows process. But a separate Linux-side mod cannot modify the Wine process memory.
- **No WinHTTP**: The Linux replacement must use `libcurl` or POSIX sockets.
- **No `IPHLPAPI`**: The Linux replacement must use `getifaddrs()` for MAC addresses.
- **No Authenticode**: Linux binaries do not use Microsoft code signing.
- **No `mscoree.dll`**: The .NET runtime is not loaded despite the import being declared.

---

## 2. Implementation Architecture

### 2.1 Where the Linux code runs

The Linux replacement can run **in two places**:

#### Option A: As a SCS Telemetry plugin `.so` (recommended)

Both ETS2 and ATS ship a native Linux build of the SCS Telemetry SDK. The plugin is loaded
from `<game>/bin/linux_x64/plugins/`.

```
~/.steam/steam/steamapps/common/American Truck Simulator/bin/linux_x64/plugins/
    modguard_linux.so     <- our replacement
```

This option:
- ✅ Uses the official Linux SDK ABI
- ✅ No DLL injection needed
- ❌ Cannot modify particle behaviour in-game (no MinHook equivalent)
- ✅ Can modify the `.sii` configuration files at game start
- ✅ Can serve as a "telemetry forwarder" to a Linux-side overlay

#### Option B: As a `LD_PRELOAD` library

A library loaded via `LD_PRELOAD=...` into the native Linux game process can intercept
function calls.

```
LD_PRELOAD=./libmodguard.so ./eurotrucks2
```

This option:
- ✅ Can hook into the Linux game libc calls (for example, `CreateFileW` from Wine)
- ❌ More fragile, depends on Wine internals
- ❌ Will not work in Proton (the game runs as a Windows binary under Wine)

#### Recommended: Hybrid

- **Primary**: Native SCS Telemetry plugin (Option A) for clean compatibility.
- **Optional**: Companion `LD_PRELOAD` shim for users who want advanced features.

---

## 3. Component Breakdown

### 3.1 Source files

```
modguard_linux/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── plugin.c            # scs_telemetry_init / scs_telemetry_shutdown
│   ├── telemetry.c         # Read telemetry values, smooth inputs
│   ├── formula.c           # RealisticSmoke math (per-input envelopes)
│   ├── config.c            # .ini reader/writer (UTF-8 instead of UTF-16LE)
│   ├── config_keys.h       # All config keys as constants
│   ├── hwid.c              # MAC + hardware fingerprint via getifaddrs()
│   ├── license.c          # libcurl-based license check
│   ├── log.c               # Game log wrapper
│   └── util.c              # String helpers, INI parser
└── docs/
    └── (this spec)
```

### 3.2 Detailed file responsibilities

#### `plugin.c`

```c
#include "scs-telemetry-common.h"

scs_u32_t g_registered_events = 0;
volatile int g_shutdown = 0;

scs_bool_t scs_telemetry_init(const scs_u32_t version,
                              const scs_telemetry_init_params_v100_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00) return SCS_RESULT_unsupported;
    
    // Save SDK callbacks
    g_register_for_event     = params->register_for_event;
    g_unregister_from_event  = params->unregister_from_event;
    g_register_for_channel   = params->register_for_channel;
    g_unregister_from_channel= params->unregister_from_channel;
    
    // Detect game (eurotrucks2 vs amtrucks)
    const char* game_id = params->common.game_id;
    if (strcmp(game_id, "eut2") == 0) g_game = GAME_ETS2;
    else if (strcmp(game_id, "ats") == 0) g_game = GAME_ATS;
    else return SCS_RESULT_unsupported;
    
    // Load config
    config_load();
    
    // Init telemetry smoothing state
    telemetry_init();
    
    // Start license worker
    license_worker_start();
    
    // Install smoke hook (if smoke_new.sii exists)
    if (smoke_mod_installed()) {
        smoke_hook_install();
    }
    
    // Register event callbacks (5 events)
    g_register_for_event(SCS_TELEMETRY_EVENT_FRAME_START, on_frame_start, NULL);
    g_register_for_event(SCS_TELEMETRY_EVENT_FRAME_END,   on_frame_end,   NULL);
    g_register_for_event(SCS_TELEMETRY_EVENT_PAUSED,      on_paused,      NULL);
    g_register_for_event(SCS_TELEMETRY_EVENT_STARTED,     on_started,     NULL);
    g_register_for_event(SCS_TELEMETRY_EVENT_CONFIGURATION, on_configuration, NULL);
    g_registered_events = 0x1f;
    
    return SCS_RESULT_OK;
}

void scs_telemetry_shutdown(void) {
    g_shutdown = 1;
    if (g_registered_events & 0x01) g_unregister_from_event(SCS_TELEMETRY_EVENT_FRAME_START, on_frame_start);
    // ... etc for all events
    
    config_save();
    license_worker_stop();
}
```

#### `telemetry.c`

```c
#include <math.h>

// Smoothing state
static float g_throttle_smoothed = 0.0f;
static float g_rpm_smoothed = 0.0f;
static float g_speed_smoothed = 0.0f;
static float g_accel_smoothed = 0.0f;
static int   g_last_gear = 0;
static float g_gear_transient_timer = 0.0f;
static float g_launch_timer = 0.0f;
static float g_free_rev_timer = 0.0f;
static int   g_was_idle = 0;  // for launch detection
static uint16_t g_poll_counter = 0;

void on_frame_start(const scs_event_t event, const void* event_info, void* ctx) {
    // Smooth telemetry values
    float dt = 1.0f / 60.0f;  // estimate; SDK doesn't expose dt here
    float throttle = get_next_float_buffer(SCS_TELEMETRY_TRUCK_CHANNEL_throttle);
    float rpm = get_next_float_buffer(SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm);
    float rpm_limit = get_next_float_buffer(SCS_TELEMETRY_TRUCK_CHANNEL_rpm_limit);
    // ... etc
    
    // Attack/release smoothing per config
    float attack_tau = cfg.attack_smoothness;
    float release_tau = cfg.release_smoothness;
    float alpha_attack = 1.0f - expf(-dt / attack_tau);
    float alpha_release = 1.0f - expf(-dt / release_tau);
    float alpha = (throttle > g_throttle_smoothed) ? alpha_attack : alpha_release;
    g_throttle_smoothed += alpha * (throttle - g_throttle_smoothed);
    
    // ... similar for rpm, speed, accel ...
    
    // Gear shift transient
    int gear = (int) get_next_float_buffer(...);
    if (gear != g_last_gear) {
        g_gear_transient_timer = 0.3f;  // 300ms decay
        g_last_gear = gear;
    }
    if (g_gear_transient_timer > 0) g_gear_transient_timer -= dt;
    
    // Launch detection
    float speed = get_next_float_buffer(SCS_TELEMETRY_TRUCK_CHANNEL_speed);
    if (g_was_idle && speed < 1.0f && throttle > 0.5f) {
        g_launch_timer = 3.0f;
    }
    g_was_idle = (speed < 0.5f);
    if (g_launch_timer > 0) g_launch_timer -= dt;
    
    // Free-rev detection
    bool clutch_pressed = get_next_float_buffer(...);
    bool engine_on = get_next_float_buffer(SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled);
    if (engine_on && !clutch_pressed && throttle < 0.05f && gear == 0) {
        g_free_rev_timer += dt;
    } else {
        g_free_rev_timer = 0;
    }
}

void on_frame_end(const scs_event_t event, const void* event_info, void* ctx) {
    // Update smoke formula
    float scale = formula_compute(g_throttle_smoothed, g_rpm_smoothed, g_speed_smoothed,
                                   g_accel_smoothed, g_gear_transient_timer, g_launch_timer,
                                   g_free_rev_timer);
    
    // Apply scaling to particles
    // (Linux: only possible if we can modify shared memory; otherwise just log)
    smoke_apply_scale(scale);
    
    // License refresh poll (every 300 frames)
    g_poll_counter++;
    if (g_poll_counter >= 300) {
        g_poll_counter = 0;
        license_poll_worker();
    }
}
```

#### `formula.c`

Direct port of the RealisticSmoke formula (see `01-reverse-engineering.md` §6 for the exact
algorithm):

```c
float formula_compute(float throttle, float rpm, float speed, float accel,
                      float gear_transient, float launch_env, float free_rev_env) {
    // Throttle curve
    float th = powf(throttle, cfg.throttle_curve);  // if pow; else throttle * cfg.throttle_curve
    
    float input = 0.0f;
    input += th                                                       * cfg.throttle_weight;
    input += (rpm / cfg.rpm_limit)                                    * cfg.rpm_weight;
    input += (speed / 30.0f)                                          * cfg.speed_weight;
    input += accel                                                    * cfg.acceleration_weight;
    input += gear_transient                                           * cfg.shift_weight;
    input += get_clutch_norm()                                        * cfg.clutch_weight;
    input += get_cargo_mass_norm() * get_cargo_loaded()               * cfg.cargo_weight;
    input += launch_env                                               * cfg.launch_weight;
    input += get_traction_accel()                                     * cfg.traction_acceleration_weight;
    input += get_load_effort()                                        * cfg.load_effort_weight;
    input += free_rev_env                                            * cfg.free_rev_weight;
    
    float steady_throttle = (steady_throttle > cfg.sustained_throttle_strength) 
                            ? steady_throttle : 0.0f;
    input += steady_throttle * cfg.event_strength;
    
    // Session peak tracking
    if (input > g_session_peak) g_session_peak = input;
    g_session_peak *= cfg.session_peak_percent;  // decay
    
    // Final output
    float output = input * cfg.intensity * cfg.final_smoke_multiplier;
    
    // Bucket assignment (100 / 2500 / 10000)
    if (output < 100.0f)        output = 100.0f;
    else if (output < 2500.0f)  output = 2500.0f;
    else                        output = 10000.0f;
    
    return output;
}
```

#### `license.c` (libcurl-based)

```c
#include <curl/curl.h>

static char* get_mac_address() {
    struct ifaddrs* ifap;
    getifaddrs(&ifap);
    char* mac = malloc(32);
    for (struct ifaddrs* i = ifap; i; i = i->ifa_next) {
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_PACKET) {
            unsigned char* m = (unsigned char*)i->ifa_addr;
            // Skip loopback
            if (!(m[0] == 0 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 0 && m[5] == 0)) {
                snprintf(mac, 32, "%02x%02x%02x%02x%02x%02x",
                         m[0], m[1], m[2], m[3], m[4], m[5]);
                break;
            }
        }
    }
    freeifaddrs(ifap);
    return mac;
}

static char* get_steam_id() {
    // Try Steam registry / config file
    // Linux: ~/.steam/steam/config/loginusers.vdf
    // Or read SteamUserID cookie
    char path[256];
    snprintf(path, sizeof(path), "%s/.steam/steam/config/loginusers.vdf", getenv("HOME"));
    // Parse VDF format and extract SteamID64
    // ...
    return strdup("0");  // fallback
}

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    // accumulate response
    return size * nmemb;
}

int license_validate() {
    CURL* curl = curl_easy_init();
    char* mac = get_mac_address();
    char* steam_id = get_steam_id();
    char url[512];
    snprintf(url, sizeof(url),
             "https://leakingsv2.modguard.com.br/key?key=%s&steamID=%s&clientID=%s",
             cfg.license_key, steam_id, mac);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    free(mac);
    free(steam_id);
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK && http_code == 200);
}
```

#### `config.c` (UTF-8 INI parser)

```c
// Use inih or similar
#include "ini.h"

typedef struct {
    int smoke_hook;
    int continuous_smoke;
    float final_smoke_multiplier;
    float intensity;
    // ... all 24 fields ...
} realistic_smoke_config_t;

extern realistic_smoke_config_t cfg;

static int handler(void* user, const char* section, const char* name, const char* value) {
    if (strcmp(section, "RealisticSmoke") == 0) {
        if (strcmp(name, "SmokeHook") == 0) cfg.smoke_hook = atoi(value);
        else if (strcmp(name, "ContinuousSmoke") == 0) cfg.continuous_smoke = atoi(value);
        else if (strcmp(name, "FinalSmokeMultiplier") == 0) cfg.final_smoke_multiplier = atof(value);
        // ... etc ...
    }
    return 1;
}

void config_load() {
    char path[256];
    snprintf(path, sizeof(path),
             "%s/%s/mod/modguard.ini",
             getenv("HOME"),
             (g_game == GAME_ETS2) ? ".local/share/Euro Truck Simulator 2"
                                   : ".local/share/American Truck Simulator");
    
    if (ini_parse(path, handler, NULL) < 0) {
        // Use defaults
        defaults_apply();
    }
}
```

---

## 4. Differences vs Windows

| Aspect | Windows MODGuard | Linux replacement |
|--------|------------------|--------------------|
| **Particle hook** | MinHook inline hook | None (read-only) |
| **License transport** | WinHTTP | libcurl |
| **MAC fingerprint** | `IPHLPAPI::GetAdaptersInfo` | `getifaddrs()` |
| **Config files** | UTF-16LE (Wide API) | UTF-8 |
| **Threading** | Win32 CreateThread | pthreads |
| **TLS callbacks** | Yes | Not needed |
| **Code signing** | Authenticode | None |
| **SDK** | SCS SDK v1.00 (Windows) | SCS SDK v1.00 (Linux) — same ABI |
| **License refresh** | Every 300 frames | Same (via SDK frame events) |

---

## 5. Build System (CMake)

```cmake
cmake_minimum_required(VERSION 3.16)
project(modguard_linux C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find SCS Telemetry SDK
find_path(SCS_SDK_INCLUDE_DIR scs-telemetry-common.h
          HINTS "${CMAKE_SOURCE_DIR}/include")

# Find libcurl (for license check)
find_package(CURL REQUIRED)

# Find pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(modguard_linux SHARED
    src/plugin.c
    src/telemetry.c
    src/formula.c
    src/config.c
    src/hwid.c
    src/license.c
    src/log.c
    src/util.c
)

target_include_directories(modguard_linux PRIVATE
    src
    ${SCS_SDK_INCLUDE_DIR}
)

target_link_libraries(modguard_linux PRIVATE
    CURL::libcurl
    Threads::Threads
    m  # math
)

target_compile_options(modguard_linux PRIVATE -Wall -Wextra -O2)

# Install to user's plugins folder
install(TARGETS modguard_linux
        LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/plugins")
```

---

## 6. Installation (User Steps)

### 6.1 Native Linux install

```bash
# 1. Install the smoke mod (if not already installed)
cp -r PM_389_Smoke ~/.local/share/American\ Truck\ Simulator/mod/

# 2. Install the plugin
cp modguard_linux.so ~/.local/share/American\ Truck\ Simulator/bin/linux_x64/plugins/

# 3. Create config (optional)
cat > ~/.local/share/American\ Truck\ Simulator/mod/modguard.ini << 'EOF'
[RealisticSmoke]
SmokeHook=1
Intensity=0.5
ThrottleWeight=1.0
EOF

# 4. Launch the game
steam steam://run/270880  # ATS Steam app ID
```

### 6.2 Proton install

```bash
# 1. Install the smoke mod (Windows version) into the Proton prefix
cp -r PM_389_Smoke ~/.steam/steam/steamapps/compatdata/270880/pfx/drive_c/users/steamuser/Documents/American\ Truck\ Simulator/mod/

# 2. Install the Windows MODGuard.dll (the original works in Proton!)
cp MODGuard.dll ~/.steam/steam/steamapps/compatdata/270880/pfx/drive_c/users/steamuser/Documents/American\ Truck\ Simulator/bin/win_x64/plugins/

# 3. Launch via Proton
steam steam://run/270880
```

> **Important**: When running under Proton/Wine, the **original Windows MODGuard.dll works**.
> The Linux replacement is only needed for *native* Linux game runs.

---

## 7. Testing Checklist

After you build the plugin, check the following:

- [ ] Plugin loads at game start (check log for "MODGuard Linux loaded")
- [ ] Telemetry values are read (check `g_throttle_smoothed` updates)
- [ ] The configuration is parsed correctly (modify the values, restart the game, and check the behavior)
- [ ] License check returns 200 (use a real or fake key)
- [ ] Smoke formula computes reasonable values (log to file)
- [ ] Worker thread exits cleanly on shutdown
- [ ] No crashes on game exit
- [ ] Works with both ATS and ETS2
- [ ] Steam-ID is read correctly from `loginusers.vdf`

---

## 8. Open Implementation Issues

1. **Particle hook in Linux**: Without MinHook, you cannot modify live particle sizes.
   Options:
   - **Option A**: Do not try. Only compute the formula and log the values.
   - **Option B**: Modify `smoke_new.sii` and `smoke_new.sii.bak` at runtime (the game reloads on map change)
   - **Option C**: Use Proton `WINEDLLOVERRIDES` to force-load a Linux-side shim

2. **Steam-ID source on Linux**: 
   - `~/.steam/steam/config/loginusers.vdf` contains `SteamID` per Steam user
   - Parse the VDF format (it is a simple key-value text file)

3. **License server URL**: The URL `https://leakingsv2.modguard.com.br/key?key=...` is hardcoded.
   - If the server is down, the plugin must degrade gracefully. It must still emit smoke without license checks.

4. **Anti-cheat / VAC**: The original Windows DLL is code-signed, but the license check is **offline-validatable** (the server returns 200 if the key is valid). The Linux replacement does not call home unless the user supplies a license key. This is consistent with the original.

---

## 9. Build & Test Quick-Start

```bash
# Build
git clone <your-repo>/modguard_linux
cd modguard_linux
mkdir build && cd build
cmake .. && make

# Test (just the formula, no game)
./test_formula

# Deploy
cp libmodguard_linux.so ~/.local/share/American\ Truck\ Simulator/bin/linux_x64/plugins/

# Run game
steam steam://run/270880
```

---

## 10. References

- **MODGuard spec**: `01-reverse-engineering.md`
- **Game class layout**: `02-particle-classes.md`
- **Smoke mod format**: `03-smoke-mod-analysis.md`
- **Open questions**: `04-open-questions.md`
- **SCS Telemetry SDK**: https://modding.scssoft.com/wiki/Documentation/Engine/Plugins/Telemetry
- **libcurl**: https://curl.se/libcurl/
- **SCS SDK headers**: https://github.com/nlnwa/scs-telemetry