# MODGuard.dll Reverse Engineering Specification

## File Metadata

| Property | Value |
|----------|-------|
| **File** | `MODGuard.dll` |
| **SHA-256** | `2ad7e487125e491d6886721003ddb409b5fba239c86ccda7e1fc7602de7cb097` |
| **SHA-1** | `9242b2fc27912dfba3bf4ea174fda36abb195124` |
| **MD5** | `3a9dcc920253214b5e2fc719f63d155c` |
| **Size** | 705,120 bytes |
| **Format** | PE32+ x86-64 Windows DLL |
| **Code-signed** | Yes (Sectigo Public Code Signing CA EV R36 + DigiCert Trusted G4 TimeStamping) |
| **Exports** | `scs_telemetry_init`, `scs_telemetry_shutdown` |
| **TLS callbacks** | `0x180050790`, `0x1800507f8` |
| **Authenticode** | Signed, embedded cert at 0x8f4e0 area (DigiCert TimeStamping CA) |

---

## 1. High-level Architecture

MODGuard is a Windows-only **SCS Telemetry SDK plugin** for **American Truck Simulator** and **Euro Truck Simulator 2**. It implements:

1. **License-key validation** — online hardware-bound check via WinHTTP
2. **Smoke particle visual mod** — runtime hook of the game's particle system
3. **RealisticSmoke formula** — scales smoke density based on telemetry inputs

The mod declares an `mscoree.dll` import but does not load the CLR runtime. This is likely leftover from a prior .NET build attempt.

---

## 2. SCS SDK Interface

### 2.1 Init params struct

```c
struct scs_telemetry_init_params_v100 {
    scs_init_params common;            // +0x00 (size 32 bytes)
    //   common.game_id        @ +0x00  const char*  ("eut2" / "ats")
    //   common.game_version   @ +0x08  u32
    //   common.game_name      @ +0x10  const char*
    //   common.log            @ +0x18  scs_log_fn
    scs_register_for_event_fn     register_for_event;       // +0x20
    scs_unregister_from_event_fn  unregister_from_event;    // +0x28
    scs_register_for_channel_fn   register_for_channel;     // +0x30
    scs_unregister_from_channel_fn unregister_from_channel;  // +0x38
};
```

### 2.2 `scs_telemetry_init(version, params)` flow

Entry: `RVA 0x359c0`, size 0x914 (pdata).

