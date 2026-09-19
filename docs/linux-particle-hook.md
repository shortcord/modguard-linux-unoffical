# Linux Particle Hook

This document describes the in-process particle hook. The hook lives in
`modguard-linux/src/particle_hook.{hpp,cpp}`,
`particle_signature.{hpp,cpp}`, `game_profiles.{hpp,cpp}`, and
`particle_admission.{hpp,cpp}`. The hook
writes to executable game memory. It is the highest-risk subsystem in the
repository. The safety constraints in `../AGENTS.md` (Hook Safety section)
govern every change here.

The source of truth is the source code. Use `../reverse-enginnering/MODGuard_RE.md` only for the
analyzed Windows MinHook behavior.

For profile maintenance and the offline binary detector, see
[`linux-hook-maintainer-guide.md`](linux-hook-maintainer-guide.md).

---

## 1. Public interface

`particle_hook.hpp` exposes:

- `install_particle_hook(const char *game_id, std::uint32_t game_version)` —
  installs the hook for a matching profile. It returns `false` if no profile
  matches, if any byte signature fails, or if any patch cannot be applied.
- `remove_particle_hook()` — restores original bytes and unmaps trampolines.
- `set_particle_demand(float demand, bool eligible)` — publishes the demand and
  the eligibility used by the admission decision. The demand is clamped to
  `[0,1]`.
- `set_particle_lifetime_multiplier(float multiplier)` — publishes a scale
  applied to the per-layer `time_to_live` field on every admitted spawn. The
  multiplier is clamped to `[0, 60]`. A value of 1 leaves SII lifetimes
  untouched. A value of 20 makes layer 0 linger for about 60s when paired with
  the standard `time_to_live: 3.0`. The hook caches the original `time_to_live`
  captured at generator registration and writes `cached_original * multiplier`
  to the generator on each spawn. The override is idempotent. Successive
  spawns do not compound on the previous scaled value.
- `apply_lifetime_to_buffer(generator, lifetime_offset, cached_original, multiplier)`
  — writes `cached_original * multiplier` to the lifetime field at the given
  offset. The hook calls this helper for each registered generator. The helper
  is exposed for unit tests against synthetic memory layouts.
- `particle_hook_frame_start()` — advances the per-frame admission sampler.
- `particle_hook_active()` — returns `true` between successful install and
  remove.
- `particle_hook_diagnostics()` — returns cumulative constructor, candidate,
  registry, smoke, admission, and suppression counters.

The state shared between telemetry callbacks and hook callbacks is stored in
atomics. This state includes demand, eligibility, lifecycle state, active calls,
the lifetime multiplier, and the diagnostic counters.

---

## 2. Hook profiles

`GameProfile` in `modguard-linux/src/game_profiles.cpp` stores the game ID,
telemetry version, optional build ID, offsets, and patch bytes. The matching
`HookProfile` in `particle_hook.cpp` stores the hook model, object offsets, and
classifier. The current profiles are:

| Game | Version | Model | Hook offset | List-allocate offset | Constructor offset | Input offset | Node size | Lifetime offset | Classifier |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ETS2 | 1.60.1.7s, telemetry schema `1.19` (`0x00010013`) | `spawn_function_entry` | `0x1fa8ab0` | `0x1bce2a0` | `0x1fad3b0` | `0x640` | `0x60` | `0x62c` | `classify_ets2_160_smoke_generator` |
| ATS | 1.60.1.8s, telemetry schema `1.06` (`0x00010006`) | `spawn_function_entry` | `0x1fadac0` | `0x1bd32b0` | `0x1fb23c0` | `0x640` | `0x60` | `0x62c` | `classify_ats_160_smoke_generator` |
| ATS | 1.61.1.4s, telemetry schema `1.06` (`0x00010006`) | `list_allocate_entry` | `0x1cbfea0` | `0x1cbfea0` | `0x20aaec0` | `0x668` | `0x60` | `0x62c` | `classify_ats_161_smoke_generator` |

