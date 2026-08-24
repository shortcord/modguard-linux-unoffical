# MODGuard Linux

This project is the first client-side milestone for a clean-room, native Linux reimplementation of MODGuard's exhaust-smoke support for ATS and ETS2.

It currently:

- loads as an official SCS Telemetry SDK plugin;
- supports native Linux ATS (`ats`) and ETS2 (`eut2`);
- collects throttle, RPM, speed, gear, clutch, engine state, and local acceleration with correctly typed SDK callbacks;
- reads RPM limit and cargo state from configuration events;
- reads the documented `[RealisticSmoke]` INI settings;
- computes a deterministic `exhaust_demand_v0` signal; and
- detours the ATS and ETS2 1.55 live-particle allocators for matching exhaust smoke generators; and
- publishes that signal as JSON to a non-blocking Unix datagram socket.

The hook is intentionally version-gated. It supports the analyzed native 1.55 executables: ETS2 SHA-256 `20f62d0b6aa6a75db360cd82d2a6fbd98ac6440ac48e6e4f863365d7f96acb6d` and ATS SHA-256 `24c8cbded3eec5b703b7c326b654d5a34d7df1d04a2a2460ac7cc7f826b4be18`. It checks the allocator and generator-constructor machine code before it changes memory. A fingerprint mismatch leaves all game code untouched.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The output is `build/modguard_linux.so`. The project vendors only the ABI subset of SCS Telemetry SDK 1.14 that it uses. Those definitions come from SCS Software's SDK. The file `LICENSE-SCS-SDK` retains its MIT license.

## Install

Copy `modguard_linux.so` into the native game's installation directory:

```text
<Steam library>/steamapps/common/American Truck Simulator/bin/linux_x64/plugins/
<Steam library>/steamapps/common/Euro Truck Simulator 2/bin/linux_x64/plugins/
```

This `.so` is for the native Linux game executable. It cannot load into a Windows game process running through Proton.

## Read Output

Before you launch the game, start a Unix datagram listener:

```bash
python3 tools/listen.py
```

The default socket is `/tmp/modguard-exhaust-<uid>.sock`. Set `MODGUARD_EXHAUST_SOCKET` in the game's environment to use another absolute path. Messages have this shape:

```json
{"schema":"exhaust_demand_v0","seq":42,"time_us":123456,"demand":0.83,"raw_demand":1.25,"session_peak":1.25,"throttle":0.70,"rpm":1450.0,"rpm_limit":2100.0,"speed":18.4,"gear":8,"engine":true,"cargo_loaded":true,"cargo_mass":18500.0,"hook_active":true,"smoke_requests":640,"admitted_requests":531}
```

The socket is diagnostic only. Particle control happens in-process, like the Windows plugin. Datagram loss is expected. It never blocks the game.

## Particle Hook

The backends detour the ETS2 allocator at `0x1f47fb0` or ATS allocator at `0x1fb2e20`. This happens only after they match its complete 16-byte body and the corresponding `particle_gen_u` constructor prefix. The replacement derives the owning generator from a constructor-populated registry. It checks the game-specific 1.55 vtables. It recognizes the companion mod's three documented layers by count, size, and lifetime. Non-matching generators always call the original allocator unchanged. Public-unit identity matching remains the next hardening step. A different mod that uses an identical layer definition can also match.

For matching layers, normalized demand becomes a deterministic 24-bit admission threshold. Rejected requests return `NULL`. Accepted requests call the game's original list allocator. Existing particles and particle fields are not changed. The plugin attempts to restore the original bytes during telemetry shutdown. If restoration fails, it keeps its trampoline mapped.

Multi-pipe setups emit consistently because admission is synchronized per layer. Each frame, every recognized smoke layer draws its decisions from a shared per-layer ordinal mixed with the frame number. Corresponding generators on different pipes receive the identical admission decision. They produce the same amount of smoke over time. Sprite position, rotation, lifetime, and size randomization remain independent. This preserves a natural appearance.