```
1. Validate version == 0x10001 (SCS_TELEMETRY_VERSION_1_00 | _1_01)
2. Save SDK function pointers at .data global slots:
      params[0x20] (register_for_event)       -> [0x1800aa200]
      params[0x28] (unregister_from_event)    -> [0x1800aa1a8]
      params[0x30] (register_for_channel)     -> [0x1800aa290]
      params[0x38] (unregister_from_channel)  -> [0x1800aa2f8]
3. Detect game via host-EXE PE-walk:
      Function 0x180003ff0 -> calls GetModuleHandleW(NULL) + GetModuleFileNameW
                               + UTF-16LE substring search for "eurotrucks2.exe" / "amtrucks.exe"
      Returns 1 = ETS2, 2 = ATS, 0 = other
4. Verify SDK game_id matches detected game:
      strcmp(params[0x08], "eut2" or "ats")
      "eut2" string at 0x18008e900, "ats" at 0x18008e908
5. Lock mutex, store next-buffer pointer (params[0x18]) -> [0x1800aa200]
6. Call 0x180002d00 (deferred init) — emits log strings, spawns worker thread
7. Install MinHook smoke particle hooks (essential_hook, particle_allocator_hook)
8. Register 5 SCS SDK EVENT callbacks via register_for_event (NOT channels):
      call site 0x180035c60: rcx = event_id, rdx = callback_fn, r8 = 0
      Mapping:
        event 1 (FRAME_START)    -> 0x180034690 (pre-frame work)
        event 2 (FRAME_END)      -> 0x180034c40 (jmp 0x180033640, main update loop)
        event 3 (PAUSED)         -> 0x180034550 (sets paused flag)
        event 4 (STARTED)        -> 0x1800345f0 (clears started flag)
        event 5 (CONFIGURATION)  -> 0x180033e90 (config event handler, particle classifier)
9. Telemetry VALUES read via SDK's `next_float_value`/`next_string_value`/
   `next_vector_value` callbacks at params[0x48..0x58] in v1.01.
10. Load config via GetPrivateProfileStringW from
    `<Documents>/Euro Truck Simulator 2/mod/*.ini` or
    `<Documents>/American Truck Simulator/mod/*.ini`
    Sections: `[RealisticSmoke]`, `[MODFiles]`
11. Spawn worker thread via CreateThread — runs once (license HTTP), exits
12. Return SCS_RESULT_OK
```

### 2.3 `scs_telemetry_shutdown()` flow

Entry: `RVA 0x362d0`, size 0x67e.

```
1. Set shutdown flag (byte [rip + 0x73dcf] = 1)
2. Lock SRWLock; clear worker-thread result queue
3. For each registered event (1..5): unregister_from_event(event_id, callback)
4. Remove MinHook smoke particle hook (MH_Disable + MH_Uninitialize)
5. Zero out telemetry buffers, restore default particle values
6. Flush settings to .ini via WritePrivateProfileStringW
7. Free allocated buffers
8. Return SCS_RESULT_OK
```

---

## 3. Imports (high-signal)

**KERNEL32.dll** (most relevant subset):
- `VirtualProtect`, `VirtualAlloc`, `VirtualFree`, `VirtualQuery`
- `SuspendThread`, `ResumeThread`, `GetThreadContext`, `SetThreadContext`
- `OpenThread`, `Thread32First`, `Thread32Next`
- `CreateThread`, `ExitThread`
- `GetPrivateProfileStringW`, `WritePrivateProfileStringW`
- `Sleep`, `SleepConditionVariableSRW`
- `IsDebuggerPresent`
- `LoadLibraryExW`, `GetProcAddress`

**USER32.dll**: `GetAsyncKeyState`

**ADVAPI32.dll**: `RegOpenKeyExW`, `RegQueryValueExW`, `RegCloseKey`

**SHELL32.dll**: `SHGetKnownFolderPath`

**ole32.dll**: `CoTaskMemFree`

**IPHLPAPI.DLL**: `GetAdaptersInfo` (MAC address for license fingerprint)

**WINHTTP.dll**: `WinHttpOpen`, `WinHttpConnect`, `WinHttpOpenRequest`, `WinHttpSendRequest`, `WinHttpReceiveResponse`, `WinHttpQueryHeaders`, `WinHttpSetOption`, `WinHttpCloseHandle`, `WinHttpCrackUrl`

**mscoree.dll**: declared but not loaded (legacy)

---

## 4. Internal Data Layout

### 4.1 Global state slots

| Address | Type | Initial value / purpose |
|---------|------|-------------------------|
| `0x1800aa200` | fn ptr | `params[0x20]` = `register_for_event` |
| `0x1800aa1a8` | fn ptr | `params[0x28]` = `unregister_from_event` |
| `0x1800aa290` | fn ptr | `params[0x30]` = `register_for_channel` |
| `0x1800aa2f8` | fn ptr | `params[0x38]` = `unregister_from_channel` |
| `0x1800aa5d0` | fn ptr | cached active register fn (overwritten in init) |
| `0x1800a210c` | int | event tick counter for license refresh |
| `0x1800a2114` | float | work buffer |
| `0x1800a1d8` | word | 300-frame poll counter (channel 2 callback) |
| `0x1800a5e88` | dword | license/initialization state flag (set to 1.0f = 0x3f800000) |

### 4.2 Event callback signatures

The 5 event callbacks at `0x180034690`, `0x180033640`, `0x180034550`, `0x1800345f0`, `0x180033e90` receive the SDK standard `(event_info, event_handle*)` parameters via `rcx` and `rdx`.

---

## 5. Smoke Particle Hook (MinHook)

MODGuard installs **two MinHook trampolines** inside the game's binary at runtime:

### 5.1 Hook targets

1. **`essential_hook`** — patches the SiiNunit loader `model_particle` allocator path. This lets MODGuard intercept the `model_particle : model.particle.smoke_new` instance. The game calls this function once per truck/exhaust when it loads a `.pmg` file.

2. **`particle_allocator_hook`** — patches the per-emitter allocator inside `particle_gen`. These are the `_nameless.208.53FC / 53FD / 53FE` instances. This lets MODGuard change per-frame spawn parameters (`particles_max_count`, `particle_size`, `scale_acc`).

Install function: `0x180031460` runs `MH_Initialize`, `MH_CreateHook`, `MH_EnableHook`.

Error strings (in .rdata):
- `minhook_initialize_failed`
- `essential_hook_create_failed` / `essential_hook_enable_failed`
- `particle_allocator_hook_create_failed` / `particle_allocator_hook_enable_failed`
- `model_particle_generator_array_not_found`
- `model_particle_generator_union_invalid`

### 5.2 Live particle iteration (channel 3 callback @ `0x180033e90`)

```c
for (rbx = list_head; rbx != NULL; rbx += 0x40) {
    if (*(uint32_t*)(rbx + 0x10) == 5 /* smoke_new */) {
        float size = *(float*)(rbx + 0x18);
        if (isnan_or_isinf(size)) { /* skip */ }
        else if (size <  100.0f)  size = 100.0f;
        else if (size <= 2500.0f) size = 2500.0f;
        else                       size = 10000.0f;
        *(float*)(rbx + 0x18) = size;
    }
}
```

Bucket constants (checked at runtime addresses):
- `0x180090ff8`: `100.0f` (bucket A — barely visible)
- `0x180091000`: `2500.0f` (bucket B — normal smoke)
- `0x180091008`: `10000.0f` (bucket C — heavy smoke)

See **`02-particle-classes.md`** for the complete game-side class layout that this hook targets.

---

## 6. RealisticSmoke Formula

### 6.1 Telemetry inputs (read each frame)

| Channel name | Type | Used as |
|--------------|------|---------|
| `truck.speed` | float | speed_norm = speed / 30 m/s |
| `truck.engine.rpm` | float | rpm_norm = rpm / rpm.limit |
| `truck.effective.throttle` | float | throttle_smoothed |
| `truck.displayed.gear` | s32 | gear / shift detection |
| `truck.effective.clutch` | float | clutch_norm |
| `truck.engine.enabled` | bool | gate condition |
| `truck.local.acceleration.linear` | fvector | traction accel |
| `cargo.mass` | float | cargo_mass_norm |
| `cargo.loaded` | bool | cargo gate |
| `rpm.limit` | float | rpm divisor |
| `truck` | prefix | group prefix |

### 6.2 Per-input dynamics

- **Throttle**: envelope follower with `AttackSmoothness` (rise) and `ReleaseSmoothness` (fall). The transient detector checks against `ThrottleRiseThreshold`. An overshoot yields `ThrottleTransientStrength * exp(-t / ThrottleTransientDecay)`.
- **RPM / Speed / Acceleration**: exponential low-pass smoothing
- **Gear**: step detector. A change applies `ShiftWeight * exp(-t / 0.3)` for 0.3 s.
- **Launch**: detects a zero-speed-to-throttle event. This applies `LaunchWeight * exp(-t / 1.5)` for 3 s.
- **FreeRev**: detects engine on, no throttle, and neutral. This applies `FreeRevWeight`.
- **Load**: `LoadSensitivity * cargo_mass_norm` while `cargo_loaded`
- **Traction**: `TractionAccelerationWeight * accel_norm`

### 6.3 Combined output

```c
float input = 0.0f;
input += pow(throttle_smoothed, ThrottleCurve) * ThrottleWeight;
input += rpm_norm                                              * RpmWeight;
input += speed_norm                                            * SpeedWeight;
input += accel_norm                                            * AccelerationWeight;
input += gear_transient                                        * ShiftWeight;
input += clutch_norm                                           * ClutchWeight;
input += cargo_mass_norm * cargo_loaded                        * CargoWeight;
input += launch_envelope                                       * LaunchWeight;
input += traction_accel                                        * TractionAccelerationWeight;
input += load_effort                                           * LoadEffortWeight;
input += free_rev_envelope                                     * FreeRevWeight;

