# PM_389_Smoke Companion Mod Analysis

## Package Overview

| Property | Value |
|----------|-------|
| File | `PM_389_Smoke.zip` |
| Size | 19,797,101 bytes (19.8 MB) |
| Mod name | PM 389 Smoke |
| Author | ToniVolma (same author as MODGuard) |
| Category | Truck |
| `mp_visible` | true |
| `mp_mod_optional` | true |

```
SiiNunit
{
mod_package : .package_name
{
    package_version: "1.0"
    display_name: "PM 389 Smoke"
    author: "ToniVolma"
    category[]: "truck"
    icon: "image.jpg"
    description_file: "mod_description.txt"
    mp_visible: true
    mp_mod_optional: true
}
}
```

### Description (from `mod_description.txt`)

> Smoke add-on for the Pizzster Modding Peterbilt 389.
> Adds exhaust smoke effects and allows MODGuard.dll to control particle emissions based on engine operation.
>
> **Smoke behavior:**
> - Smoke intensity responds to throttle input and engine load.
> - Smoke output decreases when the throttle is released.
> - The engine brake and retarder do not interrupt smoke emission while the engine is under throttle.
> - Higher engine wear increases smoke output.
>
> **Requirements:**
> - Pizzster Modding Peterbilt 389 MegaPack
> - MODGuard plugin installed and loaded by American Truck Simulator.

---

## 1. File Inventory

Total: **617 files, ~40 MB unpacked**

| Type | Count | Notes |
|------|-------|-------|
| `.pmd` | 163 | Model descriptor (header + material refs) |
| `.pmg` | 163 | Mesh binary (geometry + hookup strings) |
| `.pmc` | 8 | Precomputed matrices (westernstar_49x only) |
| `.mat` | 104 | Material definitions (text, SCS hash layout) |
| `.tobj` | 87 | Texture object references (binary) |
| `.dds` | 87 | DirectDraw Surface textures |
| `.sii` | 2 | Game definitions (`smoke_new.sii`, `smoke_new.sii.bak`) |
| `.jpg` | 1 | Mod icon |
| `.txt` | 1 | Mod description |
| `.bak` | 1 | Backup of original definition |

---

## 2. Truck Brand Coverage

**162 of 163 `.pmg` exhaust accessories embed the `model.particle.smoke_new` hookup string.**

| Brand | PMGs patched |
|-------|--------------|
| freightliner_cascadia2019 | 6/6 |
| freightliner_cascadia2024 | 6/6 |
| intnational_9900i | 6/6 |
| intnational_lonestar | 7/7 + 3 `cab_exhaust/*` |
| intnational_lt | 7/8 (1 vanilla `exhaust_a_b_int.pmg`) |
| kenworth_t680_2022 | 4/4 |
| kenworth_w900 | 43/43 |
| mack_anthem | 8/8 |
| mack_pinnacle | 11/11 |
| volvo_vnl2018 | 10/10 |
| westernstar_49x | 9/9 |
| westernstar_5700xe | 9/9 |
| westernstar_57x | 8/8 |

**Upgrade packs** (under `vehicle/truck/upgrade/exhaust/`): kenworth_t680 6/6, kenworth_w900 4/4, peterbilt_389 4/4, peterbilt_579 6/6, volvo_vnl 4/4 — all 24 exhaust PMGs patched.

---

## 3. `unit/hookup/smoke_new.sii` — The SiiNunit Binding

### 3.1 Active definition (modded, 124 lines)

