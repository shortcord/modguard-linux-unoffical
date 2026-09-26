# Mod → Plugin Emitter Attachment

This document describes how the companion smoke mod and the Linux plugin cooperate so the plugin can control smoke emission. The mod supplies **what** to render and **where**. The plugin supplies **how many** spawn requests to admit. Either side alone is a no-op.

The source of truth for implemented behavior is the source code under `modguard-linux/`. The source of truth for the on-disk companion mod is `smoke-mod/_shared/` (read-only research evidence). The source of truth for the analyzed Windows plugin is `../reverse-enginnering/MODGuard_RE.md`. AGENTS.md governs what is read-only.

## 1. The on-disk contract (`smoke-mod/_shared/`)

The companion mod `PM_389_Smoke.zip` extracts into `smoke-mod/_shared/`. This tree is the read-only base that the clean presets overlay. It contains:

- `unit/hookup/smoke_new.sii` — the SiiNunit binding the plugin recognizes. It declares one `model_particle : model.particle.smoke_new` with three `particle_gen` entries.
- `unit/hookup/smoke_new.sii.bak` — the stock single-emitter definition the mod replaces. 60 particles, `dirt` look. Kept as evidence only.
- `model/particle/pipe.dds` and `model/particle/pipe.tobj` — the smoke sprite texture and its tobj.
- `manifest.sii`, `mod_description.txt`, `image.jpg` — mod metadata.
- `vehicle/`, `automat/`, `unit/` — the rest of the truck-specific hookup wiring.

The companion mod also patches 162 `.pmg` exhaust mesh files across 17 truck brands. Each patched `.pmg` carries the ASCII string `model.particle.smoke_new` in its string table. That single hookup name is what makes a patched exhaust mesh resolve to this mod at runtime.

## 2. Emitter placement in truck models

The emitter location is in each patched exhaust `.pmg` file. It is not in a `vehicle/truck/*.sii` accessory file.

Each patched exhaust mesh embeds the hookup name `model.particle.smoke_new`. The `.pmg` also stores the local transform data for that hookup. The game uses that transform when it attaches the particle object to the exhaust mesh.

The `smoke_new.sii` file defines the particle object that the hookup name resolves to. It does not choose the truck-side position. Its three `particle_gen` entries use `gen_pos: (0, 0, 0)`, so the mesh hookup transform supplies the final emitter origin and direction.

The clean variants under `smoke-mod/<variant>/` do not move emitters. They replace only `unit/hookup/smoke_new.sii` and `model/particle/pipe.dds`. As a result, every variant inherits the truck-specific placement data from `smoke-mod/_shared/`.

Coverage in `smoke-mod/_shared/vehicle/truck/`:

| Path | Patched `.pmg` files |
| --- | --- |
| `freightliner_cascadia2019` | 6/6 |
| `freightliner_cascadia2024` | 6/6 |
| `intnational_9900i` | 6/6 |
| `intnational_lonestar` | 10/10 |
| `intnational_lt` | 8/9 |
| `kenworth_t680_2022` | 4/4 |
| `kenworth_w900` | 43/43 |
| `mack_anthem` | 8/8 |
| `mack_pinnacle` | 11/11 |
| `volvo_vnl2018` | 10/10 |
| `westernstar_49x` | 9/9 |
| `westernstar_5700xe` | 9/9 |
| `westernstar_57x` | 8/8 |
| `upgrade/exhaust/kenworth_t680` | 6/6 |
| `upgrade/exhaust/kenworth_w900` | 4/4 |
| `upgrade/exhaust/peterbilt_389` | 4/4 |
| `upgrade/exhaust/peterbilt_579` | 6/6 |
| `upgrade/exhaust/volvo_vnl` | 4/4 |

The only unpatched `.pmg` in this tree is `intnational_lt/accessory/exhaust/exhaust_a_b_int.pmg`.

Future agents must not edit `smoke-mod/_shared/`. It is read-only evidence extracted from `PM_389_Smoke.zip`.

Future tools must inspect each target `.pmg` before they infer a binary layout. Some files have compact 36-byte transform records after the hookup name. Other files continue with a larger matrix-like record. The shared fact is the embedded hookup name and the truck-local transform data, not one universal byte layout.

## 3. The runtime wiring

When the game loads a patched truck, it resolves the chain below.

1. A patched `.pmg` references hookup `model.particle.smoke_new` at its embedded transform.
2. The game resolves the `model_particle` unit from `smoke_new.sii`.
3. The unit owns three `particle_gen` references (`_nameless.208.53FC`, `53FD`, `53FE`).
4. The game instantiates one `model_particle_u` and one `particle_gen_u` per entry. ATS wraps each entry in `particle_emitter_u`.
5. The exhaust mesh binds to the live `model_particle_u`. Each generator runs as a smoke layer.