float sustained_throttle = (steady_throttle > SustainedThrottleStrength)
                         ? steady_throttle : 0.0f;
input += sustained_throttle * EventStrength;

float session_peak = max(session_peak * SessionPeakPercent, current);  // peak-hold
float output = input * Intensity * FinalSmokeMultiplier;
output = clamp(output * (session_peak > 0.0f ? input / session_peak : 1.0f),
               100.0f, 10000.0f);  // bucket assignment
```

**Note on `ThrottleCurve`**: static analysis did not definitively determine whether MODGuard uses this as a power or linear multiplier (see `04-open-questions.md` Q2). The most likely interpretation is **linear** (`throttle * ThrottleCurve`) because:
- MODGuard does not import or statically link `powf`/`logf`/`expf`
- Channel 2 callback uses only multiplication, no `pow` calls
- Default value is 1.0. This makes linear an identity.

---

## 7. Configuration (.ini file)

### 7.1 File location

```
Windows: %USERPROFILE%/Documents/Euro Truck Simulator 2/mod/*.ini
         %USERPROFILE%/Documents/American Truck Simulator/mod/*.ini

Linux equivalent:
   ~/.local/share/Euro Truck Simulator 2/mod/*.ini
   ~/.local/share/American Truck Simulator/mod/*.ini
```

### 7.2 Schema — `[RealisticSmoke]` section

```
[RealisticSmoke]
SmokeHook=1                   ; bool, default 1
ContinuousSmoke=0             ; bool, default 0
FinalSmokeMultiplier=1.0      ; float
Intensity=0.5
LoadSensitivity=1.0
AttackSmoothness=0.3
ReleaseSmoothness=0.3
EventStrength=1.0
SustainedThrottleStrength=0.5
ThrottleTransientStrength=0.7
ThrottleRiseThreshold=0.05
ThrottleTransientDecay=0.1
ThrottleCurve=1.0
ThrottleWeight=1.0
RpmWeight=1.0
SpeedWeight=1.0
AccelerationWeight=1.0
GearWeight=1.0
ClutchWeight=1.0
CargoWeight=1.0
LaunchWeight=1.0
TractionAccelerationWeight=1.0
LoadEffortWeight=1.0
FreeRevWeight=1.0
ShiftWeight=1.0
SessionPeakPercent=0.85
```

Schema — `[MODFiles]` section: registry of attached file hashes (info only).

---

## 8. License Validation

### 8.1 Inputs
- User license key (from config or registry)
- Hardware fingerprint: MAC address via `IPHLPAPI::GetAdaptersInfo`, formatted as 12 hex chars

### 8.2 URL
```
GET https://leakingsv2.modguard.com.br/key?key=<KEY>&steamID=<STEAM_ID>&clientID=<CLIENT_ID>
```

### 8.3 Flow
1. Read MAC via `GetAdaptersInfo` (function `0x180013c40`)
2. Format as `%02x%02x%02x%02x%02x%02x` (12-char string)
3. Build full URL via `MultiByteToWideChar` + format
4. `WinHttpOpen` -> `WinHttpConnect` -> `WinHttpOpenRequest(GET, ...)`
5. `WinHttpSendRequest` -> `WinHttpReceiveResponse` -> `WinHttpQueryHeaders(STATUS_CODE)`
6. Compare the status code to `200 (0xc8)`. If it matches, the license is valid.

### 8.4 Refresh interval

The worker thread body (`0x180031f80`) runs **ONCE** and exits (no `Sleep` loop).

The main thread (channel 2 callback) **polls worker results every 300 frames** via a `uint16` counter at `[rip + 0x757e0]` (RVA 0x1800aa1d8):

```
0x180034947: movzx eax, word ptr [rip + 0x757ea]   ; load counter
0x18003494e: inc ax                                   ; counter++
0x180034951: mov word ptr [rip + 0x757e0], ax       ; store
0x180034958: mov ecx, 0x12c                          ; ecx = 300
0x18003495d: cmp ax, cx                             ; if (counter != 300) skip
0x180034960: jne 0x1800349b8
0x180034962: mov word ptr [rip + 0x757cf], bp       ; reset to 0
...                                                       (drain worker queue)
```

At 60 fps this is 5 seconds. At 30 fps this is 10 seconds.

### 8.5 Steam-ID acquisition

`&steamID=` is in the URL template but MODGuard **does NOT import Steam API**. The source is unknown without dynamic analysis — likely one of:
- Shared memory (set by another mod or by the game launcher)
- Windows registry (`HKCU\Software\Valve\Steam\ActiveProcess`)
- The user's `.ini` config

### 8.6 Status strings

```
[MODGuard] License key validated successfully.
[MODGuard] Visual feedback not activated: <reason>
[MODGuard] Smoke mod found and activated successfully.
[MODGuard] Smoke hook not activated: <reason>
```

---

## 9. Anti-debug / hardening

- TLS callbacks at `0x180050790` and `0x1800507f8` run before DllMain
- `IsDebuggerPresent` imported
- Code-signed by DigiCert / Sectigo (Authenticode enforced on Windows)
- UTF-16LE storage for config keys (defeats naive `strings`)

---

## 10. Function Reference

| RVA | Name | Notes |
|-----|------|-------|
| `0x359c0` | `scs_telemetry_init` | SDK entry |
| `0x362d0` | `scs_telemetry_shutdown` | SDK entry |
| `0x002d00` | deferred init | logs license/smoke status |
| `0x002d9c` | log: license validated | ref |
| `0x002ddc` | log: smoke mod activated | ref |
| `0x002e46` | log: developer credits | ref |
| `0x003dc0` | game-id from PE substring | UTF-16LE scan |
| `0x003ff0` | game-id entry | returns 1/2/0 |
| `0x013c40` | MAC formatter | produces `%02x...` string |
| `0x014300`-`0x014500` | config writer / validators | reads `.ini` |
| `0x016250` | config regex setup | |
| `0x017150` | config loader (big function) | 1318-line disasm |
| `0x01c650` | `std::regex` internal | |
| `0x01d8a0` | typeinfo destructor | |
| `0x01de90` | regex iter::increment | |
| `0x01fa10` | regex iter variant | |
| `0x029460` | bucket assignment logic | particle classifier |
| `0x02a67f` | linked list manipulation | |
| `0x02e0a0` | worker thread result drain | |
| `0x031460` | MinHook installer | |
| `0x031f80` | worker thread body | runs once |
| `0x032060` | worker thread start | |
| `0x032190` | shutdown helper | |
| `0x031b80` | state save | |
| `0x033640` | channel 2 callback | main update loop |
| `0x033e90` | channel 5 / config event handler | particle classifier |
| `0x0342c0` | settings validator | reads `.ini` |
| `0x034550` | channel 3 (paused) callback | sets flag |
| `0x0345f0` | channel 4 (started) callback | clears flag |
| `0x034690` | channel 1 (frame_start) callback | |
| `0x034c40` | channel 2 entry (jmp 0x33640) | |
| `0x036c50` | worker queue management | |
| `0x037210` | destructor (cleanup) | |
| `0x0470b0` | URL builder | constructs HTTP request |
| `0x047225` | WinHttpCrackUrl | |
| `0x04724a` | WinHttpOpen | |
| `0x047278` | WinHttpSetOption | |
| `0x04728f` | WinHttpConnect | |
| `0x0472d3` | WinHttpOpenRequest | |
| `0x0472fb` | WinHttpSendRequest | |
| `0x04730a` | WinHttpReceiveResponse | |
| `0x04733c` | WinHttpQueryHeaders (status) | |
| `0x047358`/`0x47361`/`0x4736a` | WinHttpCloseHandle (3x) | |
| `0x050790` | TLS callback 1 | anti-debug / preload |
| `0x0507f8` | TLS callback 2 | |
| `0x056adc` | key validator (HWID hash) | |
| `0x04d1ac` | SRWLock acquire | |
| `0x04d1b4` | SRWLock release | |
| `0x0503e4` | operator new | |
| `0x0503b0` | operator delete | |