> **Disabled profiles.** The ATS 1.55 and ETS2 1.55
> `allocator_trampoline` profiles are removed from the profile table. They
> are not installed. Reason: the game reads the node that the patched
> allocator returns without a NULL check in list-unlink paths. Suppression
> by returning `nullptr` crashes the game. Crash evidence: ATS dumps
> (Aug 2026) show `SIGSEGV` at addresses `0x48`, `0x30`, `0x60`, and `0x0`.
> The faulting code is the node-unlink function at ATS VA `0x1c3e620`
> (ATS 1.55.0.0). It walks `node->next` links at `+0x00` and stores
> `node + 0x48`. A `NULL` node makes the game read `NULL + 0x48`. The 1.60
> `spawn_function_entry` profiles do not have this problem. Their
> suppression path simply does not call the original spawn function.

Each active profile stores exact spawn and constructor byte arrays in
`game_profiles.cpp`. The `spawn_size` and `constructor_size` fields define the
bytes that the plugin checks and patches. The arrays can be larger than the
patch length because they also document the full observed function prefix.

A profile is supported only when its fingerprint and machine-code checks pass.
No wildcard signatures or broad version ranges are permitted (`AGENTS.md`).
The ETS2 1.60.1.7s, ATS 1.60.1.8s, and ATS 1.61.1.4s profiles require an exact
ELF build ID. For these profiles, the build ID is authoritative after the game
ID matches. All active profiles are pinned. The ATS 1.60.x layout is verified
for the ATS 1.60.1.8s executable only, so the ATS 1.60.x family is not accepted
as a version fallback. `select_particle_profile` still supports an unpinned
profile, but the built-in table has none.

---

## 3. Hook models

The hook uses three models. The model depends on how the game spawns smoke
particles.

### 3.1 `allocator_trampoline` (disabled — ATS 1.55, ETS2 1.55)

The game smoke allocator is a small 16-byte function. The function body is a
`sub rdi, 0x20; mov esi, <node_size>; jmp <list_allocate>` thunk. The hook
replaces this thunk with a jump to `particle_allocator_hook`. The original
allocator is called via `list_allocate` directly (the profile stores its
offset). No trampoline is needed for the spawn path. The constructor is still
hooked so generators are registered (see section 5).

This model is disabled. When the hook suppresses a spawn, it returns `NULL`.
The 1.55 games dereference that node in list-unlink paths without a NULL
check. The game crashes. See section 2 for the evidence.

### 3.2 `spawn_function_entry` (ATS 1.60.1.8s and ETS2 1.60.1.7s)

These game versions use a dedicated spawn function for the per-particle decision. The
hook replaces the first 13 bytes of the spawn function entry with a jump to
`particle_spawn_hook`. It replaces the generator constructor entry with a jump
to `particle_generator_constructor_hook`. Both originals are reached through
mapped trampolines. The trampolines run the displaced bytes and jump back.

### 3.3 `list_allocate_entry` (ATS 1.61.1.4s)

ATS 1.61.1.4s calls a shared list allocator for each particle node. The hook
replaces the first 13 bytes of this allocator with a jump. The callback passes
all unrelated allocations to the trampoline without changes.

Particle calls pass `generator + 0x668` and request a `0x60`-byte node. The
callback subtracts `0x668` only after it checks the node size and pointer. It
then checks the constructor registry and the ATS 1.61 vtable.

All three ATS 1.61 particle call sites check the allocator result for
`nullptr`. Thus, the callback can safely suppress a recognized smoke request.

---

## 4. Trampoline construction

Trampolines are 4096-byte anonymous executable mappings
(`PROT_READ | PROT_WRITE | PROT_EXEC`, `MAP_PRIVATE | MAP_ANONYMOUS`)
(`particle_hook.cpp:312-317`, `particle_hook.cpp:327-342`).

A trampoline contains:

1. The original displaced bytes. These are copied verbatim from the patched
   site.
2. A 12-byte absolute jump (`mov rax, <addr>; jmp rax`) back to the
   instruction after the patched site. It is built by `jump_patch`
   (`particle_hook.cpp:146-158`). Unused bytes are filled with `0x90` (NOP).

The same `jump_patch` form builds the replacement patch. This patch redirects
the game code into the hook. A patch is applied only when `size` is in
`[12, maximum_patch_size]`.

`replace_code` (`particle_hook.cpp:160-172`):

1. Checks that the target bytes match the expected signature with `memcmp`.
2. Calls `mprotect` to make the page read/write/executable.
3. Copies the replacement bytes.
4. Clears the instruction cache with `__builtin___clear_cache`.
5. Restores read/executable protection.