## Configuration

The plugin reads `modguard_linux.ini` from the game's `mod` directory:

```text
~/.local/share/American Truck Simulator/mod/modguard_linux.ini
~/.local/share/Euro Truck Simulator 2/mod/modguard_linux.ini
```

Set `MODGUARD_CONFIG` to an explicit file path to override this location. A fully commented template with every key and its default is at [`modguard_linux.ini.example`](modguard_linux.ini.example). Copy it to `modguard_linux.ini` in the applicable mod directory. Adjust the values as necessary.

The supported `[RealisticSmoke]` keys and defaults match `../reverse-enginnering/MODGuard_RE.md`. `SmokeHook=0` disables the in-process particle hook. `ContinuousSmoke=1` keeps a small baseline while the engine runs.

Load-specific keys (also under `[RealisticSmoke]`):

- `TruckMassKg` (default `8000`) curb weight of the truck.
- `TrailerMassKg` (default `6000`) weight of the trailer when attached.
- `LoadCurvePower` (default `1.8`) steepness of the gross-weight smoke curve.
- `LoadSensitivity` (default `1.0`) strength of the heavy-load top-end boost.
- `GrossWeightReferenceKg` (default `36000`) gross weight at which the load gate saturates.
- `LoadBaseline` (default `0.15`) smoke fraction for an unloaded, empty truck.
- `ThrottleGatePower` (default `1.5`) how strongly demand follows the throttle.
- `RpmLeanFraction` (default `0.6`) RPM fraction at which smoke reaches zero.
- `RpmLeanPower` (default `1.2`) sharpness of the high-RPM lean-out.
- `LoadBoostMin` / `LoadBoostMax` (default `0.6` / `2.6`) low and high ends of the heavy-load emission boost.

## Model Scope

`exhaust_demand_v0` applies the recovered throttle exponent `1 + ThrottleCurve / 100`, time-based attack/release smoothing, and documented terms. These terms cover RPM, speed, acceleration, shift, launch, clutch, cargo, load-effort, and free-rev. `demand` is clamped to `[0,1]` for particle admission. `raw_demand` is retained for diagnostics. Inputs are clamped to safe ranges. Engine-off output is zero.

Gross weight gates the smoke. Total weight (truck curb mass plus trailer and `cargo.mass` when `cargo.loaded`) is normalized and raised to `LoadCurvePower` to form a load gate. A light, unloaded truck stays near a faint baseline. A heavy, loaded combination can reach high emission. The demand is also scaled by throttle and concentrated at low RPM. Diesel smoke represents over-fueling. Over-fueling happens mainly at low RPM under high throttle. The plume leans out sharply as engine speed rises through the mid and high end. `LoadSensitivity` widens the gap. A heavily loaded truck under load smokes substantially more than the baseline.

The SCS Telemetry SDK exposes no direct vehicle-weight channel. Truck and empty-trailer weight are configured statically. Cargo is read from telemetry.

No 100/2500/10000 particle bucket is emitted. Those values are the original plugin's RPM-limit minimum, default, and maximum. They are not a smoke-output scale.

The canonical research reference is `../reverse-enginnering/MODGuard_RE.md`. Files under `../docs/` contain earlier hypotheses. They may not include later corrections. The current implementation is documented in:

- [`../docs/linux-plugin-architecture.md`](../docs/linux-plugin-architecture.md) — plugin lifecycle, events, channels, diagnostics, build, and tests.
- [`../docs/linux-exhaust-model.md`](../docs/linux-exhaust-model.md) — the demand model and the full configuration table.
- [`../docs/linux-particle-hook.md`](../docs/linux-particle-hook.md) — the inline game-memory hook and admission sampler.
- [`../docs/mod-smoke-variants.md`](../docs/mod-smoke-variants.md) — the clean smoke-mod presets and packaging.