```sii
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

particle_gen : _nameless.208.53FC {
 list_order_up: false
 blend_alpha: true
 gen_pos: (0, 0, 0)
 rotation_speed: 1.20
 alpha_blend_coef: 0.40
 base_alpha: 0.85
 alpha_attack_time: 0
 flow_speed: (0, 1.5, -2.5)
 flow_speed_acc: (0, 0.10, 0.30)
 particle_size: (0.090, 0.090)
 particle_size_var: (0.040, 0.040)
 scale_acc: 0.38
 particle_displacement: (0.010, 0.015, 0.010)
 color_multiplier: nil
 key_pos: 5
 key_pos[0]: 0
 key_pos[1]: 38
 key_pos[2]: 96
 key_pos[3]: 168
 key_pos[4]: 255
 key_color: 5
 key_color[0]: 0
 key_color[1]: 0
 key_color[2]: 0
 key_color[3]: 0
 key_color[4]: 0
 time_to_live: 3
 time_to_live_rand: 0.70
 particles_max_count: 40
 model_path: "/model/particle/smoke.pmd"
 model_look: pipe
 relative_space: false
 use_sprites: true
}

particle_gen : _nameless.208.53FD {
 ... particles_max_count: 20 ... particle_size: (0.040, 0.040) ...
 time_to_live: 0.7
 rotation_speed: -1.40
 alpha_blend_coef: 0.35
 flow_speed: (0, 1, -2)
 flow_speed_acc: (-0.07, 0.12, 0.28)
}

particle_gen : _nameless.208.53FE {
 ... particles_max_count: 4 ... particle_size: (0.020, 0.020) ...
 time_to_live: 0.2
 rotation_speed: 0.85
 alpha_blend_coef: 0.25
 flow_speed: (0, 0.5, -0.5)
 flow_speed_acc: (0.07, 0.11, 0.29)
 relative_space: true
}

}
```

### 3.2 Backup definition (`smoke_new.sii.bak`, original 46 lines)

```sii
SiiNunit
{
model_particle : model.particle.smoke_new {
 active_distance: &4b189680           ; hex float = 10,000,000 (likely bug or format)
 gen_array: 1
 gen_array[0]: _nameless.208.53FC
 auto_disable_delay: 0
}

particle_gen : _nameless.208.53FC {
 list_order_up: false
 blend_alpha: true
 gen_pos: (0, &3d4ccccd, &3d4ccccd)
 scale_acc: &3e99999a
 rotation_speed: 10
 alpha_blend_coef: 0.3
 base_alpha: 1
 alpha_attack_time: 0
 flow_speed: (0, 0, 0)
 flow_speed_acc: (0, 0, 0)
 particle_size: (&3d23d70a, &3d23d70a)         ; (0.04, 0.04)
 particle_size_var: (&3f000000, &3f000000)     ; (0.5, 0.5)
 particle_displacement: (&3d4ccccd, &bdcccccd, &bdcccccd)
 color_multiplier: nil
 key_pos: 5
 key_pos[0]: 0
 key_pos[1]: 48
 key_pos[2]: 103
 key_pos[3]: 181
 key_pos[4]: 255
 key_color: 5
 key_color[0]: 0
 key_color[1]: 1052688
 key_color[2]: 2105376
 key_color[3]: 4210752
 key_color[4]: 8421504
 time_to_live: 2
 time_to_live_rand: &3e4ccccc
 particles_max_count: 60
 model_path: "/model/particle/smoke.pmd"
 model_look: dirt
 relative_space: false
 use_sprites: true
}

}
```

### 3.3 Differences: `.bak` vs `.sii`

