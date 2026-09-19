# Smoke Mod Variants

This document describes the clean smoke-mod presets under `smoke-mod/`, the shared companion-mod tree under `smoke-mod/_shared/`, and the self-contained CMake packaging system in `smoke-mod/CMakeLists.txt`. It is the implementation reference for the playable smoke-mod archives that pair with the Linux particle hook.

For operator-facing build and install instructions, read `../smoke-mod/README.md`.

---

## 1. Shared companion-mod tree (`smoke-mod/_shared/`)

`smoke-mod/_shared/` is the extracted `PM_389_Smoke.zip` companion mod. It is the add-on that supplies the smoke particle definitions the Linux particle hook recognizes. Its `manifest.sii` identifies it as "PM 389 Smoke" by ToniVolma, an add-on for the Pizzster Modding Peterbilt 389.

The original `PM_389_Smoke.zip` in `../reverse-enginnering/` is the verbatim evidence copy. The extracted `_shared/` tree was reorganized by truck source (see section 8). The `automat/`, `unit/`, `model/`, top-level metadata, and `vehicle/share/` subtrees are still verbatim.

Relevant shared paths used by the build tool:

- `unit/hookup/smoke_new.sii` — the particle definition with three `particle_gen` entries (the three layers).
- `model/particle/pipe.dds` — the smoke sprite texture.
- `model/particle/pipe.tobj` — the tobj that references `pipe.dds`.
- `manifest.sii`, `mod_description.txt`, `image.jpg` — mod metadata.
- `vehicle/truck/{base_game,workshop}/<truck>/` — truck-specific exhaust mesh files, organized by source.
- `vehicle/truck/upgrade/exhaust/{base_game,workshop}/<truck>/` — the shared exhaust mesh pool, organized by source.
- `automat/` — the auto-generated material pool.

The shared tree contains all truck-specific `.pmg` emitter placement data. The clean presets change the particle definition and texture only, but the overlay system can also replace mesh files (see section 4). See `mod-emitter-attachment.md` for the placement contract.

---

## 2. The three smoke layers

The companion mod defines three `particle_gen` entries inside one `model_particle : model.particle.smoke_new` block. The Linux particle hook identifies these layers by `particles_max_count`, `particle_size`, and `time_to_live` (see `linux-particle-hook.md` section 6):

| Layer | `particles_max_count` | `particle_size` | `time_to_live` | Role |
| --- | --- | --- | --- | --- |
| 0 | 40 | 0.090 | 3.0 | Long-lived haze plume. |
| 1 | 20 | 0.040 | 0.7 | Mid-distance drift. |
| 2 | 4  | 0.020 | 0.2 | Short-lived core puff. |

Every clean preset must keep these three count/size/lifetime signatures exactly. If a preset does not, the hook will not recognize the layers. The hook will pass all spawn requests through to the original allocator. Other fields (alpha, flow speed, color keys, displacement, rotation, scale acceleration) are free to differ between presets.

---

## 3. Clean presets (`smoke-mod/`)

Three clean presets are provided as source under `smoke-mod/`. Each lives in its own directory and contains the files that overlay the shared tree:

- `unit/hookup/smoke_new.sii` — the replacement particle definition.
- `model/particle/pipe.dds` — a feathered, edge-free soft cloud texture that avoids obvious billboard boundaries. Same DX10 `BC3_UNORM_SRGB` format and the original `/model/particle/pipe.dds` path.

A preset can also include mesh files (`.pmg`, `.pmd`, `.pmc`) at the same relative paths as the shared tree. See section 4 for the mesh overlay rules.

### 3.1 `soft-haze`

`smoke-mod/soft-haze/unit/hookup/smoke_new.sii` — a soft natural haze with feathered sprite edges, wider lateral drift, and gentle fade-in. It has lowered per-sprite opacity. Overlapping sprites form the plume.

Notable differences from the fixture:

- Higher `base_alpha` and `alpha_blend_coef` on each layer for softer blend.
- Wider `flow_speed` and `particle_displacement` for lateral drift.
- Longer `alpha_attack_time` for a gentler fade-in.
- `particle_size_var` widened so sprites vary more in size.

### 3.2 `subtle`

`smoke-mod/subtle/unit/hookup/smoke_new.sii` — a lighter, modern exhaust with reduced opacity, gentler growth, and less visual persistence. It becomes visible mainly under load.

Notable differences from `soft-haze`:

- Lower `base_alpha` and `alpha_blend_coef` on every layer.
- Lower `scale_acc` so sprites grow more slowly.
- Tighter `flow_speed_acc` and `particle_displacement` for less drift.
- Slightly faster `alpha_attack_time` on the core layer.

### 3.3 `dark-long`

