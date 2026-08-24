# Smoke Mod Variants

This document describes the clean smoke-mod presets under `smoke-mod/`, the shared companion-mod tree under `smoke-mod/_shared/`, and the self-contained CMake packaging system in `smoke-mod/CMakeLists.txt`. It is the implementation reference for the playable smoke-mod archives that pair with the Linux particle hook.

The proprietary companion mod `PM_389_Smoke.zip` is read-only research evidence. It is never modified (see `../AGENTS.md`). Its extracted tree lives under `smoke-mod/_shared/` and is kept verbatim. The clean presets are separate source content that overlays it.

---

## 1. Shared companion-mod tree (`smoke-mod/_shared/`)

`smoke-mod/_shared/` is the extracted `PM_389_Smoke.zip` companion mod. It is the add-on that supplies the smoke particle definitions the Linux particle hook recognizes. Its `manifest.sii` identifies it as "PM 389 Smoke" by ToniVolma, an add-on for the Pizzster Modding Peterbilt 389.

Relevant shared paths used by the build tool:

- `unit/hookup/smoke_new.sii` — the particle definition with three `particle_gen` entries (the three layers).
- `model/particle/pipe.dds` — the smoke sprite texture.
- `model/particle/pipe.tobj` — the tobj that references `pipe.dds`.
- `manifest.sii`, `mod_description.txt`, `image.jpg` — mod metadata.
- `vehicle/`, `automat/`, `unit/` — the rest of the truck-specific hookup wiring (unchanged by the clean presets).

The shared tree must stay unchanged. The build tool copies it verbatim. It overlays only the two clean files described below.

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

Two clean presets are provided as source under `smoke-mod/`. Each lives in its own directory and contains only the two files that overlay the shared tree:

- `unit/hookup/smoke_new.sii` — the replacement particle definition.
- `model/particle/pipe.dds` — a feathered, edge-free soft cloud texture that avoids obvious billboard boundaries. Same DX10 `BC3_UNORM_SRGB` format and the original `/model/particle/pipe.dds` path.

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

Both presets keep the three count/size/lifetime signatures identical to the fixture so the particle hook still recognizes them.

---

## 4. Packaging (`smoke-mod/CMakeLists.txt`)

The self-contained CMake project in `smoke-mod/CMakeLists.txt` repackages each preset as a playable mod archive. It is an opt-in build target, separate from the `modguard-linux` plugin. It uses no external `zip` binary. It uses the portable `cmake -E tar --format=zip` command.

At configure time, the project does this:

1. It checks that the shared tree `smoke-mod/_shared/` contains `unit/hookup` and `model/particle`.
2. For each variant in `soft-haze subtle`, it checks that the overlay has `unit/hookup/smoke_new.sii` and `model/particle/pipe.dds`.

At build time, the `cmake/package_variant.cmake` script does the following for each variant:

1. It copies the full shared tree into a staging directory under `smoke-mod/build-cmake/stage/<variant>/`.
2. It removes the `smoke_new.sii` and `pipe.dds` files from the staged copy.
3. It copies the overlay `unit/` tree and `pipe.dds` into the staging tree.
4. It keeps the original `pipe.tobj`. This file already references `/model/particle/pipe.dds`. The overlay overwrites this path.
5. It zips the staged tree into `smoke-mod/build-cmake/PM_389_Smoke_<variant>.zip` with `cmake -E tar --format=zip`.
6. It removes the staging directory.

Run it from the repository root:

```bash
cmake -S smoke-mod -B smoke-mod/build-cmake
cmake --build smoke-mod/build-cmake --target smoke_mod_variants
```

To build one preset only, use the per-variant target `smoke_mod_<variant>` instead of `smoke_mod_variants`.

Outputs (build artifacts, not committed):

- `smoke-mod/build-cmake/PM_389_Smoke_soft-haze.zip`
- `smoke-mod/build-cmake/PM_389_Smoke_subtle.zip`

Enable one of these in the game mod manager. The Linux plugin does not install or modify the companion mod.

---

## 5. In-game verification

You must check the final visual tuning in game. The repository cannot reproduce the SCS particle renderer. The admission decisions of the hook interact with the sprite blending, sort order, and camera distance of the renderer.

If a preset looks wrong in game, adjust the non-signature fields in its `smoke_new.sii`. Then repackage the preset. If you change `particles_max_count`, `particle_size`, or `time_to_live`, update the hook classifiers and tests at the same time.

---

## 6. Related documents

- `linux-particle-hook.md` — how the hook recognizes the three layers by count, size, and lifetime.
- `linux-plugin-architecture.md` — plugin install and the mod manager workflow.
- `../reverse-enginnering/MODGuard_RE.md` section 5.2 — the analyzed PMG hookup layout (research reference only).
- `../docs/03-smoke-mod-analysis.md` — the companion mod content breakdown (research reference only).