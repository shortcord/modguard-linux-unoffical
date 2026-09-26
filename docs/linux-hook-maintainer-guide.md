# Linux Hook Maintainer Guide

This guide is for an agent that must add or repair a Linux ATS or ETS2 particle-hook profile.
The hook writes to executable game memory. Treat every profile change as high risk.

The current source is the authority. Use `reverse-enginnering/MODGuard_RE.md` for research evidence only.
Do not copy an old hypothesis into the active hook without new evidence.

## Source Files

Read these files before changing a profile:

- `modguard-linux/src/game_profiles.hpp` and `game_profiles.cpp` contain the shared profile table.
- `modguard-linux/src/particle_hook.cpp` installs, patches, and removes the live hook.
- `modguard-linux/src/particle_signature.cpp` contains game-object classifiers and profile selection.
- `modguard-linux/src/game_detector.cpp` reads an ELF file for offline detection.
- `modguard-linux/tools/modguard_check.cpp` is the command-line interface.
- `modguard-linux/tests/particle_signature_test.cpp` tests synthetic game-object layouts.
- `modguard-linux/tests/game_detector_test.cpp` tests synthetic ELF detection.
- `docs/linux-particle-hook.md` contains the detailed hook behavior and binary evidence.

The profile table is shared by the plugin and `modguard_check`. Do not add a signature to only one of these components.

## What the Detector Checks

The plugin receives `game_id` and `game_version` from the SCS Telemetry SDK.
The value called `game_version` is a telemetry schema version. It is not the visible game release number.

The plugin then performs these checks:

1. Find the main ELF image with `dl_iterate_phdr`.
2. Record the image base and executable segment range.
3. Select profiles with the matching game ID and telemetry version.
4. Prefer an exact GNU ELF build-ID match.
5. Use a pinned profile for every supported build. The built-in table has no
   unpinned fallback.
6. Make sure that hook, constructor, and list-allocator addresses are inside executable memory.
7. Compare the original bytes at the hook and constructor addresses.
8. Stop without changing memory when any check fails.

`modguard_check` applies the same profile build-ID and machine-code rules to a file.
It derives the game and profile from the matching profile entry because it has no telemetry callback.
If a future profile is unpinned, the machine-code signatures select that profile.
The current table has no unpinned profile.

Run the utility as follows:

```bash
modguard-linux/build/modguard_check "/path/to/amtrucks"
```

The utility prints the profile name and SCS game ID when the binary is supported.
It returns `0` for a supported binary, `1` for an unsupported binary, and `2` for a read or command-line error.

## Shared Profile Contract

Each `GameProfile` entry contains:

- A display name, such as `ATS 1.61.1.4s`.
- The SCS game ID, such as `ats` or `eut2`.
- The telemetry schema version passed to `install_particle_hook`.
- An optional exact GNU ELF build ID.
- The relative offsets of the spawn hook, constructor, and list allocator.
- The expected bytes and patch lengths for the hook and constructor.

The `profiles` table in `particle_hook.cpp` has the same order as `game_profiles()`.
Its entries contain hook model details, container offsets, node size, lifetime offset, and classifiers.
Keep both tables in the same order. The `static_assert` checks the number of entries
against `game_profile_count`. Update `game_profile_count` in `game_profiles.hpp` when you
add a profile.

Current active entries are:

| Profile | Game ID | Telemetry version | Build ID | Hook model |
| --- | --- | --- | --- | --- |
| ETS2 1.60.1.7s | `eut2` | `0x00010013` | `7fa4c9e7df8004a776d9e355e7a8512e07fe6072` | `spawn_function_entry` |
| ATS 1.60.1.8s | `ats` | `0x00010006` | `e109d085d57bbeca9a35c7a61df9703796568fe3` | `spawn_function_entry` |
| ATS 1.61.1.4s | `ats` | `0x00010007` | `bbad4aec77909880502d055c9e96d2fe23ec153f` | `list_allocate_entry` |
| ETS2 1.61.1.1s | `eut2` | `0x00010014` | `d2cb5aa082c669c8f5744ae329862f954dec71c2` | `list_allocate_entry` |
| ATS 1.61.2.0s | `ats` | `0x00010007` | `e515a3ae857a3a84319d3579b94ba3acbe31edb5` | `list_allocate_entry` |

The ATS 1.61.1.4s and ATS 1.61.2.0s profiles share a telemetry version.
The exact build ID selects the correct profile. All active profiles are pinned.
The ATS 1.60.x layout is verified for the ATS 1.60.1.8s executable only. Do not
add an unpinned ATS 1.60.x fallback without per-build machine-code evidence.
The same rule applies to the ETS2 1.61.1.x and ATS 1.61.x families.

## Adding a Profile

Follow this order:

1. Obtain the exact native Linux game executable and record its game release.
2. Record its SHA-256 and GNU ELF build ID.
3. Record the telemetry schema version passed by that executable.
4. Find the hook, constructor, and list-allocator offsets for that exact executable.
5. Record the original bytes at every patch site.
6. Confirm the executable ranges and all object offsets from disassembly or runtime evidence.
7. Add one `GameProfile` entry in `game_profiles.cpp`.
8. Add the matching hook behavior entry in `particle_hook.cpp`.
9. Add or update the classifier in `particle_signature.cpp` when the object layout changes.
10. Add synthetic memory tests for every new object layout.
11. Add a synthetic ELF test for the build ID and patch bytes.
12. Update `docs/linux-particle-hook.md` with the binary version and evidence.
13. Update the supported-version tables in the root and Linux READMEs.

Do not use wildcard bytes, broad version ranges, guessed offsets, or a build ID copied from another executable.
If the evidence is incomplete, leave the profile unsupported.

## Patch Safety

The install path must remain fail closed:

- Check the target bytes before each patch.
- Change page protection only for the patch operation.
- Clear the instruction cache after writing code.
- Restore the original bytes during shutdown.
- Keep trampolines mapped if restoration fails.
- Pass unrelated particle allocations to the original game function.
- Keep hook callbacks free of file I/O, socket I/O, locks, allocation, and logging.
- Use atomics for state shared with telemetry callbacks.

Do not make the offline detector a replacement for live installation checks.
The utility reads file bytes. The plugin must also validate live memory ranges and current target bytes.

The 1.55 allocator profiles remain disabled.
Those games can dereference a null node after allocator suppression, which can crash the game.
Do not reactivate them without new evidence and a safe suppression path.

## Verification

Build and run the complete suite after any hook or profile change:

```bash
cmake -S modguard-linux -B modguard-linux/build -DCMAKE_BUILD_TYPE=Release
cmake --build modguard-linux/build --parallel
ctest --test-dir modguard-linux/build --output-on-failure
```

Run the detector against each available reference executable:

```bash
modguard-linux/build/modguard_check "/path/to/amtrucks"
modguard-linux/build/modguard_check "/path/to/eurotrucks2"
```

Also run it against an unrelated ELF and a missing path.
The unrelated ELF must return `1`. The missing path must return `2`.

Do not test a new profile only with the utility.
The utility cannot prove that the live process has the expected loaded image or writable patch state.
