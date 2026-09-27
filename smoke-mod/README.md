# Smoke Mod Variants

This directory holds the clean smoke-mod presets and the packaging system that builds them into playable SCS mod archives.

## What This Project Does

The mod controls how truck exhaust smoke looks in American Truck Simulator (ATS). It has two parts:

1. A shared companion-mod tree in `_shared/`. This tree comes from the proprietary `PM_389_Smoke.zip` mod. It supplies the particle definitions, the smoke sprite texture, and the truck-specific exhaust mesh files.
2. Clean preset overlays in `soft-haze/`, `subtle/`, `dark-long/`, and `dynamic-blend/`. Each overlay replaces files in the shared tree to change the smoke appearance.

The CMake packaging system overlays a preset onto the shared tree and writes a zip archive. You install the archive through the game mod manager.

## The Overlay Pipeline

The packaging system works in this order:

1. It copies the full shared tree into a temporary staging directory.
2. It flattens the `base_game/` and `workshop/` truck split back to the flat `/vehicle/truck/<truck>/` layout that the game expects.
3. It removes all `.bak` files from the staging directory.
4. It applies the preset overlay. Each file in the overlay replaces the file at the same relative path in the staged tree.
5. It zips the staged tree into `PM_389_Smoke_<variant>.zip`.

The shared tree is never modified. The original `PM_389_Smoke.zip` in `../reverse-enginnering/` is the verbatim evidence copy.

## File Types

| Extension | Purpose | Modified by overlays |
| --- | --- | --- |
| `.sii` | Particle definition and mod metadata | Yes (`unit/hookup/smoke_new.sii`) |
| `.dds` | Smoke sprite texture | Yes (`model/particle/pipe.dds`) |
| `.tobj` | Texture descriptor that references the `.dds` | No |
| `.pmg` | Exhaust pipe mesh geometry | Yes, if the overlay supplies it |
| `.pmd` | Mesh descriptor paired with `.pmg` | Yes, if the overlay supplies it |
| `.pmc` | Collision mesh paired with some `.pmg` files | Yes, if the overlay supplies it |
| `.mat` | Material files in `automat/` | No |

The four current presets replace only the particle definition and the sprite texture. They do not replace mesh files.

## Truck Source Split

The shared tree organizes truck overrides by source:

- `_shared/vehicle/truck/base_game/<truck>/` holds trucks that ship with ATS.
- `_shared/vehicle/truck/workshop/<truck>/` holds trucks from Steam Workshop mods.
- The `upgrade/exhaust/` mesh pool uses the same split.

The file `truck_sources.toml` records the source classification for each truck. Each entry has an evidence note and a status (`confirmed` or `hypothesis`).

The packaging system flattens this split when it writes the zip. The shipped archive uses the flat `/vehicle/truck/<truck>/` path that the game expects. The split is a source-tree organization only.

## Mesh Overlay Rules

A preset overlay can replace mesh files, not just the particle definition and the texture. The build system enforces these rules:

- An overlay file can only replace a file that exists in the shared tree at the same relative path. The overlay cannot add new files.
- If an overlay supplies a `.pmg` file, it must also supply the matching `.pmd` file.
- If the original shared `.pmg` has a paired `.pmc` file, the overlay must supply that `.pmc` file too.

If an overlay breaks these rules, the build stops with an error. Mismatched mesh pairs would crash the game, so the build fails closed.

## Build

Run these commands from the repository root:

```bash
cmake -S smoke-mod -B smoke-mod/build-cmake
cmake --build smoke-mod/build-cmake --target smoke_mod_variants
```

To build one preset only, use the per-preset target:

```bash
cmake --build smoke-mod/build-cmake --target smoke_mod_soft-haze
```

The output archives are in `smoke-mod/build-cmake/`:

- `PM_389_Smoke_soft-haze.zip`
- `PM_389_Smoke_subtle.zip`
- `PM_389_Smoke_dark-long.zip`
- `PM_389_Smoke_dynamic-blend.zip`

These are build artifacts. Do not commit them.

## Install

Enable one archive in the game mod manager. Do not enable more than one at the same time. The Linux plugin does not install or modify the companion mod.

## The Presets

- **soft-haze**: a soft natural haze with feathered sprite edges, wider lateral drift, and gentle fade-in. Overlapping sprites form the plume.
- **subtle**: a lighter, modern exhaust with reduced opacity and less visual persistence. It becomes visible mainly under load.
- **dark-long**: a heavy, dark smoke that lingers in the air. For a 1-minute layer-0 lifetime, set `LifetimeMultiplier=20` in the plugin configuration.
- **dynamic-blend**: smoke that grows and darkens as engine demand rises. It needs the updated plugin.

The three legacy presets keep the three generator layer signatures. These are particle count, sprite size, and lifetime. The Linux particle hook uses these values to identify the layers. If a preset changes them, the hook does not work.

The `dynamic-blend` preset uses a different signature set. Its three generators share one particle count and one lifetime, and differ only in base size. The plugin maps the demand to three band weights that sum to one. The result is a smooth change from small and light to large and dark.

## Create a New Preset

1. Create a new directory under `smoke-mod/<preset_name>/`. Copy the structure from an existing preset.
2. Create `unit/hookup/smoke_new.sii`. Keep the `particles_max_count`, `particle_size`, and `time_to_live` values exactly as they are in the original. Change other values to adjust the smoke appearance. To blend three sizes, copy the `dynamic-blend` signature set and use the updated plugin.
3. Create `model/particle/pipe.dds`. Use a feathered, edge-free cloud texture in `BC3_UNORM_SRGB` format.
4. If you want to replace mesh files, copy the `.pmg` and `.pmd` pair into the preset at the same relative path as the shared tree. If the original has a `.pmc`, copy that too.
5. Add the preset name to `SMOKE_MOD_VARIANTS` in `CMakeLists.txt`.
6. Build the preset.

Do the final visual adjustment in game. The repository cannot reproduce the SCS particle renderer.

## Related Documents

- `../docs/mod-smoke-variants.md` — implementation reference for the presets and the packaging system.
- `../docs/linux-particle-hook.md` — how the hook recognizes the smoke identities by count, size, and lifetime, and how it blends the dynamic bands.
- `truck_sources.toml` — truck source classification and evidence.