If any check fails, no bytes are changed.

---

## 5. Generator registry

The constructor hook registers every smoke-particle generator. This lets the
allocator/spawn hook tell which generators are the companion mod's layers.

`particle_generator_constructor_hook` (`particle_hook.cpp:270-275`) calls the
original constructor. Then it calls `register_generator(generator)`. The
registry is a fixed-size open-addressed hash table of
`std::atomic<void *>` slots (`particle_hook.cpp:99`). Its capacity is 16384. It
is keyed by `(generator >> 4) & (capacity - 1)`
(`particle_hook.cpp:176-188`). Probing is linear. The table never resizes.

`is_registered_container` (`particle_hook.cpp:190-202`) recovers the owning
generator from a container pointer. It subtracts
`profile->container_offset`, then probes the table.
`is_registered_generator` (`particle_hook.cpp:204-215`) probes directly.

The registry is cleared at install time. It is never reused across installs
(`particle_hook.cpp:344`).

---

## 6. Layer classification

`particle_signature.{hpp,cpp}` classify a generator or container as one of the
three documented smoke layers. They return `-1` if the object is not a
recognized layer.

The three layers are identified by particle count, sprite size, and lifetime
(`particle_signature.cpp:24-36`):

| Layer | `particles_max_count` | `particle_size` (x, y) | `time_to_live` |
| --- | --- | --- | --- |
| 0 (haze) | 40 | 0.090 | 3.0 |
| 1 (mid)  | 20 | 0.040 | 0.7 |
| 2 (core) | 4  | 0.020 | 0.2 |

Floats match with a tolerance of `0.0005F` (`near`, `particle_signature.cpp:19`).

### 6.1 Per-game classifiers

Each classifier first checks the vtable pointer(s) against the image base.
Then it reads the layer fields at game-specific offsets:

- `classify_ets2_155_smoke_container` (`particle_signature.cpp:40`): the
  container points to `generator - 0x548`. It checks that the generator vtable
  at `+0x0` equals `image_base + 0x2c99180`. It checks that the container
  vtable at `+0x10` equals `image_base + 0x2c99150`. It reads count at `+0x70`,
  size at `+0x54`/`+0x58`, lifetime at `+0x524`.
- `classify_ats_155_smoke_container` (`particle_signature.cpp:74`): the
  container points to `generator - 0x640`. It checks that the generator vtable
  `+0x0` equals `image_base + 0x2d15040`. It checks that the container vtable
  `+0x0` equals `image_base + 0x2d15010`. It reads count at `+0x80`, size at
  `+0x60`/`+0x64`, lifetime at `+0x7c`.
- `classify_ats_160_smoke_generator` (`particle_signature.cpp:92`): it takes
  the generator directly. It checks that vtable `+0x0` equals
  `image_base + 0x2d15040`. It reads count at `+0x630`, size at `+0x58`/`+0x5c`,
  lifetime at `+0x62c`. This classifier serves the ATS 1.60.1.8s profile.
- `classify_ets2_160_smoke_generator` (`particle_signature.cpp:58`): it takes
  the generator directly. It checks that vtable `+0x0` equals
  `image_base + 0x2d0f600`. It reads count at `+0x630`, size at `+0x58`/`+0x5c`,
  and lifetime at `+0x62c`.
- `classify_ats_161_smoke_generator` (`particle_signature.cpp:108`): it takes
  the generator directly. It
  checks that the `particle_gen_u` base vtable at `+0x0` equals
  `image_base + 0x2e41fc8`. It reads count at `+0x630`, size at
  `+0x54`/`+0x58`, and lifetime at `+0x62c`. The size is a two-float
  `particle_size` pair. The adjacent `+0x5c` field is `particle_size_var` and
  is not part of the signature.

Any vtable mismatch or layer mismatch returns `-1`. The request is then passed
to the original allocator unchanged.

> Public-unit identity matching remains the next hardening step. A different
> mod that uses an identical layer definition can also match. See
> `modguard-linux/README.md`.

---

## 7. Admission sampler

`particle_admission.{hpp,cpp}` implement `LayerSampler`. This is a synchronized
per-layer admission sampler.

Each frame, `frame_start()` increments `frame_` and zeroes the three per-layer
ordinal counters (`particle_admission.cpp:16-20`). `sample(layer)` increments
that layer's ordinal. It returns a 24-bit hash of
`(frame, layer, ordinal)` (`particle_admission.cpp:22-30`):

