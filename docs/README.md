# MODGuard Reverse Engineering — Documentation Index

## Project Summary

This documentation covers the reverse engineering of **MODGuard.dll**. This is a Windows-only SCS Telemetry SDK plugin. It adds a "RealisticSmoke" particle-effect mod to *American Truck Simulator* and *Euro Truck Simulator 2*. This work makes a clean-room **Linux reimplementation** possible for Proton and native Linux game installs.

The reverse-engineering documents (01-05) record the analyzed Windows binary. The implementation documents (`linux-*`, `mod-*`) describe the current Linux plugin and clean smoke-mod presets in `modguard-linux/` and `smoke-mod/`.

The maintainer guide [`linux-hook-maintainer-guide.md`](linux-hook-maintainer-guide.md) describes the shared profile table, the offline binary detector, and the safe process for adding a hook profile.

## Files Analysed

| File | Size | SHA-256 (truncated) | Purpose |
|------|------|---------------------|---------|
| `MODGuard.dll` | 705 KB | `2ad7e487…7cb097` | The plugin binary itself |
| `PM_389_Smoke.zip` | 19.8 MB | — | Companion mod (truck-specific smoke packs) |
| `American Truck Simulator/bin/linux_x64/amtrucks` | 51 MB | — | ATS 1.55.0.0 game binary |
| `Euro Truck Simulator 2/bin/linux_x64/eurotrucks2` | 51 MB | — | ETS2 1.55.0.0 game binary |

## Documents

### 📘 Core Spec

1. **[`01-reverse-engineering.md`](01-reverse-engineering.md)** — MODGuard.dll complete reverse engineering specification
   - SDK interface (`scs_telemetry_init` / `scs_telemetry_shutdown`)
   - Internal data flow, channel registration, event callbacks
   - RealisticSmoke formula
   - License check
   - All internal functions mapped to live game runtime
   - **Size**: the authoritative MODGuard reference

2. **[`02-particle-classes.md`](02-particle-classes.md)** — Game-side particle class hierarchy
   - `prism::model_particle_u` / `particle_emitter_u` / `particle_gen_u` / `particle_t`
   - Class sizes, vtable addresses, inheritance
   - Live particle (`particle_t`) memory layout
   - Differences between ATS 1.55 and ETS2 1.55
   - How the MinHook-based hook targets the runtime

3. **[`03-smoke-mod-analysis.md`](03-smoke-mod-analysis.md)** — Companion mod analysis
   - PM_389_Smoke.zip content breakdown
   - 162 truck brand variants, hookup string layout in `.pmg` files
   - `[RealisticSmoke]` SiiNunit configuration schema
   - Relationship between MODGuard and the smoke mod

4. **[`04-open-questions.md`](04-open-questions.md)** — All 6 questions resolved
   - Q1 — Particle class layout ✓
   - Q2 — `ThrottleCurve` shape ✓
   - Q3 — Steam-ID acquisition ✓
   - Q4 — License refresh interval ✓
   - Q5 — `next_float_buffer` signature ✓
   - Q6 — Register-call signature ✓

5. **[`05-linux-reimplementation.md`](05-linux-reimplementation.md)** — Porting guide
   - File-by-file plan for the Linux port
   - Component breakdown
   - Differences vs Windows (MinHook, WinHTTP, IPHLPAPI)
   - Suggested build system
   - Historical: this document predates the actual implementation. See the implementation docs below for current behavior.

### 📘 Implementation Documentation

These documents describe the current Linux implementation and clean smoke-mod
presets. They are the source of truth for implemented behavior. They are kept
separate from the reverse-engineering research above.

6. **[`linux-plugin-architecture.md`](linux-plugin-architecture.md)** — Native Linux plugin architecture
   - `modguard-linux/` layout and vendored SCS SDK ABI subset
   - `scs_telemetry_init` / `scs_telemetry_shutdown` lifecycle
   - Event and channel callbacks, configuration callback
   - Configuration loader and live reload watcher
   - Diagnostic Unix datagram socket and `exhaust_demand_v0` JSON schema
   - CMake build targets and the CTest suite