The class hierarchy (RE §5.6) is:

```
prism::model_hookup_u
   └── prism::particle_set_u
         └── prism::model_particle_u       (owns gen array at +0xb0)

prism::unit_t
   └── prism::particle_gen_u
         └── prism::particle_emitter_u     (ATS only)
```

`model_particle_u` derives from `particle_set_u`, which owns an array of generator pointers. ATS stores derived `particle_emitter_u` objects in that array. ETS2 stores base `particle_gen_u` objects. Either way, the layered generators are separate array entries, not three sub-objects inside one emitter.

The three gen entries sum to 64 particles (40 + 20 + 4) and replace the stock 60-particle emitter with near-to-far tiered sprite sizes (0.090, 0.040, 0.020).

## 4. The three layer signatures

The plugin recognizes a runtime generator by three numbers in its SII configuration: `particles_max_count`, `particle_size`, and `time_to_live`. The table is the single source for these numbers.

| Layer | `particles_max_count` | `particle_size` (x, y) | `time_to_live` |
| --- | --- | --- | --- |
| 0 (haze) | 40 | 0.090 | 3.0 |
| 1 (mid)  | 20 | 0.040 | 0.7 |
| 2 (core) |  4 | 0.020 | 0.2 |

Floats match with a tolerance of `0.0005` (`particle_signature.cpp:19`). Any classifier that fails to match all three returns `-1`. The hook then passes the spawn request to the original game function unchanged.

## 5. The classifier contract

Each supported game has its own classifier in `modguard-linux/src/particle_signature.cpp`. Every classifier first checks the vtable pointer(s) against the game image base. Then it reads the layer fields at game-specific offsets.

ETS2 1.55 (`classify_ets2_155_smoke_container`, `particle_signature.cpp:40`):

- The container pointer points to `generator - 0x548`.
- Generator vtable at `+0x0` must equal `image_base + 0x2c99180`.
- Container vtable at `+0x10` must equal `image_base + 0x2c99150`.
- Reads count at `+0x70`, size at `+0x54` and `+0x58`, lifetime at `+0x524`.

ATS 1.55 (`classify_ats_155_smoke_container`, `particle_signature.cpp:74`):

- The container pointer points to `generator - 0x640`.
- Generator vtable at `+0x0` must equal `image_base + 0x2d15040`.
- Container vtable at `+0x0` must equal `image_base + 0x2d15010`.
- Reads count at `+0x80`, size at `+0x60` and `+0x64`, lifetime at `+0x7c`.

ATS 1.60.1.8s (`classify_ats_160_smoke_generator`, `particle_signature.cpp:92`):

- The function takes the generator directly. There is no container indirection.
- Generator vtable at `+0x0` must equal `image_base + 0x2d15040`.
- Reads count at `+0x630`, size at `+0x58` and `+0x5c`, lifetime at `+0x62c`.

ATS 1.61.1.4s (`classify_ats_161_smoke_generator`, `particle_signature.cpp`):

- The allocator receives `generator + 0x668`.
- Generator vtable at `+0x0` must equal the `particle_gen_u` base address
  point `image_base + 0x2e41fc8`.
- Reads count at `+0x630`, size at `+0x54` and `+0x58`, lifetime at `+0x62c`.

ETS2 1.61.1.1s (`classify_ets2_161_smoke_generator`, `particle_signature.cpp`):

- The allocator receives `generator + 0x668`.
- Generator vtable at `+0x0` must equal the `particle_gen_u` base address
  point `image_base + 0x2e35488`.
- Reads count at `+0x630`, size at `+0x54` and `+0x58`, lifetime at `+0x62c`.

ATS 1.61.2.0s (`classify_ats_162_smoke_generator`, `particle_signature.cpp`):

- The allocator receives `generator + 0x668`.
- Generator vtable at `+0x0` must equal the `particle_gen_u` base address
  point `image_base + 0x2e43508`.
- Reads count at `+0x630`, size at `+0x54` and `+0x58`, lifetime at `+0x62c`.

The ETS2 1.61.1.1s and ATS 1.61.2.0s classifiers use the shared
`classify_particle_gen_u_smoke` helper. Each classifier supplies its own
vtable offset, so the check stays pinned to one executable.

A vtable mismatch on any check makes the classifier return `-1`. The hook then forwards the call to the original allocator or spawn function with no change.

The matching synthetic test lives in `modguard-linux/tests/particle_signature_test.cpp`. It builds synthetic generator and container byte vectors, sets the vtable pointers and layer fields at the offsets above, and checks that each classifier accepts the three known layers and rejects vtable mismatches.

## 6. Generator registration