```
mix((frame << 24) | (layer << 8) | ordinal) & 0xffffff
```

`mix` is a splitmix64-style finalizer (`particle_admission.cpp:7-12`). Layer
indices outside `[0, 2]` are clamped to the nearest valid layer.

The seed is `(frame, layer, ordinal)`. Matching generators on different pipes
receive the identical admission decision for the same layer and ordinal.
Consecutive requests in a frame stay independent. This keeps multi-pipe
setups consistent. Each pipe admits the same fraction of smoke over time.
Sprite position, rotation, lifetime, and size randomization stay independent.

---

## 8. Admission decision

The hook callbacks (`particle_allocator_hook`, `particle_spawn_hook`) share the
same decision logic (`particle_hook.cpp:227-268`):

1. `CallGuard` increments `active_calls` for the duration of the callback. This
   lets shutdown wait for in-flight calls to drain
   (`particle_hook.cpp:115-118`).
2. Classify the layer via the profile's classifier. If the result is `-1`, call
   the original allocator/spawn unchanged (pass-through).
3. If `eligible` is false, suppress. Return `nullptr` for the allocator. Return
   for the spawn.
4. Read `demand` and compute `threshold = (uint32)(demand * 16777216.0F)`.
5. If `threshold == 0`, suppress.
6. If `threshold != 0x1000000` (not fully open) and
   `sampler.sample(layer) >= threshold`, suppress.
7. Otherwise, call the original allocator/spawn.

The callback also updates cumulative diagnostic counters. These counters identify constructor calls, candidate calls, registry matches, smoke matches, admissions, and suppressions. Each update uses a relaxed atomic operation. The callback does not do logging, allocation, socket I/O, or locking.

The threshold is a 24-bit value. A uniform sample in `[0, 0xffffff]` is
admitted when it is below the threshold. `demand` is the expected admission
fraction. Existing particles and particle fields are never modified. Only the
allocation/spawn request itself is gated.

---

## 9. Install flow

`install_particle_hook` (`particle_hook.cpp:279`):

1. Reject if `active` is already set or `game_id` is null.
2. Locate the main executable image via `dl_iterate_phdr(find_main_image, ...)`.
   Record `image_base` and the executable segment range
   (`particle_hook.cpp:120-135`).
3. Find all profiles that match the game id and telemetry version.
4. Select the exact build-ID match. The built-in table has no unpinned
   fallback, so a different build of the same game version is not supported.
5. Bounds-check the hook, list-allocator, and constructor addresses against the executable
   segment. If an address is out of range, return false.
6. Check that the spawn and constructor bytes match the expected signatures
   with `memcmp`. If either mismatches, return false (fail closed).
7. Resolve `list_allocate` from `image_base + profile->list_allocate_offset`.
8. Map the constructor trampoline. Copy the original constructor bytes. Append
   the jump back to the constructor continuation.
9. For an entry hook, also map its trampoline. Append its jump back.
10. Clear the generator registry. Reset the sampler.
11. Pin the plugin `.so` handle with `dlopen(..., RTLD_NOW | RTLD_NODELETE)`.
    This prevents the hook callbacks from being unloaded while patched code
    references them (`particle_hook.cpp:356-359`).
12. Apply the constructor patch. Then apply the spawn patch. Use `replace_code`.
13. If either patch fails, attempt to restore any successfully patched site.
    Wait for in-flight calls to drain. Unmap trampolines. Return false.
14. Set `active = true`.

---

## 10. Shutdown flow

`remove_particle_hook` (`particle_hook.cpp:398`):

1. If the hook is not active, return.
2. Clear `eligible`. This makes the callbacks stop admitting immediately.
3. Rebuild the current patch bytes. Attempt to restore both sites via
   `replace_code`. This restores the expected original bytes.
4. If both restores succeed, set `active = false`. Wait for `active_calls` to
   drain. Unmap the trampolines.
5. If either restore fails, the hook stays active. The trampolines stay mapped.
   This is deliberate. Unmapping a trampoline while patched game code still
   references it would crash the game. The plugin logs the condition. It leaves
   the hook in place.

### 10.1 Patch rollback

