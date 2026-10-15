# MODGuard Research and Linux Reimplementation

This repository documents `MODGuard.dll`. It also contains a native Linux reimplementation for American Truck Simulator (ATS) and Euro Truck Simulator 2 (ETS2).

MODGuard is an SCS Telemetry SDK plugin. It controls the companion RealisticSmoke particle effect. The Linux plugin reads truck telemetry, calculates exhaust demand, and controls matching smoke-particle requests in the game process.

The repository also contains the analyzed Windows binary and the companion mod. These are research inputs. They stay proprietary. The SCS SDK license does not cover them.

## Supported Game Versions

| Game | Supported versions |
| --- | --- |
| American Truck Simulator | 1.55, 1.60.1.8s, 1.61.1.4s, 1.61.2.0s, 1.61.3.0s, 1.61.3.1s |
| Euro Truck Simulator 2 | 1.55, 1.60.1.7s, 1.61.1.1s |

> **Why the version matters.** The plugin writes a jump instruction into the game code in memory. The location and the exact bytes differ between game versions. The plugin checks the game identity and the bytes at the target address before it changes anything. If the game version or the bytes do not match, the plugin does nothing and the smoke effect stays in its default state. A new game version can move or change the target code. When that happens, the plugin cannot attach until a new profile is added for that version.

## Reasoning

MODGuard is a DRM platform for paid mods in ATS and ETS2. The only mod on the platform, from what I can tell, is a "Realistic Exhaust Smoke" mod.

This repo does not try to break the platform DRM. It reimplements only the smoke effect.

<small>Realistic exhaust smoke is wanted in ETS/ATS2. To make it not cross-platform is not acceptable.</small>

Compatibility with the official MODGuard plugin is limited to the smoke effect. The original client-side mod currently works but this will not be guaranteed in the future.

## Overview

The plugin and the mod work together to make truck exhaust smoke look real in the game.

The mod is a small package of game files. It tells the game how to draw the smoke. It sets the smoke color, shape, size, and how long each puff of smoke stays in the air.

The plugin is a small program that runs inside the game while you play. The game tells the plugin what the truck is doing. This includes the engine speed, the throttle, the gear, the speed, and the load. The plugin uses this data to decide how much smoke the truck should make. When the engine works hard, the plugin tells the game to make more smoke. When the engine idles, the plugin tells the game to make less smoke.

The game draws the smoke as a stream of small pictures called particles. Each particle is one puff of smoke. The game makes these particles all the time while the engine runs. The plugin does not draw the smoke itself. Instead, the plugin intercepts the game at the moment it is about to spawn a particle. The plugin reads the current truck data and then decides: let the game spawn this particle, or block it. In this way, the plugin thins out the stream of particles so the smoke matches the engine load.

The plugin recognizes three smoke layers. These are haze, mid, and core. Each layer has a different particle count, sprite size, and lifetime. The mod defines these layers. The plugin reads the same values to identify them. When the game asks for a particle, the plugin first checks which layer the request belongs to. If the request is not a known smoke layer, the plugin lets the game proceed unchanged. One preset, `dynamic-blend`, uses three size bands instead of the three plume layers. The plugin then blends the bands by load. See `docs/linux-particle-hook.md` for the identity table.

The decision uses a demand value between 0 and 1. The exhaust model calculates this value from the truck telemetry. A value of 0 blocks all particles for that request. A value of 1 lets all particles through. A value in between admits a fraction of them. To keep the decision stable across pipes, the plugin uses a deterministic sampler. For each frame, each smoke identity, and each request in order, the sampler produces a number. If the number falls below the threshold set by the demand, the particle is admitted. Matching pipes on different exhausts receive the same decision for the same identity and ordinal. This keeps multi-pipe trucks consistent. Each pipe admits the same fraction of smoke over time.