Once the constructor hook recognizes a generator, it records it in an open-addressed hash table. `particle_generator_constructor_hook` (`particle_hook.cpp:270`) calls the original constructor. Then it calls `register_generator(generator)`.

The table is a fixed-size array of `std::atomic<void *>` slots (`particle_hook.cpp:99`). Capacity is 16384. The slot index is `(generator >> 4) & 16383` (`particle_hook.cpp:176`). Probing is linear. The table never resizes. The table is cleared at install time and is never reused across installs.

For ATS 1.55 and ETS2 1.55 the allocator hook receives a container pointer, not a generator pointer. `is_registered_container` (`particle_hook.cpp:190`) subtracts `profile->container_offset` to derive the owning generator, then probes the table. This is why the offsets in section 5 must match the allocator argument.

A second table caches the original `time_to_live` captured at generator registration. `set_particle_lifetime_multiplier` writes `cached_original * multiplier` to the generator on every admitted spawn. The override is idempotent, so successive spawns do not compound on the previous scaled value.

## 7. The control loop

The hook callbacks run the same admission logic (`linux-particle-hook.md` section 8):

1. Classify the layer via the profile's classifier. If the result is `-1`, call the original allocator or spawn unchanged.
2. If `eligible` is `false`, suppress. Return `nullptr` for the allocator, or no-op for the spawn.
3. Read `demand`. Compute `threshold = (uint32)(demand * 16777216.0F)`.
4. If `threshold == 0`, suppress.
5. If `threshold != 0x1000000` and `sampler.sample(layer) >= threshold`, suppress.
6. Otherwise call the original allocator or spawn.

The 24-bit threshold is the admission fraction in fixed point. `demand = 1` admits every eligible request. `demand = 0` suppresses every request. Intermediate values thin the spawn stream proportionally. Existing particles and per-particle fields are never mutated by the hook. The `particles_max_count`, `particle_size`, `scale_acc`, and live particles stay exactly as the SII and the game renderer set them.

## 8. How a new variant or game version breaks this

A new clean variant (`smoke-mod/<name>/`) must keep the three (count, size, lifetime) tuples in section 4 exactly. Other fields are free. If a variant changes any signature, the classifier returns `-1` for every layer and the plugin passes every spawn through to the original allocator. The variant becomes invisible to the hook.

A new game version requires a new entry in the `HookProfile` table (`particle_hook.cpp:77`) and a matching classifier in `particle_signature.cpp`. Required artefacts for any new profile:

- `min_version` and `max_version` set to the exact version. No wildcards.
- Exact byte signature for the spawn and constructor sites, with a `memcmp` gate.
- Addresses bounds-checked against the executable segment.
- Vtable bases documented with the game binary and version.
- A synthetic signature test added to `modguard-linux/tests/particle_signature_test.cpp`.

The safety rules in AGENTS.md (Hook Safety section) govern any change to `particle_hook.cpp`, `particle_signature.cpp`, or `particle_admission.cpp`.

## 9. Known contract gaps

Public-unit identity matching remains open. The classifiers check vtable pointers and three layer numbers, but they do not check the `model_particle` unit name. A different mod that uses identical layer numbers and lives in a binary with the same vtable layout will also match. See `linux-particle-hook.md` section 6.1 for the next hardening step.

ATS 1.60.1.8s uses the spawn-function-entry model. The spawn hook replaces the per-particle decision inside the spawn function and bypasses the allocator trampoline. Generators still register through the constructor hook, so the same registration table applies. Future work must not double-register generators in this model.

ATS 1.61.1.4s, ETS2 1.61.1.1s, and ATS 1.61.2.0s use the list-allocator-entry
model. The hook passes unrelated allocations through the original trampoline.
Recognized particle callers check for `nullptr`, so suppression does not enter a
null-node unlink path.

## 10. Editing a mesh file

Use this workflow only when you must add an emitter at a new physical location. If you only need a different smoke look, edit a clean `smoke_new.sii` preset instead. See `mod-smoke-variants.md`.

A `.pmg` file is a compiled SCS mesh binary. Do not patch it with a hex editor.

The hookup name can appear as ASCII text. The file also contains offsets, string tables, transform records, and geometry data. Some files use compact 36-byte transform records. Other files use larger matrix-like records.

Use a separate working copy for mesh edits. Do not edit `smoke-mod/_shared/`. That tree is read-only evidence and contains proprietary extracted content.

## 11. Required tools

Install these tools before you edit a mesh:

- Blender.
- SCS Blender Tools: `https://github.com/SCSSoftware/BlenderTools`.
- SCS Conversion Tools: `https://modding.scssoft.com/wiki/Documentation/Tools/Conversion_Tools`.
- ConverterPIX: `https://github.com/mwl4/ConverterPIX`.
- Optional ConverterPIXWrapper: `https://github.com/mwl4/ConverterPIXWrapper`.
- Optional SCS extractor: `https://modding.scssoft.com/wiki/Documentation/Tools/Game_Archive_Extractor`.