`replace_code` calls `apply_code_patch`. That helper checks the target bytes,
makes the page writable and executable, writes the patch, then restores
read-execute protection. If the restore fails, the helper reverts the bytes
while the page is still writable, clears the instruction cache, and tries the
restore again. It returns `false`.

A `false` return always means the target bytes are unchanged. The caller treats
the patch as not installed. This keeps the fail-closed rule. The page is not
left both patched and writable. `tests/hook_patch_test.cpp` covers this rollback
with an injected protector that fails the first restore.

---

## 11. Safety constraints

These constraints are mandatory for any change to `particle_hook.cpp`,
`particle_signature.cpp`, or `particle_admission.cpp` (from `AGENTS.md`):

- Keep hook profiles game-specific and version-specific. No wildcard
  signatures or broad version ranges.
- Fail closed when a version, address, byte signature, vtable, or memory range
  does not match.
- Check the target bytes before each patch. Restore the original bytes during
  shutdown.
- Keep hook callbacks non-blocking. No file I/O, socket I/O, locks,
  allocation, or logging in a hot callback.
- Use atomics for state shared between telemetry callbacks and hook callbacks.
- Pass unrelated particle generators and containers to the original game
  function without modification.
- Add synthetic signature tests for each supported memory layout.
- Document the game executable and the evidence when a hook profile changes.

The callback bodies in `particle_hook.cpp:227-268` perform only atomic loads,
fixed arithmetic, a sampler call, and a tail call to the original. There is no
allocation. There is no logging. There is no blocking.

---

## 12. Tests

`tests/particle_signature_test.cpp` builds synthetic generator and container
buffers for all supported layouts. These layouts include ATS 1.61. The test sets
vtable pointers and layer fields at the documented offsets. It asserts that each classifier
accepts the three known layers. It asserts that each classifier rejects vtable
mismatches and non-smoke layers. It also checks exact build-ID profile selection.
No game binary is required.

`tests/particle_admission_test.cpp` asserts that two samplers produce identical
samples for the same frame and layer. It asserts that different layers produce
different samples. It asserts that consecutive calls to the same layer produce
different samples.

Run the complete CTest suite after any change to the hook
(see `linux-plugin-architecture.md` section 8).

### 12.1 ETS2 1.60.1.7s evidence

The ETS2 1.60.1.7s profile comes from the native Linux executable with SHA-256
`3b83b35dfe1fc2538111f496cad9e8c0366352ad374c89f024a21031a629d503`.
Its ELF build ID is `7fa4c9e7df8004a776d9e355e7a8512e07fe6072`.
The install flow requires this exact build ID before it changes executable memory.

Static data flow shows that this executable passes `0x00010013` as
`scs_sdk_init_params_t::game_version`. This value is telemetry schema version
1.19. It is not the game release number.

The spawn function starts at `0x1fa8ab0`. Its first 16 bytes are
`55 48 89 e5 41 57 41 56 49 89 fe 41 55 41 54 53`.
The constructor starts at `0x1fad3b0`. Its first 18 bytes are
`55 48 89 e5 41 55 41 bd 00 01 00 00 41 54 53 48 89 fb`.
The constructor installs the generator vtable at `0x2d0f600`.

The spawn code reads count at `+0x630`, size at `+0x58` and `+0x5c`, and
lifetime at `+0x62c`. It calls the list allocator at `0x1bce2a0` with node size
`0x60`. These facts support the profile and its synthetic memory test.

### 12.2 ATS 1.60.1.8s evidence

The ATS 1.60.1.8s profile comes from the native Linux executable with SHA-256
`24c8cbded3eec5b703b7c326b654d5a34d7df1d04a2a2460ac7cc7f826b4be18`.
Its ELF build ID is `e109d085d57bbeca9a35c7a61df9703796568fe3`.
The install flow requires this exact build ID before it changes executable memory.

The recorded telemetry version is `0x00010006`. ATS 1.61.1.4s uses the same
value. The install flow uses the exact build ID to select the correct ATS
profile. The ATS 1.60.x family is pinned to this one build. A different
ATS 1.60.x build is not supported.

The spawn function starts at `0x1fadac0`. Its first 16 bytes are
`55 48 89 e5 41 57 41 56 49 89 fe 41 55 41 54 53`.
The constructor starts at `0x1fb23c0`. Its first 18 bytes are
`55 48 89 e5 41 55 41 bd 00 01 00 00 41 54 53 48 89 fb`.
The constructor installs the generator vtable at `0x2d15040`.

