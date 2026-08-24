# Linux Particle Hook

This document describes the in-process particle hook. The hook lives in
`modguard-linux/src/particle_hook.{hpp,cpp}`,
`particle_signature.{hpp,cpp}`, and `particle_admission.{hpp,cpp}`. The hook
writes to executable game memory. It is the highest-risk subsystem in the
repository. The safety constraints in `../AGENTS.md` (Hook Safety section)
govern every change here.

The source of truth is the source code. Use `../reverse-enginnering/MODGuard_RE.md` only for the
analyzed Windows MinHook behavior.

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
- `particle_hook_frame_start()` — advances the per-frame admission sampler.
- `particle_hook_active()` — returns `true` between successful install and
  remove.
- `ParticleHookStats` / `ParticleHookDiagnostics` — counter structs. They are
  declared but the hook does not currently populate them.

The state shared between telemetry callbacks and hook callbacks is stored in
atomics: `demand`, `eligible`, `active`, `active_calls`
(`particle_hook.cpp:94-97`).

---

## 2. Hook profiles

A `HookProfile` (`particle_hook.cpp:59-74`) ties a game id and a version range
to the addresses, byte signatures, and classifier the hook uses. The current
profiles are (`particle_hook.cpp:76-92`):

| Game | Version | Model | Spawn offset | List-allocate offset | Constructor offset | Container offset | Node size | Classifier |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ATS | `1.06` (0x00010006) | `spawn_function_entry` | `0x1fadac0` | `0x1bd32b0` | `0x1fb23c0` | `0x640` | `0x60` | `classify_ats_160_smoke_generator` |
| ATS | `1.05` (0x00010005) | `allocator_trampoline` | `0x1fb2e20` | `0x1bd32b0` | `0x1fb23c0` | `0x640` | `0x60` | `classify_ats_155_smoke_container` |
| ETS2 | `1.05` (0x00010005) | `allocator_trampoline` | `0x1f47fb0` | `0x1b713f0` | `0x1f47640` | `0x548` | `0x58` | `classify_ets2_155_smoke_container` |

Each profile pins exact byte signatures:

- ATS 1.60 spawn: `ats_160_expected_spawn` (16 bytes,
  `particle_hook.cpp:54-57`), patch size 13.
- ATS 1.55 / ETS2 1.55 allocator: `ats_expected_allocator` /
  `ets2_expected_allocator` (16 bytes each,
  `particle_hook.cpp:37-44`), patch size 16.
- ATS constructor: `ats_expected_constructor` (18 bytes,
  `particle_hook.cpp:49-53`).
- ETS2 constructor: `ets2_expected_constructor` (16 bytes,
  `particle_hook.cpp:45-48`).

A profile is supported only when its version and machine-code checks pass. No
wildcard signatures or broad version ranges are permitted (`AGENTS.md`).

---

## 3. Hook models

The hook uses two models. The model depends on how the game spawns smoke
particles.

### 3.1 `allocator_trampoline` (ATS 1.55, ETS2 1.55)

The game smoke allocator is a small 16-byte function. The function body is a
`sub rdi, 0x20; mov esi, <node_size>; jmp <list_allocate>` thunk. The hook
replaces this thunk with a jump to `particle_allocator_hook`. The original
allocator is called via `list_allocate` directly (the profile stores its
offset). No trampoline is needed for the spawn path. The constructor is still
hooked so generators are registered (see section 5).

### 3.2 `spawn_function_entry` (ATS 1.60)

ATS 1.60 moves the per-particle decision into a dedicated spawn function. The
hook replaces the first 13 bytes of the spawn function entry with a jump to
`particle_spawn_hook`. It replaces the generator constructor entry with a jump
to `particle_generator_constructor_hook`. Both originals are reached through
mapped trampolines. The trampolines run the displaced bytes and jump back.

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
- `classify_ats_155_smoke_container` (`particle_signature.cpp:58`): the
  container points to `generator - 0x640`. It checks that the generator vtable
  `+0x0` equals `image_base + 0x2d15040`. It checks that the container vtable
  `+0x0` equals `image_base + 0x2d15010`. It reads count at `+0x80`, size at
  `+0x60`/`+0x64`, lifetime at `+0x7c`.
- `classify_ats_160_smoke_generator` (`particle_signature.cpp:76`): it takes
  the generator directly. It checks that vtable `+0x0` equals
  `image_base + 0x2d15040`. It reads count at `+0x630`, size at `+0x58`/`+0x5c`,
  lifetime at `+0x62c`.

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

The threshold is a 24-bit value. A uniform sample in `[0, 0xffffff]` is
admitted when it is below the threshold. `demand` is the expected admission
fraction. Existing particles and particle fields are never modified. Only the
allocation/spawn request itself is gated.

---

## 9. Install flow

`install_particle_hook` (`particle_hook.cpp:279`):

1. Reject if `active` is already set or `game_id` is null.
2. Find a matching profile by game id and version range. If none matches,
   return false.
3. Locate the main executable image via `dl_iterate_phdr(find_main_image, ...)`.
   Record `image_base` and the executable segment range
   (`particle_hook.cpp:120-135`).
4. Bounds-check the spawn and constructor addresses against the executable
   segment. If an address is out of range, return false.
5. Check that the spawn and constructor bytes match the expected signatures
   with `memcmp`. If either mismatches, return false (fail closed).
6. Resolve `list_allocate` from `image_base + profile->list_allocate_offset`.
7. Map the constructor trampoline. Copy the original constructor bytes. Append
   the jump back to the constructor continuation.
8. For `spawn_function_entry`, also map the spawn trampoline. Append its jump
   back.
9. Clear the generator registry. Reset the sampler.
10. Pin the plugin `.so` handle with `dlopen(..., RTLD_NOW | RTLD_NODELETE)`.
    This prevents the hook callbacks from being unloaded while patched code
    references them (`particle_hook.cpp:356-359`).
11. Apply the constructor patch. Then apply the spawn patch. Use `replace_code`.
12. If either patch fails, attempt to restore any successfully patched site.
    Wait for in-flight calls to drain. Unmap trampolines. Return false.
13. Set `active = true`.

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

`tests/particle_signature_test.cpp` builds synthetic generator/container byte
vectors for ETS2 1.55, ATS 1.55, and ATS 1.60. It sets vtable pointers and
layer fields at the documented offsets. It asserts that each classifier
accepts the three known layers. It asserts that each classifier rejects vtable
mismatches and non-smoke layers. No game binary is required.

`tests/particle_admission_test.cpp` asserts that two samplers produce identical
samples for the same frame and layer. It asserts that different layers produce
different samples. It asserts that consecutive calls to the same layer produce
different samples.

Run the complete CTest suite after any change to the hook
(see `linux-plugin-architecture.md` section 8).

---

## 13. Related documents

- `linux-plugin-architecture.md` — install/shutdown sequencing and the frame
  callbacks that drive `set_particle_demand` and `particle_hook_frame_start`.
- `linux-exhaust-model.md` — how `demand` is computed before it reaches the
  hook.
- `mod-smoke-variants.md` — the clean smoke-mod presets whose layer signatures
  the classifiers recognize.
- `../reverse-enginnering/MODGuard_RE.md` section 5 — the analyzed Windows MinHook behavior
  (research reference only).