# MODGuard Research and Linux Reimplementation

This repository documents `MODGuard.dll` and contains a native Linux reimplementation for American Truck Simulator (ATS) and Euro Truck Simulator 2 (ETS2).

MODGuard is an SCS Telemetry SDK plugin that controls the companion RealisticSmoke particle effect. The Linux plugin reads truck telemetry, calculates exhaust demand, and controls matching smoke-particle requests in the game process.

The repository also contains the analyzed Windows binary and companion mod as research inputs. They remain proprietary. They are not covered by the SCS SDK license.

## Status

The Linux implementation currently provides:

- Native Linux SCS Telemetry SDK integration for ATS and ETS2.
- A configurable exhaust-demand model based on throttle, RPM, speed, gear, clutch, cargo, and acceleration.
- An in-process particle hook with version and machine-code checks.
- Hook profiles for ATS 1.55, ATS 1.60, and ETS2 1.55.
- Synchronized multi-pipe emission. Matching smoke layers on different pipes receive identical admission decisions.
- Non-blocking JSON diagnostics through a Unix datagram socket.
- Two clean smoke-mod presets (soft haze and subtle modern exhaust) with a packaging tool.
- Unit tests for the model, configuration parser, plugin ABI behavior, particle signatures, and layer admission.

The plugin is only for native Linux game executables. It cannot load into a Windows game process under Proton.

Particle-hook support is fail closed. If a game version or machine-code signature does not match, the plugin leaves the game code unchanged.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `reverse-enginnering/MODGuard_RE.md` | Canonical reverse-engineering specification and current research conclusions. |
| `docs/` | Detailed analysis, porting notes, historical research, and implementation documentation. |
| `docs/README.md` | Index of research and implementation documents. |
| `docs/linux-plugin-architecture.md` | Native Linux plugin architecture, lifecycle, diagnostics, build, and tests. |
| `docs/linux-exhaust-model.md` | `exhaust_demand_v0` model and the full `[RealisticSmoke]` configuration table. |
| `docs/linux-particle-hook.md` | In-process particle hook, profiles, classifiers, and admission sampler. |
| `docs/mod-smoke-variants.md` | Clean smoke-mod presets and packaging. |
| `docs/artefacts/` | Scripts, disassembly output, and raw research notes. |
| `reverse-enginnering/MODGuard.dll` | Analyzed proprietary Windows plugin. |
| `reverse-enginnering/PM_389_Smoke.zip` | Proprietary companion smoke mod used for analysis. |
| `smoke-mod/_shared/` | Extracted `PM_389_Smoke.zip` tree. Shared base the variants overlay. Kept verbatim. |
| `smoke-mod/soft-haze/`, `smoke-mod/subtle/` | Clean replacement particle definitions and sprite texture for each preset. |
| `smoke-mod/build/` | Packaged smoke-mod variant archives (build output, not committed). |
| `tools/` | Helper scripts, including the smoke-variant builder. |
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

## Install

If the game installation does not contain the plugin directory, create it. Then copy `modguard_linux.so` to the applicable directory:

```text
<Steam library>/steamapps/common/American Truck Simulator/bin/linux_x64/plugins/
<Steam library>/steamapps/common/Euro Truck Simulator 2/bin/linux_x64/plugins/
```

Install and enable the companion smoke mod separately through the game mod manager. The Linux plugin does not install or modify the companion mod.

After startup, check the game log. The plugin reports whether the telemetry client loaded. The plugin reports whether the particle hook passed its checks.

## Smoke Mod Variants

The companion mod `PM_389_Smoke.zip` is read-only proprietary evidence. It is never modified. Its extracted tree lives under `smoke-mod/_shared/` and is kept verbatim. Two clean visual presets are provided as source under `smoke-mod/`. They are repackaged into playable mod archives:

- **soft-haze**: a soft natural haze with feathered sprite edges, wider lateral drift, gentle fade-in, and lowered per-sprite opacity. Overlapping sprites form the plume.
- **subtle**: a lighter, modern exhaust with reduced opacity, gentler growth, and less visual persistence. It becomes visible mainly under load.

Both presets keep the documented generator layer signatures (count, size, lifetime). The Linux particle hook still recognizes them. They also replace the smoke sprite with a feathered, edge-free soft cloud texture. This texture avoids obvious billboard boundaries. The texture keeps the same DX10 `BC3_UNORM_SRGB` format and the original `model/particle/pipe.dds` path.

Build the packaged archives:

```bash
bash tools/build-smoke-variants.sh
```

This copies the shared tree, overlays the variant's particle definition and sprite, and writes `smoke-mod/build/PM_389_Smoke_soft-haze.zip` and `smoke-mod/build/PM_389_Smoke_subtle.zip`. Enable one of these in the game mod manager.

Check the final visual tuning in game. The repository cannot reproduce the SCS particle renderer.

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

See [`modguard-linux/README.md`](modguard-linux/README.md) for plugin behavior and model details. See [`reverse-enginnering/MODGuard_RE.md`](reverse-enginnering/MODGuard_RE.md#4-configuration-ini-file) for the complete schema. See [`docs/linux-exhaust-model.md`](docs/linux-exhaust-model.md) for the full configuration table and the demand model reference.

## Diagnostics

Before you start the game, start the listener:

```bash
python3 modguard-linux/tools/listen.py
```

The listener binds to `/tmp/modguard-exhaust-<uid>.sock` by default. Set `MODGUARD_EXHAUST_SOCKET` to an absolute path to select a different socket.

The plugin sends `exhaust_demand_v0` JSON datagrams at most ten times each second. This output is diagnostic only. Missing datagrams do not affect in-process particle control.

## Research Notes

The canonical specification records the source binary hash, recovered SDK behavior, configuration schema, formulas, particle layouts, and game addresses:

- [`reverse-enginnering/MODGuard_RE.md`](reverse-enginnering/MODGuard_RE.md): current consolidated specification.
- [`docs/README.md`](docs/README.md): index of detailed research and implementation documents.
- [`docs/artefacts/README.md`](docs/artefacts/README.md): raw artefact descriptions and reproduction tools.

Implementation documentation for the current Linux plugin and smoke-mod presets:

- [`docs/linux-plugin-architecture.md`](docs/linux-plugin-architecture.md): plugin lifecycle, events, channels, diagnostics, build, and tests.
- [`docs/linux-exhaust-model.md`](docs/linux-exhaust-model.md): demand model and full configuration table.
- [`docs/linux-particle-hook.md`](docs/linux-particle-hook.md): inline game-memory hook and admission sampler.
- [`docs/mod-smoke-variants.md`](docs/mod-smoke-variants.md): clean smoke-mod presets and packaging.

Addresses and object layouts are specific to the analyzed game executables. Do not apply them to another release without new binary evidence.

## Legal Notice

`MODGuard.dll`, `PM_389_Smoke.zip`, and extracted companion-mod files belong to their respective rights holders. Their presence supports authorized interoperability research.

The clean-room Linux implementation is separate source code. `modguard-linux/LICENSE-SCS-SDK` only covers the vendored SCS SDK definitions.

This repository does not currently provide a license for the remaining Linux source. Copyright law reserves all rights until the maintainers add one.