The spawn code reads count at `+0x630`, size at `+0x58` and `+0x5c`, and
lifetime at `+0x62c`. It calls the list allocator at `0x1bd32b0` with node size
`0x60`. The generator container offset is `0x640`. These facts support the
profile and its synthetic memory test. The 1.61 `particle_size` pair at
`+0x54`/`+0x58` and the `particle_emitter_u` vtable are not present in this
build, so the 1.60 classifier applies.

### 12.3 ATS 1.61.1.4s evidence

The ATS 1.61.1.4s profile comes from the native Linux executable with SHA-256
`c2d1b3d2eace307b68d4a8f945ef0fc35b6d334a68377a635e35170b0ddb1d37`.
Its ELF build ID is `bbad4aec77909880502d055c9e96d2fe23ec153f`.

The recorded telemetry version is `0x00010006`. ATS 1.60.1.8s uses the same
value. The install flow uses the exact build ID to select the ATS 1.61 profile.

The list allocator starts at `0x1cbfea0`. Its patched bytes are
`55 48 89 e5 41 54 48 83 ec 18 4c 8b 27`. The base constructor starts at
`0x20aaec0` and installs the `particle_gen_u` vtable address point
`0x2e41fc8`. The derived `particle_emitter_u` constructors replace it with
the runtime vtable address point `0x2e416f8`.

The particle paths pass `generator + 0x668` to the allocator. They request a
`0x60`-byte node and check for `nullptr`. At the allocator call, the generator
uses the base vtable at `0x2e41fc8`.

The spawn path loads the two-float `particle_size` pair from `+0x54` at
`0x20a7ceb`. It reads `particle_displacement` from `+0x64`, `+0x68`, and
`+0x6c` at `0x20a7dda`, `0x20a7dc1`, and `0x20a7da4`. The count remains at
`+0x630`. The lifetime remains at `+0x62c`.

A runtime capture from this build recorded 1,344 registered candidates. All
1,344 candidates used the base vtable, and none used the emitter vtable. A
bounded float probe found matches at `+0x54`, `+0x58`, `+0x64`, and `+0x6c`.
The `+0x54`/`+0x58` pair is `particle_size`. The `+0x64`/`+0x6c` values are
`particle_displacement` and coincide with the layer 2 size. The old
`+0x58`/`+0x5c` check failed because `+0x5c` is `particle_size_var`.

---

## 13. Field-offset diagnostic technique

Use this technique when a layer classifier stops matching after a game update.
It found the ATS 1.61 `particle_size` offset. It needs only a diagnostic build.
It does not change admission.

1. Add a temporary counter per signature stage: vtable match, count match, size
   match, lifetime match, and complete tuple. Publish them in the datagram.
2. Run the game through the affected event. The first counter that stays at zero
   identifies the failing stage. A capture showed all candidates matched the
   count and lifetime but none matched the size.
3. Add a temporary bitmask probe for the failing field. Scan aligned floats in a
   small fixed range. Set one bit per offset that holds a documented value. OR
   the result into one relaxed atomic and publish it.
4. Run again. Decode the mask to offsets. Compare each offset against a static
   disassembly of the same build. Confirm which offset is the real field.
5. Correct the classifier. Add a synthetic test for the new offset and a
   rejection test for the old offset. Remove the probe and the stage counters.
6. Record the game build, the evidence addresses, and the result here.

Keep each temporary counter atomic and non-blocking. Follow the hot-path rules
in section 11. Remove the probe when the field is confirmed. The ATS 1.61 case
used base vtable `0x2e41fc8`, count `+0x630`, size `+0x54`/`+0x58`, and
lifetime `+0x62c`.

---

## 14. Related documents

- `linux-plugin-architecture.md` — install/shutdown sequencing and the frame
  callbacks that drive `set_particle_demand` and `particle_hook_frame_start`.
- `linux-exhaust-model.md` — how `demand` is computed before it reaches the
  hook.
- `mod-smoke-variants.md` — the clean smoke-mod presets whose layer signatures
  the classifiers recognize.
- `../reverse-enginnering/MODGuard_RE.md` section 5 — the analyzed Windows MinHook behavior
  (research reference only).
