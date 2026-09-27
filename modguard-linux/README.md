# MODGuard Linux

This project is the first client-side milestone for a clean-room, native Linux reimplementation of MODGuard's exhaust-smoke support for ATS and ETS2.

It currently:

- loads as an official SCS Telemetry SDK plugin;
- supports native Linux ATS (`ats`) and ETS2 (`eut2`);
- collects throttle, RPM, speed, gear, clutch, engine state, and local acceleration with correctly typed SDK callbacks;
- reads RPM limit and cargo state from configuration events;
- reads the documented `[RealisticSmoke]` INI settings;
- computes a deterministic `exhaust_demand_v0` signal; and
- detours the supported ATS and ETS2 particle spawn paths for matching exhaust smoke generators; and
- publishes that signal as JSON to a non-blocking Unix datagram socket.

The hook is fingerprint-gated. Active profiles cover ATS 1.60.1.8s, ATS 1.61.1.4s, ATS 1.61.2.0s, ETS2 1.60.1.7s, and ETS2 1.61.1.1s. The ATS 1.60.1.8s SHA-256 is `24c8cbded3eec5b703b7c326b654d5a34d7df1d04a2a2460ac7cc7f826b4be18`. Its ELF build ID is `e109d085d57bbeca9a35c7a61df9703796568fe3`. The ATS 1.61.1.4s SHA-256 is `c2d1b3d2eace307b68d4a8f945ef0fc35b6d334a68377a635e35170b0ddb1d37`. Its ELF build ID is `bbad4aec77909880502d055c9e96d2fe23ec153f`. The ATS 1.61.2.0s SHA-256 is `9e6e82a56e045736558def1cb877b184ddc666c90af40c11667766d357c7a4a3`. Its ELF build ID is `e515a3ae857a3a84319d3579b94ba3acbe31edb5`. The ETS2 1.61.1.1s SHA-256 is `16659c0454622cb9718f8cbfc08af198f7143632f8a01c55f8620278e2d43bac`. Its ELF build ID is `d2cb5aa082c669c8f5744ae329862f954dec71c2`. Every active profile is pinned. A pinned profile requires the exact game ID and ELF build ID. The hook checks the machine code before it changes memory. A fingerprint mismatch leaves all game code unchanged. A different build of the same game version is not supported until a profile is added for it.

The 1.55 `allocator_trampoline` profiles are disabled. The 1.55 games read the node that the patched allocator returns without a NULL check in list-unlink paths. Suppression by returning `nullptr` crashes the game. On 1.55 installs the plugin runs telemetry and diagnostics only; no particles are controlled. See `../docs/linux-particle-hook.md` for the crash evidence.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The output is `build/modguard_linux.so`. The project vendors only the ABI subset of SCS Telemetry SDK 1.14 that it uses. Those definitions come from SCS Software's SDK. The file `LICENSE-SCS-SDK` retains its MIT license.

The build also creates `build/modguard_check`. Run it with a native Linux game binary:

```bash
build/modguard_check "/path/to/game"
```