| Field | `.bak` (vanilla) | `.sii` (modded) |
|-------|------------------|------------------|
| `active_distance` | `&4b189680` (= 10,000,000??) | `220` |
| **`gen_array`** | **1** | **3** |
| `gen_array[0]` | `_nameless.208.53FC` | `_nameless.208.53FC` |
| `gen_array[1]` | — | `_nameless.208.53FD` |
| `gen_array[2]` | — | `_nameless.208.53FE` |
| `model_look` | `dirt` | `pipe` (all 3 emitters) |
| `particles_max_count` (gen 0) | **60** | **40** |
| `particles_max_count` (gen 1) | — | 20 |
| `particles_max_count` (gen 2) | — | 4 |
| Total particles (sum) | 60 | **64** |
| `particle_size` (gen 0) | `(&3d23d70a, &3d23d70a)` ≈ (0.04, 0.04) | `(0.090, 0.090)` |
| `particle_size` (gen 1) | — | `(0.040, 0.040)` |
| `particle_size` (gen 2) | — | `(0.020, 0.020)` |
| `time_to_live` (gen 0) | 2 | 3 |
| `time_to_live` (gen 1) | — | 0.7 |
| `time_to_live` (gen 2) | — | 0.2 |
| `flow_speed` (gen 0) | `(0, 0, 0)` | `(0, 1.5, -2.5)` |
| `flow_speed` (gen 1) | — | `(0, 1, -2)` |
| `flow_speed` (gen 2) | — | `(0, 0.5, -0.5)` |
| `rotation_speed` (gen 0) | 10 | 1.20 |
| `alpha_blend_coef` (gen 0) | 0.30 | 0.40 |
| `base_alpha` (gen 0) | 1.0 | 0.85 |
| `relative_space` (gen 2) | n/a | `true` (gen 0 & 1 are `false`) |

### 3.4 Layered emitter design

The 3 emitters form a **near→far tiered size** effect:

| Generator | Max particles | Particle size | Time to live | Purpose |
|-----------|---------------:|--------------:|-------------:|---------|
| `_nameless.208.53FC` | **40** | 0.090 | 3.0 s | **Near main smoke** (large, long-lived) |
| `_nameless.208.53FD` | **20** | 0.040 | 0.7 s | **Mid smoke** (medium, fast) |
| `_nameless.208.53FE` | **4** | 0.020 | 0.2 s | **Distant smoke** (small, very fast, attached) |

Total: 64 max particles vs original 60.

---

## 4. `.pmg` Hookup String Binary Format

Each patched `.pmg` has the hookup entry inserted at the end of the string table:

```
... <ptrA: uint32 LE>  <0xFFFFFFFF × 2>  <ptrB: uint32 LE>  'model.particle.smoke_new'  <00 00 00 00>
    <float[6]  24 B>   <0xFF000000>      <0x00000000 × 2>   ← 36-byte entry body
    <float[6]  24 B>   <0xFF000000>      <0x00000000 × 2>   ← next body
    ...
```

### 4.1 Fields

| Field | Size | Meaning |
|-------|------|---------|
| `ptrA` | 4 B | File-internal offset back into geometry block |
| `0xFFFFFFFF` | 4 B | Sentinel |
| `0xFFFFFFFF` | 4 B | Sentinel |
| `ptrB` | 4 B | Offset to LOD/skin index table |
| `'model.particle.smoke_new'` | 24 B | ASCII hookup name |
| `0x00 0x00 0x00 0x00` | 4 B | NUL pad for 4-byte alignment |
| `float[6]` | 24 B | Transform (one body per LOD/instance) |
| `0xFF000000` | 4 B | Terminator |
| `0x00000000 × 2` | 8 B | Padding |

### 4.2 Verification

Tested across PMG versions 2, 3, 4, 5, and 6. All have the same layout. The string `'model.particle.smoke_new'` is preceded by 16 bytes of header. It is followed by 4 bytes of pad.

The single ASCII hookup string used in every file is **`model.particle.smoke_new`**. No other hookup names appear in any PMG of this mod.

---

## 5. `model/particle/pipe.dds` — The Smoke Sprite

| Property | Value |
|----------|-------|
| File | `model/particle/pipe.dds` |
| Size | 5,636 bytes |
| Format | BC3_UNORM_SRGB |
| Resolution | 64×64 |
| Purpose | New sprite texture replacing vanilla `dirt` look |
| `pipe.tobj` | 72 bytes, references `/model/particle/pipe.dds` |

The actual particle quad mesh `/model/particle/smoke.pmd` is **not** shipped. The game is expected to provide it. All three `particle_gen` instances point to it.

---

## 6. Mod Metadata