7. **[`linux-exhaust-model.md`](linux-exhaust-model.md)** — `exhaust_demand_v0` model and configuration
   - `ExhaustInputs` / `ExhaustConfig` / `ExhaustOutput` structs
   - Throttle smoothing, boost lag, load gate, weighted Windows terms, transients
   - Full `[RealisticSmoke]` key table with defaults and sanitize ranges
   - Relationship to the Windows bucket formula

8. **[`linux-particle-hook.md`](linux-particle-hook.md)** — In-process particle hook
   - Active hook profiles for ATS 1.60.1.8s, ATS 1.61.1.4s, ATS 1.61.2.0s,
     ATS 1.61.3.0s, ATS 1.61.3.1s, ATS 1.61.3.18s, ETS2 1.60.1.7s, and ETS2 1.61.1.1s; the
     1.55 allocator profiles are disabled because NULL suppression crashes
     the game
   - Allocator-trampoline, allocator-entry, and spawn-function-entry models
   - Trampoline construction, generator registry, generator classifiers
   - Synchronized `LayerSampler` admission and the 24-bit threshold
   - Dynamic-blend band weights
   - Install/shutdown flow and safety constraints

9. **[`mod-smoke-variants.md`](mod-smoke-variants.md)** — Clean smoke-mod presets
   - The legacy layer signatures and the dynamic-blend band signatures (count, size, lifetime)
   - `smoke-mod/soft-haze`, `smoke-mod/subtle`, `smoke-mod/dark-long`, and `smoke-mod/dynamic-blend` presets
   - `smoke-mod/CMakeLists.txt` packaging (opt-in target)
   - Relationship to the shared `smoke-mod/_shared/` companion-mod tree

10. **[`mod-emitter-attachment.md`](mod-emitter-attachment.md)** — Mod → plugin emitter attachment
    - On-disk contract under `smoke-mod/_shared/` (PM_389_Smoke)
    - Runtime wiring from `.pmg` hookup string to live particle generators
    - Layer signatures and per-game classifier contracts (offsets, vtable bases)
    - Generator registry and admission control loop
    - Authoring rules for new variants and new game versions
    - End-user workflow for adding a mesh locator with SCS Blender Tools

### 🗂️ Raw Artefacts

- **[`artefacts/`](artefacts/)** — Raw research output
  - `cfg_loader.txt` — Full disassembly of the configuration-loader function (1318 lines)
  - `disassembly_notes.md` — Working notes on instruction-level analysis
  - `ghidra_pyghidra.py` — Python script used for Ghidra analysis
  - `ets2_binary`, `ats_155_binary` — Reference (not copied). Use the source paths.

## Quick-Reference: Key File Hashes

```
MODGuard.dll:
  SHA-256: 2ad7e487125e491d6886721003ddb409b5fba239c86ccda7e1fc7602de7cb097
  SHA-1:   9242b2fc27912dfba3bf4ea174fda36abb195124
  MD5:     3a9dcc920253214b5e2fc719f63d155c
  Size:    705,120 bytes
  Format:  PE32+ x86-64 Windows DLL
  Authenticode: Signed by Sectigo / DigiCert
```

## Quick-Reference: Key Addresses (ATS 1.55.0.0)

```
particle_gen_u vtable:        VA 0x2d15030  (pointer 0x2d15040 = vtable+0x10)
particle_emitter_u vtable:   VA 0x2d14708  (pointer 0x2d14718 = vtable+0x10)
particle_gen_u size:         0x1a68
particle_emitter_u size:     0x1b40
particle_t stride:           0x40
```

## Quick-Reference: MODGuard Live Particle Hook

```c
// Channel 3 callback (0x180033e90) — per-particle iteration
for (rbx = list_head; rbx != NULL; rbx += 0x40) {
    if (*(uint32_t*)(rbx + 0x10) == 5 /* smoke */) {
        float size = *(float*)(rbx + 0x18);
        if      (size < 100.0f)   size = 100.0f;
        else if (size < 2500.0f)  size = 2500.0f;
        else                      size = 10000.0f;
        *(float*)(rbx + 0x18) = size;
    }
}
```

## Author / Provenance

- **Author of MODGuard**: ToniVolma
- **License**: Proprietary (not open source)
- **Reason for reverse engineering**: The author does not provide a native Linux build. This analysis is for personal, non-commercial re-implementation on Linux systems.