It uses the same ELF build-ID and machine-code checks as the plugin. It exits with `0` for a supported binary, `1` for an unsupported binary, and `2` when it cannot read the file or the command line is invalid.

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
{"schema":"exhaust_demand_v0","seq":42,"time_us":123456,"demand":0.83,"raw_demand":1.25,"session_peak":1.25,"throttle":0.70,"rpm":1450.0,"rpm_limit":2100.0,"speed":18.4,"gear":8,"engine":true,"cargo_loaded":true,"cargo_mass":18500.0,"hook_active":true,"particle_constructors":12,"particle_candidates":720,"particle_registered":640,"smoke_requests":600,"admitted_requests":531,"suppressed_requests":69,"dynamic_low_requests":0,"dynamic_low_admitted":0,"dynamic_medium_requests":0,"dynamic_medium_admitted":0,"dynamic_high_requests":0,"dynamic_high_admitted":0}
```

The socket is diagnostic only. Particle control happens in-process, like the Windows plugin. Datagram loss is expected. It never blocks the game.

The particle counters are cumulative for the current hook installation. They show constructors, candidate calls, registry matches, smoke requests, admissions, and suppressions. The first counter that stays at zero narrows the failed hook stage. The registry contains all particle generators, not only exhaust generators.

The `dynamic_*` counters are nonzero only when the `dynamic-blend` preset is active. They show the request and admission counts for the low, medium, and high appearance bands. Use them to make sure that all three bands request particles at a similar rate.



## Particle Hook

The ATS 1.60.1.8s backend detours the spawn function at `0x1fadac0`. The ATS 1.61.1.4s backend detours the list allocator at `0x1cbfea0`. The ATS 1.61.2.0s backend detours the list allocator at `0x1cc1310`. The ETS2 1.60.1.7s backend detours the spawn function at `0x1fa8ab0`. The ETS2 1.61.1.1s backend detours the list allocator at `0x1cb3d40`. Each backend checks the complete patch bytes and the applicable `particle_gen_u` constructor prefix. The replacement uses a constructor-populated registry to find the generator. The registry caches the smoke identity and the original lifetime at construction. The hook checks the game-specific vtable. It recognizes the companion mod's three documented layers by count, size, and lifetime. Non-matching generators always use the original game function.

For matching layers, normalized demand becomes a deterministic 24-bit admission threshold. Rejected spawn calls return without spawning. Accepted calls reach the game's original spawn function. Existing particles and particle fields are not changed. The plugin attempts to restore the original bytes during telemetry shutdown. If restoration fails, it keeps its trampoline mapped.

Multi-pipe setups emit consistently because admission is synchronized per identity. Each frame, every recognized smoke generator draws its decisions from a shared per-identity ordinal mixed with the frame number. Corresponding generators on different pipes receive the identical admission decision. They produce the same amount of smoke over time. Sprite position, rotation, lifetime, and size randomization remain independent. This preserves a natural appearance.

### Size Blending

The `dynamic-blend` preset defines three appearance bands: small and light, default, and large and dark. All three bands share one particle count and one lifetime, so they request particles at the same rate. Only the base size differs. The plugin maps the smoke demand to three band weights. The weights sum to one, so the demanded emission is split across the bands and the total emission does not change.

Below half demand, the smoke uses the small and default bands. Above half demand, it uses the default and large bands. The result is a smooth change from light to dark and from small to large as the engine works harder. Existing particles are not changed. They expire by lifetime, so the transition is natural.

The legacy three-layer presets (`soft-haze`, `subtle`, `dark-long`) keep the original behavior.

## Configuration

The plugin reads `modguard_linux.ini` from the game's `mod` directory:

```text
~/.local/share/American Truck Simulator/mod/modguard_linux.ini
~/.local/share/Euro Truck Simulator 2/mod/modguard_linux.ini
```

Set `MODGUARD_CONFIG` to an explicit file path to override this location. A fully commented template with every key and its default is at [`modguard_linux.ini.example`](modguard_linux.ini.example). Copy it to `modguard_linux.ini` in the applicable mod directory. Adjust the values as necessary.

You can change the configuration while the game runs. Edit `modguard_linux.ini` and save it. The plugin applies the new values on the next simulation frame. You do not restart the game. A normal save, an atomic rename save, and a `touch` all trigger a reload. If the file is missing or invalid, the plugin keeps the previous configuration. If a single value is invalid, the plugin uses the default for that key. See `../docs/linux-plugin-architecture.md` section 6.2 for the reload contract.

The supported `[RealisticSmoke]` keys and defaults match `../reverse-enginnering/MODGuard_RE.md`. `SmokeHook=0` disables the in-process particle hook. `ContinuousSmoke=1` keeps a small baseline while the engine runs.

Load-specific keys (also under `[RealisticSmoke]`):

- `TruckMassKg` (default `8000`) curb weight of the truck.
- `TrailerMassKg` (default `6000`) weight of the trailer when attached.
- `LoadCurvePower` (default `1.8`) steepness of the gross-weight smoke curve.
- `LoadSensitivity` (default `1.0`) strength of the heavy-load top-end boost.
- `GrossWeightReferenceKg` (default `36000`) gross weight at which the load gate saturates.
- `LoadBaseline` (default `0.15`) smoke fraction for an unloaded, empty truck.
- `ThrottleGatePower` (default `1.5`) how strongly demand follows the throttle.
- `LoadBoostMin` / `LoadBoostMax` (default `0.6` / `2.6`) low and high ends of the heavy-load emission boost.

Weighted additive terms from the Windows formula (also under `[RealisticSmoke]`):

- `ThrottleCurve` (default `45`) exponent `1 + value/100` of the curved-throttle term. `-1` selects `1.0`.
- `RpmWeight`, `SpeedWeight`, `AccelerationWeight`, `ClutchWeight`, `CargoWeight`, `TractionAccelerationWeight` (default `1.0` each) weights of the per-input terms.
- `LoadEffortWeight` (default `1.0`) weight of the computed fuel-to-air demand. Hypothesis mapping.
- `FreeRevWeight` (default `1.0`) weight of the free-rev envelope (engine on, no throttle, neutral).
- `SustainedThrottleStrength` (default `0.5`) scales the curved-throttle term and gates the sustained-throttle event.
- `EventStrength` (default `1.0`) weight of the sustained-throttle event. Hypothesis.

## Model Scope

`exhaust_demand_v0` applies the recovered throttle exponent `1 + ThrottleCurve / 100`, time-based attack/release smoothing, and the recovered weighted terms. These terms cover RPM, speed, acceleration, shift, launch, clutch, cargo, load-effort, and free-rev. The model adds the weighted terms before its own gates. At zero throttle the gates keep the demand at zero, so a free-revving idle stays clean. `demand` is clamped to `[0,1]` for particle admission. `raw_demand` is retained for diagnostics. Inputs are clamped to safe ranges. Engine-off output is zero.

Gross weight gates the smoke. Total weight (truck curb mass plus trailer and `cargo.mass` when `cargo.loaded`) is normalized and raised to `LoadCurvePower` to form a load gate. A light, unloaded truck stays near a faint baseline. A heavy, loaded combination can reach high emission. The demand is also scaled by throttle. `LoadSensitivity` widens the gap. A heavily loaded truck under load smokes substantially more than the baseline.

The SCS Telemetry SDK exposes no direct vehicle-weight channel. Truck and empty-trailer weight are configured statically. Cargo is read from telemetry.

No 100/2500/10000 particle bucket is emitted. Those values are the original plugin's RPM-limit minimum, default, and maximum. They are not a smoke-output scale.

The canonical research reference is `../reverse-enginnering/MODGuard_RE.md`. Files under `../docs/` contain earlier hypotheses. They may not include later corrections. The current implementation is documented in:

- [`../docs/linux-plugin-architecture.md`](../docs/linux-plugin-architecture.md) — plugin lifecycle, events, channels, configuration reload, diagnostics, build, and tests.
- [`../docs/linux-exhaust-model.md`](../docs/linux-exhaust-model.md) — the demand model and the full configuration table.
- [`../docs/linux-particle-hook.md`](../docs/linux-particle-hook.md) — the inline game-memory hook and admission sampler.
- [`../docs/mod-smoke-variants.md`](../docs/mod-smoke-variants.md) — the clean smoke-mod presets and packaging.