To do this, the plugin replaces a small part of the game code in memory while the game runs. The plugin writes a jump instruction at the entry of the game's particle spawn function. When the game reaches that point, the jump sends control to the plugin callback. The plugin makes its decision and then calls the original game code to finish the request. Before the plugin changes anything, it checks the game identity and the exact bytes at the target address. If the game does not match, or the bytes differ, the plugin does nothing and the game keeps its normal behavior. This keeps the game safe. When the plugin shuts down, it restores the original bytes.

Without the plugin, the mod makes the same smoke all the time. With the plugin, the smoke changes with the way you drive.

## Status

The Linux implementation gives these functions:

- Native Linux SCS Telemetry SDK integration for ATS and ETS2.
- A configurable exhaust-demand model. The model uses throttle, RPM, speed, gear, clutch, cargo, and acceleration.
- An in-process particle hook with version and machine-code checks.
- Active hook profiles for ATS 1.60.1.8s, ATS 1.61.1.4s, ATS 1.61.2.0s, ATS 1.61.3.0s, ATS 1.61.3.1s, ETS2 1.60.1.7s, and ETS2 1.61.1.1s. The 1.55 allocator profiles are disabled. Every active profile is pinned to an exact ELF build ID.
- Synchronized multi-pipe emission. Matching smoke identities on different pipes get identical admission decisions.
- Non-blocking JSON diagnostics through a Unix datagram socket.
- Four clean smoke-mod presets. The presets are soft haze, subtle modern exhaust, dark long smoke, and dynamic size blend. A CMake packaging system builds them.
- Unit tests for the model, the configuration parser, the plugin ABI, the particle signatures, the blend weights, and the identity admission.

The plugin works only with native Linux game executables. It cannot load into a Windows game process under Proton.

Particle-hook support is fail closed. If a game version or machine-code signature does not match, the plugin leaves the game code unchanged.

> **Important Note.** If you install the mod and the smoke is constant regardless of if your engine is on or not, then you either forgot the plugin **or** are running an older/unsupported game version.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `reverse-enginnering/MODGuard_RE.md` | Canonical reverse-engineering specification and current research conclusions. |
| `docs/` | Detailed analysis, porting notes, historical research, and implementation documentation. |
| `docs/README.md` | Index of research and implementation documents. |
| `docs/linux-plugin-architecture.md` | Native Linux plugin architecture, lifecycle, configuration reload, diagnostics, build, and tests. |
| `docs/linux-exhaust-model.md` | `exhaust_demand_v0` model and the full `[RealisticSmoke]` configuration table. |
| `docs/linux-particle-hook.md` | In-process particle hook, profiles, classifiers, and admission sampler. |
| `docs/mod-smoke-variants.md` | Clean smoke-mod presets and packaging. |
| `docs/artefacts/` | Scripts, disassembly output, and raw research notes. |
| `reverse-enginnering/MODGuard.dll` | Analyzed proprietary Windows plugin. |
| `reverse-enginnering/PM_389_Smoke.zip` | Proprietary companion smoke mod used for analysis. |
| `smoke-mod/_shared/` | Extracted `PM_389_Smoke.zip` tree. Shared base the variants overlay. The `vehicle/truck/` subtree is reorganized by source into `base_game/` and `workshop/` subdirectories. The packaging script flattens these back to the SCS-expected flat layout. |
| `smoke-mod/truck_sources.toml` | Sidecar index that maps each truck directory to `base_game` or `workshop`, with evidence and status. |
| `smoke-mod/soft-haze/`, `smoke-mod/subtle/`, `smoke-mod/dark-long/`, `smoke-mod/dynamic-blend/` | Clean replacement particle definitions and sprite texture for each preset. The overlay is mesh-capable. |
| `smoke-mod/README.md` | Operator-facing build and install instructions for the smoke-mod presets. |
| `smoke-mod/CMakeLists.txt` | Self-contained CMake packaging system for the smoke-mod variants. |
| `smoke-mod/build-cmake/` | CMake build tree for the packaged smoke-mod variant archives. This is build output, not committed. |
| `modguard-linux/` | Native Linux plugin source, tests, and tools. |
| `modguard-linux/modguard_linux.ini.example` | Commented configuration template for the Linux plugin. |