Useful SCS documentation:

- Import with SCS Blender Tools: `https://modding.scssoft.com/wiki/Documentation/Tools/SCS_Blender_Tools/Import`.
- Model Locators: `https://modding.scssoft.com/wiki/Documentation/Tools/SCS_Blender_Tools/Locators/Model_Locators`.
- Hookup Library configuration: `https://modding.scssoft.com/wiki/Documentation/Tools/SCS_Blender_Tools/Global_Settings`.

## 12. End-user workflow

The safe workflow is convert, edit in Blender, export, convert back, and package.

1. Create a working directory outside `smoke-mod/_shared/`.
2. Copy the target truck model files into the working directory.
3. If the model is in an `.scs` archive, extract it with `scs_extractor`.
4. Convert the target `.pmg` and `.pmd` files with ConverterPIX.
5. Make sure that ConverterPIX produced SCS intermediate files.
6. Open Blender.
7. Enable SCS Blender Tools.
8. Configure the SCS project base in Blender.
9. Configure the Hookup Library path in Blender.
10. Make sure that the library contains `model.particle.smoke_new`.
11. Import the generated `.pim` file into Blender.
12. Find the exhaust outlet in the model.
13. Add a Model Locator at the exhaust outlet.
14. Set the locator Hookup value to `model.particle.smoke_new`.
15. Set the locator transform to point along the exhaust flow.
16. Save the Blender file for future edits.
17. Export the model with SCS Blender Tools.
18. Convert the exported intermediate files with SCS Conversion Tools.
19. Make sure that the conversion log has no model or material errors.
20. Package the converted `.pmg`, `.pmd`, and related resources as a mod.
21. Keep the same in-game path as the model that you override.
22. Enable the mesh mod and the smoke particle mod in the game mod manager.
23. Start the game.
24. Read `game.log.txt`.
25. Make sure that the game loaded the mesh and the hookup without errors.
26. Check the emitter position in game.

The mesh mod supplies the emitter location. The smoke particle mod supplies `unit/hookup/smoke_new.sii`. The Linux plugin controls only the particle admission rate.

## 13. Validation checklist

Use this checklist after you export the mesh:

- The exported model contains a Model Locator.
- The locator Hookup value is `model.particle.smoke_new`.
- The locator is at the exhaust outlet.
- The locator direction matches the exhaust flow.
- The converted mod contains the expected `.pmg` and `.pmd` files.
- The mod preserves the in-game path of the overridden model.
- `game.log.txt` has no missing-model, missing-material, or missing-hookup errors.
- Smoke appears at the new location in game.

If the smoke appears at the wrong location, correct the locator transform in Blender. Then export and convert the model again.

If the smoke does not appear, make sure that `unit/hookup/smoke_new.sii` is loaded by an enabled mod. Then read `game.log.txt` for hookup errors.

## 14. Agent requirements for mesh edits

An automated agent needs more than text-editing access to change emitter placement. It needs these capabilities:

- Run ConverterPIX or an equivalent PMG-to-PIX converter.
- Run Blender with SCS Blender Tools.
- Use Blender Python or controlled UI automation.
- Create and place SCS Model Locators.
- Set SCS custom properties, including the Hookup value.
- Export SCS intermediate model files.
- Run SCS Conversion Tools.
- Read converter logs and `game.log.txt`.
- Package a mod with the correct SCS path layout.

Direct `.pmg` patching needs a custom PMG parser and writer. That parser must understand these model records:

- String tables.
- Offsets and alignment.
- Transform records.
- Materials.
- Looks and variants.
- Geometry records.

This repository does not include such a tool.

## 15. Risks and repository rules

CAUTION: Do not edit `smoke-mod/_shared/`. That directory contains proprietary extracted content and must stay unchanged.

CAUTION: Do not byte-patch `.pmg` files. A bad offset or transform record can corrupt the mesh.

CAUTION: Do not redistribute game assets or paid-mod assets unless the asset license permits it.

A new physical emitter location needs a mesh edit. A new smoke layer at an existing location does not need a mesh edit. It needs an overlay `smoke_new.sii` change and matching plugin classifier changes.

## 16. Related documents

- `linux-particle-hook.md` — install flow, trampolines, and admission logic in full.
- `mod-smoke-variants.md` — clean presets and how to author a new one.
- `linux-plugin-architecture.md` — the telemetry path that feeds `set_particle_demand`.
- `../reverse-enginnering/MODGuard_RE.md` section 5 — the Windows MODGuard equivalent (research reference only).
