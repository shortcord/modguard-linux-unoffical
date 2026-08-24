# AGENTS.md

## Scope

These instructions apply to the complete repository.

This repository contains reverse-engineering evidence and a native Linux reimplementation. Keep factual research separate from implementation assumptions.

## Project Layout

- `reverse-enginnering/`: read-only reverse-engineering evidence.
  - `reverse-enginnering/MODGuard_RE.md`: canonical specification for the analyzed Windows plugin.
  - `reverse-enginnering/MODGuard.dll`: proprietary reference binary. Treat this file as read-only evidence.
  - `reverse-enginnering/PM_389_Smoke.zip`: companion mod archive. Treat as read-only evidence.
- `docs/`: supporting analysis, historical research, and implementation documentation. Some earlier documents contain superseded hypotheses.
- `docs/README.md`: index of research and implementation documents.
- `docs/linux-plugin-architecture.md`: native Linux plugin architecture, lifecycle, diagnostics, build, and tests.
- `docs/linux-exhaust-model.md`: `exhaust_demand_v0` model and the full `[RealisticSmoke]` configuration table.
- `docs/linux-particle-hook.md`: in-process particle hook, profiles, classifiers, and admission sampler.
- `docs/mod-smoke-variants.md`: clean smoke-mod presets and packaging.
- `docs/artefacts/`: generated disassembly, scripts, and raw research notes.
- `smoke-mod/`: the smoke mod source tree.
  - `smoke-mod/_shared/`: the extracted `PM_389_Smoke.zip` companion-mod tree. This is the shared base the variants overlay. It contains proprietary mod content kept verbatim; do not modify it.
  - `smoke-mod/soft-haze/` and `smoke-mod/subtle/`: clean replacement particle definitions and sprite texture for each preset. These are the only clean source overlays.
  - `smoke-mod/build-cmake/`: CMake build tree that holds the packaged variant archives (build output, not committed).
- `smoke-mod/CMakeLists.txt`: self-contained CMake packaging system. It overlays a `smoke-mod/<variant>/` preset onto the `smoke-mod/_shared/` tree. Build it with `cmake -S smoke-mod -B smoke-mod/build-cmake` and `cmake --build smoke-mod/build-cmake --target smoke_mod_variants`.
- `modguard-linux/`: C++17 implementation of the native Linux SCS Telemetry plugin.
- `modguard-linux/include/`: the minimal vendored SCS Telemetry SDK ABI.
- `modguard-linux/src/`: configuration, exhaust model, plugin entry points, and particle hook.
- `modguard-linux/modguard_linux.ini.example`: commented configuration template that mirrors the defaults and sanitize ranges.
- `modguard-linux/tests/`: standalone CTest executables that use `assert`.
- `modguard-linux/tools/listen.py`: diagnostic Unix datagram listener.

If the task does not require it, do not inspect `.opencode/`, `modguard-linux/build/`, or generated build directories.

## Source Of Truth

Use `reverse-enginnering/MODGuard_RE.md` as the primary research reference. Use the source code and tests as the source of truth for current Linux behavior. The implementation documents under `docs/` describe that behavior. These are `linux-plugin-architecture.md`, `linux-exhaust-model.md`, `linux-particle-hook.md`, and `mod-smoke-variants.md`. Keep them in sync with the source.

Use `https://modding.scssoft.com/wiki/Main_Page` as the official SCS wiki. It shows what ATS/ETS2 expects for resources.

Before you copy a claim from `docs/` into code or current documentation, compare it with `reverse-enginnering/MODGuard_RE.md`. Record new reverse-engineering conclusions with their binary, version, address, and supporting evidence.

If you do not have direct evidence, do not modify reference binaries, extracted mod data, hashes, offsets, or signatures. Keep proprietary content out of new source files.

## Build And Test

The git repository root is the project root. Run build commands from the project root:

```bash
cmake -S modguard-linux -B modguard-linux/build -DCMAKE_BUILD_TYPE=Release
cmake --build modguard-linux/build --parallel
ctest --test-dir modguard-linux/build --output-on-failure
```

The build creates `modguard-linux/build/modguard_linux.so`. Do not commit build output.

The smoke-mod variant archives are a separate, opt-in CMake project. Package them with:

```bash
cmake -S smoke-mod -B smoke-mod/build-cmake
cmake --build smoke-mod/build-cmake --target smoke_mod_variants
```

The output is `smoke-mod/build-cmake/PM_389_Smoke_<variant>.zip`. Do not commit build output.

If you change C++ code or CMake files, run the complete CTest suite. If behavior changes, add or update a focused test.

The tests must not require an installed game, network access, or proprietary binaries. Use synthetic memory layouts for particle-signature tests.

## C++ Conventions

- Use C++17 and the existing standard-library approach.
- If it does not implement the exported C ABI, keep code in the `modguard` namespace.
- Preserve the `extern "C"` names and ABI of `scs_telemetry_init` and `scs_telemetry_shutdown`.
- Compile cleanly with `-Wall -Wextra -Wpedantic`.
- Use fixed-width integer types for binary layouts, addresses, counters, and ABI-sensitive values.
- Use `F` suffixes for `float` literals.
- If they fit the existing code, use `nullptr`, brace initialization, and RAII.
- Keep headers self-contained and use the existing include-guard style.
- Keep comments short. Explain binary-layout facts and safety constraints, not routine control flow.

## Hook Safety

The particle hook writes to executable game memory. Treat every change in `particle_hook.cpp` and `particle_signature.cpp` as high risk.

- Keep hook profiles game-specific and version-specific.
- If a version, address, byte signature, vtable, or memory range does not match, make installation fail closed.
- Do not add wildcard signatures or broad version ranges.
- Before each patch, check the target bytes. During shutdown, restore the original bytes.
- Keep hook callbacks non-blocking. Do not add file I/O, socket I/O, locks, allocation, or logging to a hot hook callback.
- Use atomics for state shared between telemetry callbacks and hook callbacks.
- Do not modify unrelated particle generators and containers. Pass them to the original game function.
- Add synthetic signature tests for each supported memory layout.
- If a hook profile changes, document the game executable and evidence.

The current source contains profiles for ATS 1.55, ATS 1.60, and ETS2 1.55. A profile is supported only when its version and machine-code checks pass.

## Configuration And Telemetry

Keep configuration keys in the case-sensitive `[RealisticSmoke]` schema. If a file or value is invalid, preserve safe defaults.

`MODGUARD_CONFIG` overrides the configuration path. `MODGUARD_EXHAUST_SOCKET` overrides the diagnostic socket path and must contain an absolute path.

Keep diagnostic output non-blocking. Datagram loss must not affect particle control or the game process.

If the JSON schema changes, update `modguard-linux/README.md`, `tools/listen.py`, and plugin tests in the same change.

## Documentation

If the repository layout, supported games, or main workflow changes, update the root `README.md`. Update `modguard-linux/README.md` for plugin behavior, installation, configuration, and diagnostics.

The implementation documents under `docs/` describe current Linux behavior. These are `linux-plugin-architecture.md`, `linux-exhaust-model.md`, `linux-particle-hook.md`, and `mod-smoke-variants.md`. They are the source of truth for implemented behavior. The numbered research documents (`01-05`) record reverse-engineering analysis. They can contain superseded hypotheses. Do not present them as implemented behavior.

If the configuration schema changes, keep `modguard-linux/modguard_linux.ini.example` in sync. Match the defaults in `src/exhaust_model.hpp` and the sanitize ranges in `src/config.cpp`.

State the exact game version for addresses and object layouts. Label unconfirmed findings as hypotheses. Do not present historical plans in `docs/` as implemented behavior.

Use the simple-english skill when writting any documentation.