`reverse-enginnering/MODGuard_RE.md` is the primary research reference. Some files in `docs/` contain earlier hypotheses. Later research corrected these hypotheses.

## Requirements

- Linux on x86-64
- CMake 3.16 or newer
- A C++17 compiler
- A native Linux installation of ATS or ETS2 for in-game use
- Python 3 for the optional diagnostic listener

The project vendors the required subset of the SCS Telemetry SDK ABI. The plugin has no network dependency.

## Build

Run these commands from the repository root:

```bash
cmake -S modguard-linux -B modguard-linux/build -DCMAKE_BUILD_TYPE=Release
cmake --build modguard-linux/build --parallel
ctest --test-dir modguard-linux/build --output-on-failure
```

The build creates `modguard-linux/build/modguard_linux.so`.

The build also creates `modguard-linux/build/modguard_check`. Give this utility a native Linux game binary to report its support status:

```bash
modguard-linux/build/modguard_check "/path/to/game"
```

The utility uses the same ELF build-ID and machine-code checks as the plugin.

## Install

If the game installation does not contain the plugin directory, create it. Then copy `modguard_linux.so` to the applicable directory:

```text
<Steam library>/steamapps/common/American Truck Simulator/bin/linux_x64/plugins/
<Steam library>/steamapps/common/Euro Truck Simulator 2/bin/linux_x64/plugins/
```

Install and enable the companion smoke mod separately through the game mod manager. The Linux plugin does not install or modify the companion mod.

After startup, read the game log. The plugin reports whether the telemetry client loaded. The plugin reports whether the particle hook passed its checks.

## Smoke Mod Variants

The companion mod `PM_389_Smoke.zip` is read-only proprietary evidence. Its extracted tree lives under `smoke-mod/_shared/`. The `vehicle/truck/` subtree is reorganized by source into `base_game/` and `workshop/` subdirectories. The packaging script flattens these back to the SCS-expected flat `/vehicle/truck/<truck>/` layout in the shipped archive. The original `PM_389_Smoke.zip` in `reverse-enginnering/` is the verbatim evidence copy.

Four clean visual presets are provided as source under `smoke-mod/`. They are repackaged into playable mod archives:

- **soft-haze**: a soft natural haze with feathered sprite edges, wider lateral drift, gentle fade-in, and lowered per-sprite opacity. Overlapping sprites form the plume.
- **subtle**: a lighter, modern exhaust with reduced opacity, gentler growth, and less visual persistence. It becomes visible mainly under load.
- **dark-long**: a heavy, dark smoke that lingers in the air. For a 1-minute layer-0 lifetime, set `LifetimeMultiplier=20` in the plugin configuration.
- **dynamic-blend**: smoke that grows and darkens as engine demand rises. It uses three appearance bands that share one count and one lifetime. The plugin weights the bands by demand, so the total emission does not change. It needs the updated plugin.

The three legacy presets keep the documented generator layer signatures. These are count, size, and lifetime. The Linux particle hook still recognizes them. The `dynamic-blend` preset uses a separate signature set. The presets also replace the smoke sprite with a feathered, edge-free soft cloud texture. This texture avoids obvious billboard boundaries. The texture keeps the same DX10 `BC3_UNORM_SRGB` format and the original `model/particle/pipe.dds` path.

The overlay system is mesh-capable. A preset can also replace `.pmg`/`.pmd`/`.pmc` files at matching relative paths. The build enforces a `.pmg`+`.pmd` pair rule and fails closed on a mismatch.

Build the packaged archives:

```bash
cmake -S smoke-mod -B smoke-mod/build-cmake
cmake --build smoke-mod/build-cmake --target smoke_mod_variants
```

