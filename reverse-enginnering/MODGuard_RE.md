# MODGuard.dll Reverse Engineering Specification
## Reimplementation Target: ETS2/ATS on Linux

**Source binary**: `MODGuard.dll`
**SHA-256**: `2ad7e487125e491d6886721003ddb409b5fba239c86ccda7e1fc7602de7cb097`
**Size**: 705,120 bytes
**Architecture**: PE32+ x86-64 Windows DLL
**Exports**: `scs_telemetry_init`, `scs_telemetry_shutdown` (SCS Telemetry SDK plugin)

---

## 1. High-level Architecture

MODGuard is a Windows-only SCS Telemetry SDK plugin for **Euro Truck Simulator 2** and **American Truck Simulator**. It implements:

1. A **license-key check** (online validation via WinHTTP, hardware-bound)
2. A **smoke particle visual mod** (MinHook-based runtime hook of the game's particle system)
3. A **RealisticSmoke formula** that scales smoke particle density based on telemetry inputs

The mod embeds an additional scs-telemetry-sidecar (no `mscoree.dll` runtime is loaded despite the import being declared — likely leftover from a prior .NET/CLR build).

---

## 2. SDK Interface (`scs_telemetry_init` / `scs_telemetry_shutdown`)

### 2.1 Init params struct (matches SCS SDK v1.00/v1.01 layout)

```c
struct scs_telemetry_init_params_v100 {
    scs_init_params common;          // +0x00
    // common:
    //   +0x00: const char* game_name      (e.g., "Euro Truck Simulator 2")
    //   +0x08: const char* game_id        (e.g., "eut2", "ats")
    //   +0x10: u32 game_version (+0x14 padding on x64)
    //   +0x18: scs_log_t log_fn
    scs_register_for_event_fn     register_for_event;     // +0x20
    scs_unregister_from_event_fn  unregister_from_event;  // +0x28
    scs_register_for_channel_fn   register_for_channel;   // +0x30
    scs_unregister_from_channel_fn unregister_from_channel;// +0x38
};
```

### 2.2 `scs_telemetry_init(version, params)` flow

```
1. Validate version == 0x10001 (SCS_TELEMETRY_VERSION_1_00 | _1_01)
2. Save SDK fn ptrs at global storage (verified via 4 stores at 0x1800359fe..0x180035a1f):
     params->register_for_event       -> [0x1800aa200]
     params->unregister_from_event    -> [0x1800aa1a8]
     params->register_for_channel     -> [0x1800aa290]
     params->unregister_from_channel  -> [0x1800aa2f8]
3. Detect game by walking the host EXE PE for the substring "eurotrucks2.exe"
   or "amtrucks.exe" (UTF-16LE) in the version-info block:
       function 0x180003ff0 returns 1=ETS2, 2=ATS, 0=other
4. Verify SDK-supplied game_id (params+0x08) matches detected game
   (strcmp with "eut2" or "ats" — encoded as ASCII short strings at 0x18008e900)
5. Lock mutex and cache the SDK log callback from params+0x18 at 0x1800aa170
6. Call 0x180002d00 (deferred init) — logs license/smoke status, spawns worker thread
7. Install MinHook smoke particle hooks (essential_hook, particle_allocator_hook)
8. Register 5 SCS SDK EVENT callbacks via register_for_event (NOT channels):
     Event-call site at 0x180035c60: rcx=event_id, rdx=callback_fn, r8=0
     Mapping (out-of-order registration):
       event 1 (FRAME_START)    -> 0x180034690 (telemetry pre-frame work)
       event 2 (FRAME_END)      -> 0x180034c40 (jmp 0x180033640 — main update + 300-frame poll)
       event 3 (PAUSED)         -> 0x180034550 (sets byte [rip + 0x719c6] = 1)
       event 4 (STARTED)        -> 0x1800345f0 (sets byte [rip + 0x71926] = 0)
       event 5 (CONFIGURATION)  -> 0x180033e90 (truck/job configuration attributes)
   The `rdx` argument is NOT a string pointer; it points to real machine code
   because MODGuard uses a custom call wrapper where the second arg is the
   event-handler function pointer.
9. Register telemetry channels through `register_for_channel` at 0x1800342c0. Each call uses the
   standard six-argument SDK signature: name, index=-1, value type, flags=3
   (`EACH_FRAME | NO_VALUE`), callback, context=NULL. The callback receives the current `scs_value_t`
   when available; `NO_VALUE` also requests a callback with a NULL value when the channel is
   unavailable. There are no `next_float_value`/`next_string_value`/`next_vector_value` fields in
   this init struct.
10. Load config via GetPrivateProfileStringW from
    `<Documents>/Euro Truck Simulator 2/mod/*.ini` or
    `<Documents>/American Truck Simulator/mod/*.ini`
    Sections read: `[RealisticSmoke]`, `[MODFiles]`
11. Spawn worker thread (CreateThread) — thread body runs once (license HTTP check), exits.
12. Return SCS_RESULT_OK
```

### 2.3 `scs_telemetry_shutdown()`

```
1. Set shutdown flag (`byte [rip + 0x73dcf] = 1` at 0x180036303)
2. Lock SRWLock and clear worker-thread result queue
3. For each registered event (1..5): call unregister_from_event(event_id, callback)
4. Remove MinHook smoke particle hook (MH_Disable + MH_Uninitialize)
5. Zero out telemetry buffers, restore default particle values
6. Flush settings to .ini (WritePrivateProfileStringW)
7. Free allocated buffers
8. Return SCS_RESULT_OK
```

---

## 3. Telemetry Channels (consumed)

ASCII strings from .rdata indicate the telemetry names read by MODGuard:

| Channel name                       | Type    | Purpose |
|------------------------------------|---------|---------|
| `truck.speed`                      | float   | Truck speed (m/s) |
| `truck.engine.rpm`                 | float   | Engine RPM |
| `truck.effective.throttle`         | float   | Throttle input (smoothed) |
| `truck.displayed.gear`             | s32     | Current displayed gear |
| `truck.effective.clutch`           | float   | Clutch input |
| `truck.engine.enabled`             | bool    | Engine on/off |
| `truck.local.acceleration.linear`  | fvector | Linear acceleration (m/s²) |
| `cargo.mass`                       | float   | Cargo mass (kg) |
| `cargo.loaded`                     | bool    | Cargo attached |
| `rpm.limit`                        | float   | Max RPM |
| `truck`                            | (prefix)| Used to detect truck sub-channels |

These match the SCS Telemetry SDK channel names exactly. The Linux reimplementation can read them through the same SDK header (or equivalent shared-memory channel).

---

## 4. Configuration (`.ini` file)

### 4.1 File location
```
%USERPROFILE%/Documents/Euro Truck Simulator 2/mod/<modname>.ini
%USERPROFILE%/Documents/American Truck Simulator/mod/<modname>.ini
```
On Linux the analog is:
```
~/.local/share/Euro Truck Simulator 2/mod/<modname>.ini
~/.local/share/American Truck Simulator/mod/<modname>.ini
```

### 4.2 Schema

```
[RealisticSmoke]
SmokeHook=1                   ; bool, default 1
ContinuousSmoke=0             ; bool, default 0
FinalSmokeMultiplier=1.0      ; float, multiplier applied at end of formula
Intensity=0.5                 ; float, overall gain
LoadSensitivity=1.0           ; float
AttackSmoothness=0.3          ; float (lower = snappier)
ReleaseSmoothness=0.3         ; float
EventStrength=1.0             ; float
SustainedThrottleStrength=0.5 ; float
ThrottleTransientStrength=0.7 ; float
ThrottleRiseThreshold=0.05    ; float
ThrottleTransientDecay=0.1    ; float
ThrottleCurve=45              ; integer -1..100; exponent = 1 + value/100 (default 45)
ThrottleWeight=1.0            ; float
RpmWeight=1.0                 ; float
SpeedWeight=1.0               ; float
AccelerationWeight=1.0        ; float
GearWeight=1.0                ; float
ClutchWeight=1.0              ; float
CargoWeight=1.0               ; float
LaunchWeight=1.0              ; float
TractionAccelerationWeight=1.0
LoadEffortWeight=1.0
FreeRevWeight=1.0
ShiftWeight=1.0
SessionPeakPercent=0.85       ; float, peak-hold threshold

[MODFiles]
; info / hash tracking block (registry of attached files)
```

---

## 5. Smoke Particle Hook (MinHook)

### 5.1 What is hooked (verified from the companion mod `PM_389_Smoke`)

The companion mod **PM_389_Smoke** by ToniVolma (same author as MODGuard) installs the runtime
artifacts that MODGuard hooks into:

- 162 of 163 `.pmg` exhaust-mesh files (across 17 truck brands + 5 upgrade packs) embed the ASCII hookup
  string `model.particle.smoke_new` directly in their string table.
- `unit/hookup/smoke_new.sii` declares the class instance:

```
SiiNunit
{
model_particle : model.particle.smoke_new {
 active_distance: 220
 gen_array: 3
 gen_array[0]: _nameless.208.53FC
 gen_array[1]: _nameless.208.53FD
 gen_array[2]: _nameless.208.53FE
 auto_disable_delay: 0
}

particle_gen : _nameless.208.53FC { particles_max_count: 40, model_path: "/model/particle/smoke.pmd", model_look: pipe, particle_size: (0.090, 0.090), time_to_live: 3, flow_speed: (0, 1.5, -2.5), scale_acc: 0.38, ... }
particle_gen : _nameless.208.53FD { particles_max_count: 20, ..., particle_size: (0.040, 0.040), time_to_live: 0.7, ... }
particle_gen : _nameless.208.53FE { particles_max_count:  4, ..., particle_size: (0.020, 0.020), time_to_live: 0.2, relative_space: true, ... }
}
```

The pre-mod backup `smoke_new.sii.bak` shows the **original single emitter**:
`gen_array: 1`, `particles_max_count: 60`, `model_look: dirt`, `particle_size ≈ 0.04`,
`time_to_live: 2`. The mod splits this into **three layered emitters** (40 + 20 + 4 = 64 particles,
near→far tiered size 0.090 → 0.040 → 0.020) and swaps the sprite texture to `pipe.dds`.

### 5.2 PMG hookup entry layout (verified)

In every patched `.pmg`, the ASCII string `model.particle.smoke_new` (24 bytes) appears followed by a
4-byte NUL pad, with this canonical prelude and body:

```
... <ptrA: uint32 LE>  <0xFFFFFFFF × 2>  <ptrB: uint32 LE>  'model.particle.smoke_new'  <00 00 00 00>
    <float[6]  24 B>   <0xFF000000>      <0x00000000 × 2>   ← 36-byte entry body
    <float[6]  24 B>   <0xFF000000>      <0x00000000 × 2>   ← next body
    ...                                                                (one body per LOD/instance)
```

`ptrA` / `ptrB` are mesh-internal offsets back into the geometry block; they point at the first/last
mesh data byte referenced by this hookup. Multiple 36-byte bodies follow because the same hookup name
is reused for every LOD / skin instance.

The single ASCII hookup string used in every file is **`model.particle.smoke_new`**. No other hookup
names appear in any PMG of this mod.

**Runtime integration contract:**

The data mod does not read telemetry or a value published by MODGuard. Its `.sii`, `.pmg`, `.tobj`,
and `.dds` files are static game data and contain no callback, shared-memory, variable-substitution,
or plugin-input mechanism. Cooperation happens entirely inside the game process:

1. A patched exhaust PMG requests hookup `model.particle.smoke_new` at its embedded transform.
2. The game resolves the `model_particle` unit and its three `particle_gen` references from
   `smoke_new.sii`.
3. MODGuard's lookup/materializer hooks recognize that public unit ID and register the resulting
   game-owned model, generator, and particle-container objects.
4. Telemetry is reduced to a normalized smoke-demand value in `[0,1]`.
5. The allocator hook admits approximately that fraction of particle spawn requests for registered
   smoke containers and returns `NULL` for the rest.

The mod therefore supplies **what and where to render**; the plugin controls **how many requested
particles are actually created over time**. The static `particle_size`, lifetime, flow, alpha,
texture, and `particles_max_count` values continue to govern every admitted particle. The maximum
count is a capacity limit, not the live intensity control.

The identifier match is explicit in the DLL: wrapper `0x1800324a0` compares against
`model.particle.` at `0x18008eb90`, then helper `0x180038890` searches the candidate string for
`particle.smoke_new` at `0x18008ebc0`. The generator IDs `_nameless.208.53FC/53FD/53FE` do not occur
in the DLL; after matching the public model ID, MODGuard follows and validates the model's generator
references by class and object identity.

### 5.3 MODGuard hooks

MODGuard installs **three essential hooks** and a later particle-allocation hook inside the Windows
game executable. The essential-hook setup function is `0x180034df0`; `0x180031460` is instead a
`ContinuousSmoke` configuration parser.

| Call site | Runtime target global | Replacement | Trampoline | Purpose |
|---|---|---|---|---|
| `0x1800351ce` | `0x1800aa1e8` | `0x1800324a0` | `0x1800aa238` | Generic unit lookup/load wrapper |
| `0x1800351ea` | `0x1800aa1b0` | `0x180032700` | `0x1800aa2e8` | `model_particle` materializer wrapper |
| `0x180035206` | `0x1800aa218` | `0x1800327e0` | `0x1800aa1f0` | `particle_gen` materializer wrapper |

The targets are not hard-coded game RVAs. Setup parses the host PE and discovers them at runtime.
The unit-lookup target is the unique executable-section match for an exact 31-byte signature
beginning `40 56 48 81 EC 90 00 00 00 48 8D 44 24 40`. The materializers are found from the
`model_particle` and `particle_gen` class descriptors: the scanner recognizes a
`48 8D 05 <rel32> C3` descriptor-return function, follows the containing class/materializer record,
validates executable pointers, and requires one result per class. Failure strings include
`unit_lookup_not_unique`, `model_materializer_not_unique`, and `generator_materializer_not_unique`.

The wrappers call their trampolines, validate returned objects against the discovered class sets,
and record that a usable particle model/generator has materialized.

A fourth hook is installed later at `0x180034b23`. Its target is observed dynamically from a
validated particle-container object and stored at `0x1800aa288`; replacement `0x180033380` and
trampoline `0x1800aa298` implement selective allocation forwarding. Conflicting observations
produce `particle_allocator_mismatch`.

The allocator replacement does not rewrite arguments or returned particle fields:

- Containers absent from the smoke-container registry are forwarded unchanged at `0x180033412`.
- For a registered container, it reads packed output `0x1800aa1c0`, checks two eligibility gates,
  and clamps the embedded float to `[0,1]`.
- It multiplies demand by `16777216.0f` (`2^24`, constant `0x180091028`) and uses a mixed
  container/counter value as a deterministic 24-bit sampling sequence.
- Accepted requests call the original allocator at `0x1800335b7`; rejected requests return `NULL`
  at `0x180033619`.

Thus `demand=1` forwards every eligible request, `demand=0` suppresses all requests, and intermediate
values thin the spawn stream proportionally. This controls emission density without mutating
`particles_max_count`, `particle_size`, `scale_acc`, or existing live particles.

Hook installation checks:
```
- MH_Initialize
- MH_CreateHook(target_addr, replacement_fn, &trampoline)
- MH_EnableHook(target_addr)
```

Errors observed in MODGuard strings:
- `minhook_initialize_failed`
- `essential_hook_create_failed` / `essential_hook_enable_failed`
- `particle_allocator_hook_create_failed` / `particle_allocator_hook_enable_failed`
- `model_particle_generator_array_not_found`
- `model_particle_generator_union_invalid`

### 5.4 Configuration callback behaviour

The event-5 callback at `0x180033e90` consumes the standard SDK
`scs_telemetry_configuration_t`, not particle records. Its attribute array contains 0x40-byte
`scs_named_value_t` entries:

```c
struct scs_named_value_t {             // size 0x40
    const char *name;                  // +0x00
    uint32_t index;                    // +0x08
    uint32_t padding;
    scs_value_t value;                 // +0x10: type, +0x18: union payload
};
```

Consequently, `cmp dword ptr [rbx+0x10], 5` checks
`SCS_VALUE_TYPE_float`, `[rbx+0x18]` reads the float payload, and `[rbx+0x40]` is the next
attribute's name pointer. It does not test `class_id == 5`, and the 0x40 stride says nothing about
the engine particle layout.

The callback handles two configuration IDs:

- For `truck`, it finds float attribute `rpm.limit`. A finite value is clamped to
  `[100.0, 10000.0]`; missing/invalid data leaves the default `2500.0`. The result is stored at
  `0x1800a2114`, and the found flag at `0x1800a7fee`.
- For `job`, it finds float `cargo.mass`, clamps it to at least zero, and stores it at
  `0x1800aa1e0`. It also reads bool `cargo.loaded` and updates flags at `0x1800a5f1f` and
  `0x1800a7fed`.

The constants at `0x180090ff8`, `0x180091000`, and `0x180091008` are therefore RPM-limit clamp
minimum/default/maximum values. They are not smoke-size buckets.

### 5.5 Engine particle layout is independent

No engine particle structure can be inferred from the SDK configuration callback. Native runtime
particle nodes were recovered separately from the game allocator/update paths and are documented in
§5.8.

### 5.6 ATS 1.55.0.0 — Actual class layout from game binary analysis

Extracted by walking the vtables and RTTI typeinfos in `/mnt/ssd1/SteamLibrary/steamapps/common/American Truck Simulator/bin/linux_x64/amtrucks` (build ATS 1.55.0.0):

**Class hierarchy** (verified from Itanium ABI RTTI direct-base pointers):

```
prism::model_hookup_u
   └── prism::particle_set_u
         └── prism::model_particle_u

prism::unit_t
   └── prism::particle_gen_u
         └── prism::particle_emitter_u         (ATS only)
```

`model_particle_u` and `particle_gen_u` are not in one inheritance chain. A
`particle_set_u` owns an `array_dyn_t<owner_ptr_t<particle_gen_u>>` at about `+0xb0`. ATS array
entries may point to the derived `particle_emitter_u`; ETS2 entries point directly to
`particle_gen_u`.

Confirmed ATS objects:

- `particle_gen_u`: size 0x1a68; vtable header/address point `0x2d15030/0x2d15040`;
  complete/deleting destructors `0x1fae370/0x1fae500`.
- `particle_emitter_u`: size 0x1b40; vtable header/address point `0x2d14708/0x2d14718`;
  complete/deleting destructors `0x1fa6ba0/0x1fa6e00`.
- `model_particle_u`: size 0xf8; vtable header/address point `0x2b7a998/0x2b7a9a8`;
  complete/deleting destructors `0x13e21e0/0x13e21f0`.

The fields at emitter offsets `+0x1a70`, `+0x1a88`, and `+0x1aa0` are 0x18-byte string-like fields
named `list_sorting`, `texture_path`, and `nmap_path` in reflection metadata. They are not three
particle-generator sub-objects; the common destructor at `0x1c17280` and 0x18-byte deleting size
confirm this.

**ETS2 1.55.0.0 differences** (from `/mnt/ssd1/SteamLibrary/steamapps/common/Euro Truck Simulator 2/bin/linux_x64/eurotrucks2`):

- **No `prism::particle_emitter_u` class** — ETS2 stores base `particle_gen_u` objects in the
  `particle_set_u` owner-pointer array.
- ETS2 `model_particle_u` is also 0xf8 bytes, with vtable header/address point
  `0x2b03818/0x2b03828` and complete/deleting destructors `0x139a4f0/0x139a500`.
- ETS2 `particle_gen_u` is 0x1968 bytes, versus ATS's 0x1a68. Its dynamic-list sub-object is at
  `+0x558`, with the anchor near `+0x568` and a ref-counted pointer at `+0x1960`.
- ATS can store derived `particle_emitter_u` objects in the same owner-pointer array. Each emitter
  has one `particle_gen_u` base at offset zero; layered generators are separate array entries, not
  three 0x18-byte sub-objects inside one emitter.

### 5.7 Particle generator reflection layout (ATS/ETS2 1.55)

Both binaries retain 0x28-byte reflection descriptors containing a field-name pointer, owner-type
pointer, object offset, type/flags, and a reserved qword. These descriptors give direct semantic
names to the configuration-facing fields:

| Field | ATS `particle_gen_u` | ETS2 `particle_gen_u` |
|---|---:|---:|
| `list_order_up` | `+0x10` | `+0x10` |
| `blend_alpha` | `+0x14` | `+0x14` |
| `precomputation_time` | `+0x18` | absent |
| `relative_space` | `+0x1c` | `+0x13` |
| `gen_pos` | `+0x2c` | `+0x20` |
| `scale_acc` | `+0x30` | `+0x24` |
| `alpha_blend_coef` | `+0x38` | `+0x2c` |
| `base_alpha` | `+0x3c` | `+0x30` |
| `alpha_attack_time` | `+0x40` | `+0x34` |
| `flow_speed` | `+0x4c` | `+0x40` |
| `flow_speed_acc` | `+0x58` | `+0x4c` |
| `particle_size` | `+0x60` | `+0x54` |
| `particle_size_var` | `+0x68` | `+0x5c` |
| `particle_displacement` | `+0x74` | `+0x68` |
| `use_sprites` | `+0x78` | `+0x6c` |
| `time_to_live` | `+0x7c` | `+0x524` |
| `particles_max_count` | `+0x80` | `+0x70` |
| `model_path` | `+0x98` | `+0x88` |
| `color_multiplier` | `+0xa0` | `+0x90` |
| `key_pos` | `+0xd8` | `+0xc8` |
| `key_color` | `+0x62c` | `+0x520` |
| `time_to_live_rand` | `+0x630` | `+0x528` |

ATS descriptors `0x2d15100..0x2d154e8` belong to owner metadata `0x2d15520`, whose class record
reports size `0x1a68`. ETS2 descriptors `0x2c99240..0x2c995d8` belong to `0x2c99620`, whose
record reports size `0x1968`. `angle_randomization` has descriptor offset zero in both games and is
therefore accessor-backed rather than a direct field; its backing storage remains unknown.

ATS's derived `particle_emitter_u` adds reflection-visible fields at `+0x1a68..+0x1b28`, including
kill-plane, material, fade, spawner, bounce, gravity, air-resistance, chaoticity, and wind controls.
`particles_per_second` is accessor-backed. This confirms that smoke layers are separate generator
array entries, not fields within one emitter.

### 5.8 Engine live-particle nodes

The game runtime list nodes are unrelated to the SDK configuration attributes discussed in §5.4:

| Item | ATS 1.55 | ETS2 1.55 |
|---|---:|---:|
| Allocator | `0x1fb2e20` | `0x1f47fb0` |
| Allocated node size | `0x60` | `0x58` |
| Intrusive `next` / `previous` | `+0x00` / `+0x08` | `+0x00` / `+0x08` |
| Common state begins | `+0x10` | `+0x10` |
| Packed color | node `+0x50` | node `+0x54` |

Within the common state (offsets relative to node `+0x10`):

- `+0x00`: remaining lifetime, decremented each update.
- `+0x04`: alpha-attack/fade timer, also decremented each update.
- `+0x08`: rotation or animation accumulator; `+0x0c` is its rate.
- `+0x10` and `+0x14`: scalar size/animation components; exact names remain ambiguous.
- `+0x18..+0x20`: position-like vector integrated from the velocity-like vector at `+0x24..+0x2c`.
- `+0x30..+0x38`: secondary/interpolated position-like vector.
- ATS state `+0x3c` is an alpha/blend coefficient. ETS2 stores the corresponding coefficient at
  node `+0x50` and packed wrapping/tile coordinates at node `+0x4c`.

### 5.9 Exact 1.55 materializer counterparts

Linux class records identify constructor/materializer entry points corresponding to MODGuard's two
Windows materializer targets:

| Game | Class | Class record | Constructor | Destructor | Size |
|---|---|---:|---:|---:|---:|
| ATS | `model_particle` | `0x2b7ab00` | `0x13e2220` | `0x13e2340` | `0xf8` |
| ATS | `particle_gen` | `0x2d15520` | `0x1fb2ba0` | `0x1fada90` | `0x1a68` |
| ETS2 | `model_particle` | `0x2b03980` | `0x139a530` | `0x139a650` | `0xf8` |
| ETS2 | `particle_gen` | `0x2c99620` | `0x1f47bf0` | `0x1f43e20` | `0x1968` |

These are semantic counterparts, not ABI-compatible replacements for the Windows functions. Exact
Windows game VAs cannot be recovered from the DLL because it stores only runtime scan results; the
matching Windows game executables are required.

### 5.10 Disabling the hook

When `SmokeHook=0` the install is skipped. The setting can be changed at runtime via:
```
[MODGuard] Smoke hook next start: ENABLED
[MODGuard] Smoke hook next start: DISABLED
```
(later re-applied on next map/job load).

---

## 6. RealisticSmoke formula

### 6.1 Inputs
Per-frame, MODGuard reads:
- `throttle_smoothed`        (exponentially smoothed `effective.throttle`)
- `rpm_norm`                 (`engine.rpm / rpm.limit`, clamped [0, 1])
- `speed_norm`               (`speed / 30` m/s, clamped)
- `accel_norm`               (length of `local.acceleration.linear / 9.81`, clamped)
- `gear`                     (`displayed.gear`)
- `clutch_norm`              (`effective.clutch`)
- `cargo_mass_norm`          (`cargo.mass / 30000` kg)
- `cargo_loaded`             (`cargo.loaded`)
- `engine_enabled`           (`truck.engine.enabled`)

### 6.2 Per-input dynamics
- **Throttle**: envelope follower with `AttackSmoothness` (rise) and `ReleaseSmoothness` (fall).
  A transient detector compares against `ThrottleRiseThreshold`; overshoot yields `ThrottleTransientStrength * exp(-t / ThrottleTransientDecay)`.
- **RPM**: smooth via exponential moving average.
- **Speed**: low-pass.
- **Acceleration**: low-pass.
- **Gear**: step when `displayed.gear` changes; `ShiftWeight * (gear_transient)` for the next 0.3 s.
- **Launch**: detect zero-speed-to-throttle event; `LaunchWeight * exp(-t / 1.5)` for 3 s.
- **FreeRev**: detect engine on, no throttle, in neutral; `FreeRevWeight`.
- **Load**: `LoadSensitivity * cargo_mass_norm` while `cargo_loaded`.
- **Traction**: `TractionAccelerationWeight * accel_norm`.

### 6.3 Combined gain (partially recovered)

```c
float input = 0.0f;
float throttle_exponent = (ThrottleCurve == -1)
                        ? 1.0f
                        : 1.0f + (float)ThrottleCurve / 100.0f;
input += powf(throttle_smoothed, throttle_exponent)
       * SustainedThrottleStrength / 100.0f;
input += rpm_norm                                       * RpmWeight;
input += speed_norm                                     * SpeedWeight;
input += accel_norm                                     * AccelerationWeight;
input += gear_transient                                 * ShiftWeight;
input += clutch_norm                                    * ClutchWeight;
input += cargo_mass_norm * cargo_loaded                 * CargoWeight;
input += launch_envelope                                * LaunchWeight;
input += traction_accel                                 * TractionAccelerationWeight;
input += load_effort                                    * LoadEffortWeight;
input += free_rev_envelope                              * FreeRevWeight;
input += throttle_transient;  // calculated separately and added after the curved sustained term

float session_peak = max(session_peak * SessionPeakPercent, current);  // peak-hold
float output = input * Intensity * FinalSmokeMultiplier;
```

The internal contribution formula remains only partially recovered, but its final actuator is now
resolved. Function `0x18002e8f0` stores the raw result at output `+0x00` and a `[0,1]`-clamped result
at output `+0x04` (`0x180030481..0x18003049b`). FRAME_END reads the clamped value and packs it with
two eligibility bits into `0x1800aa1c0` (`0x180033ad6..0x180033b10`). The allocator hook converts
that float to a 24-bit admission threshold and probabilistically suppresses spawn requests, as
described in §5.3.

The `100/2500/10000` constants previously attributed to this formula belong to `rpm.limit`
validation in the SDK configuration callback (§5.4); they are not smoke-size classes.

### 6.4 Continuous smoke

If `ContinuousSmoke=1`, the hook stops the transient release and keeps smoke visible at a baseline
proportional to `Intensity` even with zero throttle.

---

## 7. License validation

### 7.1 Inputs
- User-supplied license key.
- Local MAC address (via `IPHLPAPI::GetAdaptersInfo`, formatted as `%02x%02x%02x%02x%02x%02x`).
- SteamID64 discovered from local Steam metadata.

### 7.2 Flow
```
1. Read MAC, format 12-char hex.
2. Discover SteamID64 from Steam metadata (exactly 17 decimal digits), in order:
     a. `<SteamRoot>/config/loginusers.vdf`, account marked `"MostRecent"`
     b. `<SteamRoot>/config/avatarcache/<SteamID64>.(png|jpg|jpeg)`
     c. `<SteamRoot>/config/coplay_<SteamID64>.vdf`
   SteamRoot comes from Valve registry keys, standard install paths, or HOME/XDG paths.
3. WinHttpOpen  ->  WinHttpConnect("leakingsv2.modguard.com.br")
                   -> WinHttpOpenRequest("GET",
                      "/key?key=<KEY>&clientID=<CLIENT>&steamID=<STEAMID>&data=<DATA>")
                   -> WinHttpSendRequest / WinHttpReceiveResponse
4. Read body, check for success pattern.
5. Cache result in process memory; the worker runs once and the frame callback polls its result.
6. On failure, the mod runs in "smoke-only, no license" mode (visual feedback still shown).
```

### 7.3 Status strings
```
[MODGuard] License key validated successfully.
[MODGuard] Visual feedback not activated: <reason>
[MODGuard] Smoke mod found and activated successfully.
[MODGuard] Smoke hook not activated: <reason>
```

### 7.4 Hardware fingerprint (Windows-specific)
- `%02x%02x%02x%02x%02x%02x` formatted MAC.
- Possible `IsDebuggerPresent` check (TLS callback).
- Volume serial number (GetAdaptersInfo fallback to `GetVolumeInformationW`).

---

## 8. Anti-debug / hardening

- TLS callbacks at `0x180050790` and `0x1800507f8` (called before DllMain).
- `IsDebuggerPresent` referenced (delay-loaded).
- The mod is code-signed by DigiCert / Sectigo, so integrity is enforced by Windows Authenticode on the
  Windows builds.
- Strings are stored as **UTF-16LE** in `.rdata` (config keys) to dodge naive `strings` analysis.

---

## 9. Function reference

| RVA         | Name                                 | Notes |
|-------------|--------------------------------------|-------|
| 0x359c0     | `scs_telemetry_init`                 | SDK entry |
| 0x362d0     | `scs_telemetry_shutdown`             | SDK entry |
| 0x002d00    | deferred init                        | logs license/smoke status |
| 0x002d9c    | log: license validated               | ref only |
| 0x002ddc    | log: smoke mod activated             | ref only |
| 0x002e46    | log: developer credits               | ref only |
| 0x003dc0    | game-id from PE substring search     | UTF-16LE scan |
| 0x003ff0    | game-id entry: GetModuleHandleW + scan | returns 1/2/0 |
| 0x031460    | `ContinuousSmoke` config parser      | not a hook installer |
| 0x032060    | worker-thread start                  | |
| 0x031f80    | worker-thread body                   | |
| 0x032190    | shutdown helper                      | |
| 0x031b80    | state save                           | |
| 0x0342c0    | telemetry channel registration       | calls `register_for_channel`, flags=3 |
| 0x034b23    | particle allocator hook call         | target observed from a live container |
| 0x034df0    | essential-hook discovery/install     | installs unit, model, and generator hooks |
| 0x034690    | FRAME_START event callback           | engine.enabled filter |
| 0x034c40    | FRAME_END event -> 0x033640          | main update loop (telemetry -> particle scale) |
| 0x033e90    | CONFIGURATION event callback         | reads `rpm.limit`, `cargo.mass`, `cargo.loaded` |
| 0x034550    | PAUSED event callback                | continuous smoke writer |
| 0x0345f0    | STARTED event callback               | visual feedback writer |
| 0x050790    | TLS callback 1                       | anti-debug / preload |
| 0x0507f8    | TLS callback 2                       | |
| 0x056adc    | key validator: HWID-based hash       | |
| 0x04d1ac    | SRWLock acquire                      | |
| 0x04d1b4    | SRWLock release                      | |
| 0x0503e4    | operator new                         | |
| 0x0503b0    | operator delete                      | |

Global state at `.data`:
| VA          | Purpose |
|-------------|---------|
| 0x1800aa170 | SDK log callback |
| 0x1800aa200 | `register_for_event` |
| 0x1800aa1a8 | `unregister_from_event` |
| 0x1800aa290 | `register_for_channel` |
| 0x1800aa2f8 | `unregister_from_channel` |

---

## 10. Linux reimplementation outline

### 10.1 Required environment
- Native Linux ETS2/ATS installation. A native plugin is an ELF `.so`; Proton is only relevant when
  running the Windows game build and its `.dll` plugins.

### 10.2 Components to port

1. **Native plugin .so** — implement `scs_telemetry_init` / `scs_telemetry_shutdown` against the Linux
   SCS SDK headers (the structs and channel names are identical). Drop into
   `<ETS2 or ATS>/bin/linux_x64/plugins/`.
2. **Smoke hook** — MinHook cannot be reused on Linux. The SCS Telemetry SDK does not expose a
   particle descriptor API, so shared memory alone cannot modify game particles. A faithful port
   requires either a version-specific native detour/interposition layer using the constructor and
   allocator targets in §5.9, or a data-only smoke mod whose parameters do not change per frame.
3. **RealisticSmoke formula** — port the recovered portion from §6. Its actuator is known: use the
   normalized output as particle-allocation admission probability. Some internal contribution terms
   still require further reverse engineering.
4. **Config loader** — replace `GetPrivateProfileStringW` with a plain C `inih` or hand-rolled
   parser reading `~/.local/share/<Game>/mod/<modname>.ini`.
5. **License validation** — replace WinHTTP with libcurl (or a small HTTP client over POSIX sockets).
   Endpoint: `https://leakingsv2.modguard.com.br/key?key=<KEY>&steamID=<STEAMID>`. Hardware fingerprint
   can use `getifaddrs()` (returns MAC addresses) instead of `IPHLPAPI`.
6. **Telemetry channels** — register identical channel names. The Linux SDK provides the same API
   surface as Windows (structs and `scs_register_for_channel` semantics).

### 10.3 Suggested file layout
```
modguard_linux/
├── CMakeLists.txt
├── src/
│   ├── plugin.c           # scs_telemetry_init / shutdown
│   ├── channels.c        # telemetry channel registration
│   ├── smoke_hook.c       # smoke particle logic and native detours
│   ├── config.c          # .ini reader/writer (UTF-8)
│   ├── license.c         # libcurl-based license check
│   ├── hwid.c            # MAC + fingerprint via getifaddrs
│   └── formula.c         # RealisticSmoke math (per-input envelopes)
└── README.md
```

### 10.4 Differences vs Windows
- No `MinHook`; use a Linux-native detour/interposition mechanism if runtime mutation is required.
- No code signing (Linux binaries don't use Authenticode; ignore DLL signature fields).
- TLS callbacks are not needed on Linux; `__attribute__((constructor))` can replace them if any.
- UTF-16LE config keys become UTF-8 (rewrite lookup tables).

---

## 11. Open questions / things still to reverse

1. ~~Exact bit layout of `model_particle` and `particle_gen` vtables~~ - **SUBSTANTIALLY RESOLVED**:
   RTTI establishes two separate hierarchies, and deleting destructors establish class sizes.
   `model_particle_u` is 0xf8 bytes in both games. ATS `particle_gen_u` is 0x1a68 and ETS2's is
   0x1968. Reflection descriptors now identify the direct generator fields. The previously cited
   0x40-byte records are SDK `scs_named_value_t` configuration attributes, not engine particles.
2. ~~The exact lambda-curve shape for `ThrottleCurve`~~ - **RESOLVED**: the UTF-16 parser stores an
   integer in the range -1..100 (default 45). Code at `0x18002f15e-0x18002f18e` computes exponent
   `1.0 + ThrottleCurve / 100.0`; -1 selects 1.0. It calls an internal `powf` implementation at
   `0x180079150`, then multiplies by `SustainedThrottleStrength / 100.0`.
3. ~~Steam-ID acquisition path~~ - **RESOLVED**: function `0x18003ecc0` discovers and caches an exact
   17-digit SteamID64 from `loginusers.vdf` (`"MostRecent"` first), then avatar-cache filenames, then
   `coplay_<id>.vdf`. Steam roots come from Valve registry keys, standard paths, and HOME/XDG paths.
   URL builder `0x180049880` appends `&clientID=`, `&steamID=`, and `&data=`.
4. ~~Worker-thread sleep interval and tick rate~~ - **RESOLVED**: worker thread runs once, exits.
   Main thread polls worker results every 300 frames in the FRAME_END callback (5-10 seconds).
5. ~~Detailed bitfield layout of `register_for_channel` flags~~ - **RESOLVED**: MODGuard registers
   both events and channels. Function `0x1800342c0` calls the saved `register_for_channel` pointer
   at `0x1800aa290` with index -1, the channel's SDK value type, flags=3
   (`EACH_FRAME | NO_VALUE`), a callback, and NULL context. The five calls at `0x180035c3d` onward
   separately register FRAME_START, FRAME_END, PAUSED, STARTED, and CONFIGURATION events.
6. ~~Generator field semantics and constructor/materializer targets~~ - **SUBSTANTIALLY RESOLVED FOR
   ATS/ETS2 1.55**: reflection maps direct fields in both games, and class records identify the four
   Linux constructors. Remaining unknowns are accessor backing fields (`angle_randomization` and ATS
   `particles_per_second`), exact names for several runtime animation scalars/vectors, the Linux
   generic unit-lookup counterpart, and concrete Windows target VAs without matching game EXEs.

### Q1 Final Answer (from ATS/ETS2 binary analysis)

**Confirmed class sizes and vtables** (ATS/ETS2 1.55.0.0):

| Class | Size | Vtable Address | Inherits From |
|-------|------|----------------|---------------|
| `prism::model_particle_u` | **0xf8** | ATS `0x2b7a998`; ETS2 `0x2b03818` | `particle_set_u` |
| `prism::particle_emitter_u` | **0x1b40** | VA `0x2d14708` | `particle_gen_u` |
| ATS `prism::particle_gen_u` | **0x1a68** | VA `0x2d15030` | `unit_t` |
| ETS2 `prism::particle_gen_u` | **0x1968** | VA `0x2c99170` | `unit_t` |
| ATS runtime particle node | **0x60** | - | intrusive list node |
| ETS2 runtime particle node | **0x58** | - | intrusive list node |

The unrelated 0x40-byte configuration attributes are standard SDK `scs_named_value_t` records; see
§5.4. They must not be overlaid on engine particle nodes.

**Key insight for ATS** (not in ETS2): `particle_emitter_u` derives from `particle_gen_u`. It does
not derive from `model_particle_u` and does not contain three generator sub-objects. In both games,
`model_particle_u` derives from `particle_set_u`, which owns an array of generator pointers:

- ATS: `model_particle_u : particle_set_u` -> owner-pointer array -> `particle_emitter_u : particle_gen_u`
- ETS2: `model_particle_u : particle_set_u` -> owner-pointer array -> `particle_gen_u`

MODGuard hooks generic unit lookup plus the `model_particle` and `particle_gen` materializers, then
derives a separate allocator target from a validated live container. This recognizes the generator
array regardless of the concrete ATS/ETS2 generator type.

---

## 12. Companion mods / package inventory

Author ToniVolma distributes MODGuard together with **truck-specific smoke packs** that install the
`model.particle.smoke_new` hookup into exhaust meshes:

| Mod | Trucks covered |
|---|---|
| **PM 389 Smoke** (analysed here) | Pizzster Modding Peterbilt 389 — and 162 PMGs across 17 other brands (Western Star 57x/5700XE/49x, Volvo VNL/VNL2018, Peterbilt 579, Kenworth W900/T680/T680 2022, Mack Pinnacle/Anthem, International LT/Lone Star/9900i, Freightliner Cascadia 2019/2024) |
| *Likely similar packs* | One per major truck brand with stock exhaust accessories |

The PM 389 Smoke pack contains:
- `unit/hookup/smoke_new.sii` (the SiiNunit binding MODGuard hooks into)
- `unit/hookup/smoke_new.sii.bak` (the original single-emitter definition, kept as backup)
- `model/particle/pipe.dds` + `pipe.tobj` (the new sprite texture, replaces vanilla `dirt` look)
- 162 `.pmg` files with the `model.particle.smoke_new` hookup string inserted into their string
  tables (the rest of the .pmg geometry is unchanged)

The PM 389 Smoke mod (and any sister mod) is a **mandatory prerequisite** for MODGuard: without
the `.sii` and `.pmg` hookup strings, MODGuard has nothing to hook and prints
`[MODGuard] Smoke hook not activated: ...`.

---

## 13. Resolution of original open questions

| # | Question | Status | Resolution |
|---|----------|--------|------------|
| 1 | `model_particle` / `particle_gen` layout | **Substantially resolved** | Separate RTTI hierarchies: `model_particle_u : particle_set_u` owns generator pointers; ATS has `particle_emitter_u : particle_gen_u`. Reflection maps the direct generator fields. The prior 0x40 record was SDK configuration data; native nodes are 0x60 (ATS) and 0x58 (ETS2). |
| 2 | `ThrottleCurve` shape | **Resolved** | Integer -1..100, default 45. Exponent is `1 + value/100`; -1 selects 1.0. Internal `powf` call at `0x180079150`, reached from `0x18002f18e`. |
| 3 | Steam-ID source | **Resolved** | Exact 17-digit SteamID64 from Steam's `loginusers.vdf` MostRecent account, then avatar-cache and `coplay_*.vdf` fallbacks. Discovery function `0x18003ecc0`; URL builder `0x180049880`. |
| 4 | License refresh interval | **Resolved** | User-thread (HTTP) thread runs ONCE then exits. The FRAME_END callback polls worker results every 300 frames (counter at `[rip + 0x757e0]`). At 60fps this is 5 seconds; at 30fps it's 10 seconds. |
| 5 | Telemetry value delivery | **Resolved** | Values arrive through callbacks passed to `register_for_channel`; with `NO_VALUE`, the callback also receives NULL when a channel is unavailable. The init struct ends at `unregister_from_channel` (+0x38) and has no next-value callbacks. |
| 6 | `register_for_channel` flags | **Resolved** | Calls at `0x1800342c0` use index -1 and flags=3 (`EACH_FRAME | NO_VALUE`). Five separate calls register SDK events at `0x180035c3d` onward. |
| 7 | Generator fields / materializer targets | **Substantially resolved for 1.55** | Reflection maps direct fields in both games. Linux class records identify ATS constructors `0x13e2220` / `0x1fb2ba0` and ETS2 constructors `0x139a530` / `0x1f47bf0`. MODGuard discovers Windows targets structurally, so fixed Windows VAs require matching game executables. |