`smoke-mod/dark-long/unit/hookup/smoke_new.sii` — a heavy, dark smoke that lingers in the air. The opacity is high, the growth rate is near zero, and the color palette is very dark grey. The SII keeps the hook-required lifetime signatures. For the 1-minute layer-0 lifetime, pair this preset with `LifetimeMultiplier=20` in the plugin configuration (see `modguard-linux/modguard_linux.ini.example` and section 5 below).

Notable differences from `soft-haze`:

- Higher `base_alpha` and `alpha_blend_coef` on every layer.
- Lower `scale_acc` so smoke stays large and visible longer.
- Darker `key_color` values.
- Hook-signature `time_to_live` values kept identical to the fixture.

All three presets keep the three count/size/lifetime signatures identical to the fixture so the particle hook still recognizes them.

---

## 4. Overlay pipeline

The packaging system in `smoke-mod/CMakeLists.txt` and `smoke-mod/cmake/package_variant.cmake` overlays each preset onto the shared tree and writes a zip archive.

### 4.1 Overlay file matching

The overlay is a recursive tree. Every file under `smoke-mod/<variant>/` is an overlay candidate, paired with its relative path in `_shared/`. At configure time, the build checks that each overlay file has a matching file in the shared tree at the same relative path. If an overlay file has no match, the build stops with an error. The overlay may only replace existing files. It cannot add new files.

### 4.2 Mesh overlay rules

The overlay can replace mesh files. The build enforces these rules at configure time:

- If an overlay supplies a `.pmg` file, it must also supply the matching `.pmd` file.
- If the original shared `.pmg` has a paired `.pmc` file, the overlay must supply that `.pmc` file too.

If an overlay breaks these rules, the build stops with an error. Mismatched mesh pairs would crash the game, so the build fails closed.

The three current presets do not supply mesh files. They replace only the particle definition and the sprite texture.

### 4.3 Packaging steps

At build time, the `cmake/package_variant.cmake` script does the following for each variant:

1. It copies the full shared tree into a staging directory under `smoke-mod/build-cmake/stage/<variant>/`.
2. It flattens the `base_game/` and `workshop/` truck split back to the flat `/vehicle/truck/<truck>/` layout. It does the same for the `upgrade/exhaust/` pool. See section 8.
3. It removes all `.bak` files from the staging directory.
4. It applies the overlay. For each overlay file, it removes the staged file at the same relative path, then copies the overlay file into its place.
5. It keeps the original `pipe.tobj`. This file references `/model/particle/pipe.dds`. No preset supplies a replacement `pipe.tobj`, so the shared copy stays.
6. It zips the staged tree into `smoke-mod/build-cmake/PM_389_Smoke_<variant>.zip` with `cmake -E tar --format=zip`.
7. It removes the staging directory.

The script uses no external `zip` binary. It uses the portable `cmake -E tar --format=zip` command.

### 4.4 Zip internal layout

The zip stores entries with no top-level prefix folder. The top-level entries (`automat/`, `model/`, `unit/`, `vehicle/`, `manifest.sii`, `mod_description.txt`, `image.jpg`) sit at the root of the zip. The `vehicle/truck/` tree uses the flat `/vehicle/truck/<truck>/` layout that the game expects, not the source-tree `base_game/`/`workshop/` split.

---

## 5. How to create a new variant

To create a new smoke preset, follow these steps:

1. **Create the folder structure**. Make a new directory under `smoke-mod/<variant_name>/`. Copy the structure from an existing preset (`soft-haze` is a good template).

2. **Create the particle definition**. Create a file at `unit/hookup/smoke_new.sii`. Use an existing variant as a template.
    - **Important**: You must keep the `particles_max_count`, `particle_size`, and `time_to_live` values for the three `particle_gen` entries exactly as they are in the original companion mod. The Linux particle hook uses these values as signatures to identify the layers. If you change them, the hook will not work.
    - You can change other values (such as `base_alpha`, `flow_speed`, or `key_color`) to change the look and behavior of the smoke.
    - To make the smoke linger longer than the SII lifetime, set `LifetimeMultiplier` in `modguard_linux.ini`. Range [0, 60]. A value of 20 makes layer 0 last about 60s. The plugin caches the original `time_to_live` at generator registration and writes `original * multiplier` on every admitted spawn. The override is idempotent. Successive spawns do not compound on the previous scaled value.

3. **Provide a smoke texture**. Place a `.dds` file at `model/particle/pipe.dds`. Use a feathered, edge-free cloud texture. The file must use the `BC3_UNORM_SRGB` format.

4. **Optional: replace mesh files**. If you want to replace exhaust mesh files, copy the `.pmg` and `.pmd` pair into the preset at the same relative path as the shared tree. If the original shared `.pmg` has a paired `.pmc`, copy that too. The build checks that the pair is complete.

5. **Add the variant to the build system**. Open `smoke-mod/CMakeLists.txt`. Add the `<variant_name>` to the `SMOKE_MOD_VARIANTS` list.