This copies the shared tree, flattens the source split, removes `.bak` files, overlays the variant files, and writes four archives. The archives are `smoke-mod/build-cmake/PM_389_Smoke_<variant>.zip`. Enable one of these in the game mod manager.

For operator-facing build and install instructions, read [`smoke-mod/README.md`](smoke-mod/README.md). For the implementation reference, read [`docs/mod-smoke-variants.md`](docs/mod-smoke-variants.md).

Do the final visual tuning in game. The repository cannot reproduce the SCS particle renderer.

## Configuration

The plugin reads `modguard_linux.ini` from the applicable game mod directory:

```text
~/.local/share/American Truck Simulator/mod/modguard_linux.ini
~/.local/share/Euro Truck Simulator 2/mod/modguard_linux.ini
```

Set `MODGUARD_CONFIG` to use an explicit file path. The file uses the `[RealisticSmoke]` section:

```ini
[RealisticSmoke]
SmokeHook=1
ContinuousSmoke=0
Intensity=0.3
FinalSmokeMultiplier=0.8
ThrottleCurve=45
```

Missing and invalid values use safe defaults. `SmokeHook=0` disables in-process particle control. Telemetry and diagnostic output stay active.

A fully commented template is at [`modguard-linux/modguard_linux.ini.example`](modguard-linux/modguard_linux.ini.example). It contains every key, its default, and its sanitize range. Copy it to `modguard_linux.ini` in the applicable mod directory. Adjust the values as necessary.

Read [`modguard-linux/README.md`](modguard-linux/README.md) for plugin behavior and model details. Read [`reverse-enginnering/MODGuard_RE.md`](reverse-enginnering/MODGuard_RE.md#4-configuration-ini-file) for the complete schema. Read [`docs/linux-exhaust-model.md`](docs/linux-exhaust-model.md) for the full configuration table and the demand model reference.

## Diagnostics

Before you start the game, start the listener:

```bash
python3 modguard-linux/tools/listen.py
```

The listener binds to `/tmp/modguard-exhaust-<uid>.sock` by default. Set `MODGUARD_EXHAUST_SOCKET` to an absolute path to select a different socket.

The plugin sends `exhaust_demand_v0` JSON datagrams at most ten times each second. This output is diagnostic only. Missing datagrams do not affect in-process particle control.

## Research Notes

The canonical specification records the source binary hash, the recovered SDK behavior, the configuration schema, the formulas, the particle layouts, and the game addresses:

- [`reverse-enginnering/MODGuard_RE.md`](reverse-enginnering/MODGuard_RE.md): current consolidated specification.
- [`docs/README.md`](docs/README.md): index of detailed research and implementation documents.
- [`docs/artefacts/README.md`](docs/artefacts/README.md): raw artefact descriptions and reproduction tools.

Implementation documentation for the current Linux plugin and the smoke-mod presets:

- [`docs/linux-plugin-architecture.md`](docs/linux-plugin-architecture.md): plugin lifecycle, events, channels, configuration reload, diagnostics, build, and tests.
- [`docs/linux-exhaust-model.md`](docs/linux-exhaust-model.md): demand model and full configuration table.
- [`docs/linux-particle-hook.md`](docs/linux-particle-hook.md): inline game-memory hook and admission sampler.
- [`docs/mod-smoke-variants.md`](docs/mod-smoke-variants.md): clean smoke-mod presets and packaging.

Addresses and object layouts are specific to the analyzed game executables. Do not apply them to another release without new binary evidence.

## Legal Notice

`MODGuard.dll`, `PM_389_Smoke.zip`, and the extracted companion-mod files belong to their respective rights holders. Their presence supports authorized interoperability research.

The clean-room Linux implementation is separate source code. `modguard-linux/LICENSE-SCS-SDK` covers only the vendored SCS SDK definitions.

This repository does not give a license for the remaining Linux source at this time. Copyright law reserves all rights until the maintainers add one.

## AI Usage Disclosure
Most reverse enginnering and subsequent c++ development was done with the assistance of AI tooling.