- **Package**: "PM 389 Smoke"
- **Author**: ToniVolma (same as MODGuard)
- **Version**: 1.0
- **Category**: `truck`
- **MP compatible**: yes (`mp_visible: true`, `mp_mod_optional: true`)
- **Dependency**: Pizzster Modding Peterbilt 389 MegaPack (the truck mod)
- **Required by**: MODGuard (the plugin)

---

## 7. Mandatory Prerequisites

This mod is a **mandatory prerequisite** for MODGuard:

1. **Without `unit/hookup/smoke_new.sii`**, the game has no `model.particle : model.particle.smoke_new`.
2. **Without the `model.particle.smoke_new` hookup string in `.pmg` files**, the truck models have no particle emitter to bind to.
3. **Without the 3-emitter layout of the mod**, the MODGuard RealisticSmoke formula cannot map its bucket values (100/2500/10000) to particle sizes that produce visible smoke.

MODGuard prints `[MODGuard] Smoke hook not activated: <reason>` if any of these is missing.

---

## 8. Sister Mods

The same pattern repeats for each truck brand in a separate mod. The SCS mod manager allows only one definition of `model_particle : model.particle.smoke_new` per mod folder. ToniVolma publishes one pack per truck brand. All packs share the same author and the same underlying hookup architecture.

---

## 9. Relationship to MODGuard

```
                   ┌─────────────────────────────────────────┐
                   │         ETS2 / ATS Game Process         │
                   │                                          │
                   │  ┌──────────────┐   ┌────────────────┐  │
                   │  │ PM_389_Smoke │   │    MODGuard    │  │
                   │  │     .zip      │   │     .dll       │  │
                   │  └──────┬───────┘   └───────┬────────┘  │
                   │         │                    │            │
                   │  Provides:                Hooks:        │
                   │  - smoke_new.sii          essential_hook│
                   │  - PMG hookup strings     particle_alloc_hook
                   │  - pipe.dds texture       bucket iteration
                   │         │                    │            │
                   │         ▼                    ▼            │
                   │  ┌──────────────────────────────────────┐│
                   │  │  SCS Engine (game runtime)           ││
                   │  │  - SiiNunit parser                   ││
                   │  │  - model_particle allocator          ││
                   │  │  - particle_gen emitters             ││
                   │  │  - list_t<particle_gen_u, particle_t>││
                   │  └──────────────────────────────────────┘│
                   └─────────────────────────────────────────┘
```

### 9.1 Lifecycle

1. Game starts → reads all enabled mods from the `mod/` folder.
2. The SiiNunit parser loads `unit/hookup/smoke_new.sii`. It builds `model_particle : model.particle.smoke_new`.
3. The game loads the `.pmg` exhaust meshes. For each exhaust with the `model.particle.smoke_new` hookup:
   - It allocates a `model_particle_u` instance.
   - It binds the `gen_array[0..2]` of this instance to the 3 `particle_gen_u` definitions.
4. The MODGuard `essential_hook` intercepts step 3. It gets access to the live `model_particle_u`.
5. The MODGuard `particle_allocator_hook` modifies the emitter creation of step 3. It registers custom callbacks.
6. Each frame, the particle update loop of the game iterates `list_t<particle_gen_u, particle_t>`. It calls the MODGuard bucket assignment on each live particle.
7. The bucket value (100/2500/10000) is written back to `particle->size` at `+0x18`.

---

## 10. Where to Find Each Truck's Companion Mod

The exact filenames for each truck brand were not captured during this analysis. They follow this pattern:

```
PM_<Make>_<Model>_Smoke.zip     e.g. PM_Freightliner_Cascadia_Smoke.zip
```

To identify the correct mod for a given truck:
1. Check `mod_description.txt` for the "PM <Brand> Smoke" name.
2. Extract the mod. Make sure that `unit/hookup/smoke_new.sii` is present.
3. Check that at least one `.pmg` contains `model.particle.smoke_new`.