6. **Build the mod**. Run the following commands from the project root:
    ```bash
    cmake -S smoke-mod -B smoke-mod/build-cmake
    cmake --build smoke-mod/build-cmake --target smoke_mod_<variant_name>
    ```

Outputs (build artifacts, not committed):

- `smoke-mod/build-cmake/PM_389_Smoke_soft-haze.zip`
- `smoke-mod/build-cmake/PM_389_Smoke_subtle.zip`
- `smoke-mod/build-cmake/PM_389_Smoke_dark-long.zip`

Enable one of these in the game mod manager. The Linux plugin does not install or modify the companion mod.

---

## 6. In-game verification

You must check the final visual tuning in game. The repository cannot reproduce the SCS particle renderer. The admission decisions of the hook interact with the sprite blending, sort order, and camera distance of the renderer.

If a preset looks wrong in game, adjust the non-signature fields in its `smoke_new.sii`. Then repackage the preset. If you change `particles_max_count`, `particle_size`, or `time_to_live`, update the hook classifiers and tests at the same time.

---

## 7. Related documents

- `../smoke-mod/README.md` — operator-facing build and install instructions for the smoke-mod presets.
- `linux-particle-hook.md` — how the hook recognizes the three layers by count, size, and lifetime.
- `linux-plugin-architecture.md` — plugin install and the mod manager workflow.
- `../reverse-enginnering/MODGuard_RE.md` section 5.2 — the analyzed PMG hookup layout (research reference only).
- `../docs/03-smoke-mod-analysis.md` — the companion mod content breakdown (research reference only).

---

## 8. Truck source classification

The shared tree organizes truck overrides by source. The source-tree split is:

- `_shared/vehicle/truck/base_game/<truck>/` — trucks that ship with ATS.
- `_shared/vehicle/truck/workshop/<truck>/` — trucks from Steam Workshop mods.
- `_shared/vehicle/truck/upgrade/exhaust/base_game/<truck>/` — shared exhaust mesh pool for base-game trucks.
- `_shared/vehicle/truck/upgrade/exhaust/workshop/<truck>/` — shared exhaust mesh pool for Workshop trucks.

The file `smoke-mod/truck_sources.toml` records the source classification for each truck. Each entry has a `source` field (`base_game` or `workshop`), an `evidence` note, and a `status` field (`confirmed` or `hypothesis`).

The packaging system flattens this split when it writes the zip. The shipped archive uses the flat `/vehicle/truck/<truck>/` path that the game expects. The split is a source-tree organization only.

### 8.1 Classification table

The classification uses this convention: SCS base-game truck directories use plain names with no year suffix. Year-suffixed directories denote third-party model-year mods. The SCS modding wiki has no per-truck directory listing, so non-launch-truck entries are hypotheses.

| Truck directory | Source | Status | Evidence |
| --- | --- | --- | --- |
| `intnational_9900i` | base_game | hypothesis | Official SCS ATS truck. |
| `intnational_lonestar` | base_game | confirmed | Official SCS ATS truck, added in 2020. |
| `intnational_lt` | base_game | confirmed | Official SCS ATS truck. |
| `kenworth_t680` | base_game | confirmed | ATS launch truck (2016). |
| `kenworth_w900` | base_game | confirmed | ATS launch truck (2016). |
| `mack_anthem` | base_game | confirmed | Official SCS ATS truck, added in 2020. |
| `mack_pinnacle` | base_game | hypothesis | Official SCS ATS truck. |
| `peterbilt_579` | base_game | confirmed | ATS launch truck (2016). |
| `volvo_vnl` | base_game | hypothesis | Official SCS ATS truck, plain directory name. |
| `westernstar_49x` | base_game | confirmed | Official SCS ATS truck, added in 2020. |
| `westernstar_5700xe` | base_game | hypothesis | Official SCS ATS truck. |
| `westernstar_57x` | base_game | hypothesis | Official SCS ATS truck, newer addition. |
| `freightliner_cascadia2019` | workshop | hypothesis | Year suffix. SCS base game uses `freightliner_cascadia`. Third-party model-year mod. |
| `freightliner_cascadia2024` | workshop | hypothesis | Year suffix. SCS base game uses `freightliner_cascadia`. Third-party model-year mod. |
| `kenworth_t680_2022` | workshop | hypothesis | Year suffix. SCS base game uses `kenworth_t680`. Third-party 2022-facelift mod. |
| `peterbilt_389` | workshop | confirmed | `mod_description.txt` names the Pizzster Modding Peterbilt 389 MegaPack as a requirement. The override targets the Workshop mod truck. |
| `volvo_vnl2018` | workshop | hypothesis | Year suffix. SCS base game uses `volvo_vnl`. Third-party VNL 2018 mod. |

If you get new evidence for a truck, update `truck_sources.toml` and this table in the same change. Move the truck directory between `base_game/` and `workshop/` if the source changes.