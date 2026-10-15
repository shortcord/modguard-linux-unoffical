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
  registry, smoke, admission, suppression, and per-band dynamic counters.

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
| ATS | 1.61.1.4s, telemetry schema `1.07` (`0x00010007`) | `list_allocate_entry` | `0x1cbfea0` | `0x1cbfea0` | `0x20aaec0` | `0x668` | `0x60` | `0x62c` | `classify_ats_161_smoke_generator` |
| ETS2 | 1.61.1.1s, telemetry schema `1.20` (`0x00010014`) | `list_allocate_entry` | `0x1cb3d40` | `0x1cb3d40` | `0x209ed60` | `0x668` | `0x60` | `0x62c` | `classify_ets2_161_smoke_generator` |
| ATS | 1.61.2.0s, telemetry schema `1.07` (`0x00010007`) | `list_allocate_entry` | `0x1cc1310` | `0x1cc1310` | `0x20ac330` | `0x668` | `0x60` | `0x62c` | `classify_ats_162_smoke_generator` |
| ATS | 1.61.3.0s, telemetry schema `1.07` (`0x00010007`) | `list_allocate_entry` | `0x1cc1660` | `0x1cc1660` | `0x20ac680` | `0x668` | `0x60` | `0x62c` | `classify_ats_163_smoke_generator` |
| ATS | 1.61.3.1s, telemetry schema `1.07` (`0x00010007`) | `list_allocate_entry` | `0x1cc1840` | `0x1cc1840` | `0x20ac860` | `0x668` | `0x60` | `0x62c` | `classify_ats_164_smoke_generator` |
| ATS | 1.61.3.18s, telemetry schema `1.08` (`0x00010008`) | `list_allocate_entry` | `0x1cc4970` | `0x1cc4970` | `0x20af990` | `0x668` | `0x60` | `0x62c` | `classify_ats_165_smoke_generator` |

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
Every active profile requires an exact ELF build ID. The build ID is
authoritative after the game ID matches. All active profiles are pinned. The
ATS 1.60.x layout is verified for the ATS 1.60.1.8s executable only, so the ATS
1.60.x family is not accepted as a version fallback. The same rule applies to
the ETS2 1.61.1.x and ATS 1.61.x families. `select_particle_profile` still
supports an unpinned profile, but the built-in table has none.

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

### 3.3 `list_allocate_entry` (ATS 1.61.1.4s, ETS2 1.61.1.1s, ATS 1.61.2.0s, ATS 1.61.3.0s, ATS 1.61.3.1s, ATS 1.61.3.18s)

These builds call a shared list allocator for each particle node. The hook
replaces the first 13 bytes of this allocator with a jump. The callback passes
all unrelated allocations to the trampoline without changes.

Particle calls pass `generator + 0x668` and request a `0x60`-byte node. The
callback subtracts `0x668` only after it checks the node size and pointer. It
then checks the constructor registry and the build-specific `particle_gen_u`
vtable.

The particle call sites in these builds check the allocator result for
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

## 6. Generator classification

`particle_signature.{hpp,cpp}` classify a generator or container as one smoke
identity value. They return `particle_identity_none` (`-1`) if the object is
not a recognized smoke generator.

The construction hook records the identity when the generator is registered.
It also records the original lifetime. If the constructor does not yet have the
final SII values, the first spawn classifies live and records the result. Later
spawns then use the cached identity. This keeps classification correct after the
lifetime override rewrites `+0x62c` (section 1).

### 6.1 Legacy layers

The three legacy layers use particle count, sprite size, and lifetime
(`particle_signature.cpp`, `smoke_identity`):

| Identity | Name | `particles_max_count` | `particle_size` (x, y) | `time_to_live` |
| --- | --- | --- | --- | --- |
| 0 | haze | 40 | 0.090 | 3.0 |
| 1 | mid  | 20 | 0.040 | 0.7 |
| 2 | core | 4  | 0.020 | 0.2 |

Floats match with a tolerance of `0.0005F` (`near`, `particle_signature.cpp:19`).

### 6.2 Dynamic-blend bands

The `dynamic-blend` smoke-mod preset (see `mod-smoke-variants.md`) defines three
appearance bands. All three share count `24` and lifetime `3.0`. Only the base
size differs, so every band requests particles at the same rate:

| Identity | Name | `particles_max_count` | `particle_size` (x, y) | `time_to_live` |
| --- | --- | --- | --- | --- |
| 3 | low    | 24 | 0.030 | 3.0 |
| 4 | medium | 24 | 0.060 | 3.0 |
| 5 | high   | 24 | 0.120 | 3.0 |

Equal request rate is necessary for blending. The plugin splits the demand
across the bands by weighted admission (section 8). The weights sum to one, so
the total emission stays the same.

The bands use a different count from the legacy layers. The sizes are far apart
and away from the legacy sizes, so the `0.0005F` tolerance cannot confuse a
band with another identity. A tuple that matches no identity returns
`particle_identity_none`.

### 6.3 Per-game classifiers

Each classifier first checks the vtable pointer(s) against the image base.
Then it reads the identity fields at game-specific offsets:

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
- `classify_ats_161_smoke_generator` (`particle_signature.cpp`): it takes
  the generator directly. It
  checks that the `particle_gen_u` base vtable at `+0x0` equals
  `image_base + 0x2e41fc8`. It reads count at `+0x630`, size at
  `+0x54`/`+0x58`, and lifetime at `+0x62c`. The size is a two-float
  `particle_size` pair. The adjacent `+0x5c` field is `particle_size_var` and
  is not part of the signature.
- `classify_ets2_161_smoke_generator` (`particle_signature.cpp`): it takes the
  generator directly. It uses the shared 1.61 helper with the ETS2 1.61.1.1s
  `particle_gen_u` vtable `image_base + 0x2e35488`. It reads count at `+0x630`,
  size at `+0x54`/`+0x58`, and lifetime at `+0x62c`.
- `classify_ats_162_smoke_generator` (`particle_signature.cpp`): it takes the
  generator directly. It uses the shared 1.61 helper with the ATS 1.61.2.0s
  `particle_gen_u` vtable `image_base + 0x2e43508`. It reads count at `+0x630`,
  size at `+0x54`/`+0x58`, and lifetime at `+0x62c`.
- `classify_ats_163_smoke_generator` (`particle_signature.cpp`): it takes the
  generator directly. It uses the shared 1.61 helper with the ATS 1.61.3.0s
  `particle_gen_u` vtable `image_base + 0x2e43868`. It reads count at `+0x630`,
  size at `+0x54`/`+0x58`, and lifetime at `+0x62c`.
- `classify_ats_164_smoke_generator` (`particle_signature.cpp`): it takes the
  generator directly. It uses the shared 1.61 helper with the ATS 1.61.3.1s
  `particle_gen_u` vtable `image_base + 0x2e43a48`. It reads count at `+0x630`,
  size at `+0x54`/`+0x58`, and lifetime at `+0x62c`.
- `classify_ats_165_smoke_generator` (`particle_signature.cpp`): it takes the
  generator directly. It uses the shared 1.61 helper with the ATS 1.61.3.18s
  `particle_gen_u` vtable `image_base + 0x2e46dc8`. It reads count at `+0x630`,
  size at `+0x54`/`+0x58`, and lifetime at `+0x62c`.

`classify_particle_gen_u_smoke` (`particle_signature.cpp`) holds the shared
1.61 layout. It checks the vtable against a caller-supplied offset. Then it
classifies the identity from count, size, and lifetime. Each build supplies its
own vtable, so the check stays pinned to one executable. The ETS2 1.61.1.1s,
 ATS 1.61.2.0s, ATS 1.61.3.0s, ATS 1.61.3.1s, and ATS 1.61.3.18s classifiers call this helper.

Any vtable mismatch or identity mismatch returns `particle_identity_none`. The
request is then passed to the original allocator unchanged.

> Public-unit identity matching remains the next hardening step. A different
> mod that uses an identical definition can also match. See
> `modguard-linux/README.md`.

---

## 7. Admission sampler

`particle_admission.{hpp,cpp}` implement `LayerSampler`. This is a synchronized
per-identity admission sampler.

Each frame, `frame_start()` increments `frame_` and zeroes the six per-identity
ordinal counters (`particle_admission.cpp`). `sample(identity)` increments that
identity's ordinal. It returns a 24-bit hash of `(frame, identity, ordinal)`:

```
mix((frame << 24) | (identity << 8) | ordinal) & 0xffffff
```

`mix` is a splitmix64-style finalizer (`particle_admission.cpp:7-12`). Identity
indices outside `[0, particle_identity_max]` are clamped to the nearest valid
stream.

The seed is `(frame, identity, ordinal)`. Matching generators on different pipes
receive the identical admission decision for the same identity and ordinal.
Consecutive requests in a frame stay independent. This keeps multi-pipe
setups consistent. Each pipe admits the same fraction of smoke over time.
Sprite position, rotation, lifetime, and size randomization stay independent.

---

## 8. Admission decision

The hook callbacks (`particle_allocator_hook`, `particle_spawn_hook`) share the
same decision logic (`admit_request`, `particle_hook.cpp`):

1. `CallGuard` increments `active_calls` for the duration of the callback. This
   lets shutdown wait for in-flight calls to drain.
2. Look up the generator in the registry and read its cached identity. If the
   identity is not cached yet, classify it live and record the result. If the
   identity is `particle_identity_none`, call the original allocator/spawn
   unchanged (pass-through).
3. If `eligible` is false, suppress. Return `nullptr` for the allocator. Return
   for the spawn.
4. Read `demand`. For a legacy identity, use `demand` as the admission
   fraction. For a dynamic-blend identity, use the identity's band weight
   (section 8.1).
5. Compute `threshold = (uint32)(fraction * 16777216.0F)`.
6. If `threshold == 0`, suppress.
7. If `threshold != 0x1000000` (not fully open) and
   `sampler.sample(identity) >= threshold`, suppress.
8. Otherwise, call the original allocator/spawn.

The callback also updates cumulative diagnostic counters. These counters identify
constructor calls, candidate calls, registry matches, smoke matches, admissions,
suppressions, and the per-band dynamic requests and admissions. Each update uses
a relaxed atomic operation. The callback does not do logging, allocation, socket
I/O, or locking.

The threshold is a 24-bit value. A uniform sample in `[0, 0xffffff]` is
admitted when it is below the threshold. The admission fraction is the expected
admission fraction. Existing particles and particle fields are never modified.
Only the allocation/spawn request itself is gated.

### 8.1 Dynamic-blend weights

`particle_blend.{hpp,cpp}` map demand in `[0, 1]` to three band weights. The
weights sum to one:

| Demand | low | medium | high |
| --- | --- | --- | --- |
| 0.00 | 1.0 | 0.0 | 0.0 |
| 0.25 | 0.5 | 0.5 | 0.0 |
| 0.50 | 0.0 | 1.0 | 0.0 |
| 0.75 | 0.0 | 0.5 | 0.5 |
| 1.00 | 0.0 | 0.0 | 1.0 |

The low band fades out as demand passes 0.5. The high band fades in. Below
demand 0.5, the visible bands are low and medium. A non-finite demand is
treated as zero. An out-of-range demand is clamped to `[0, 1]`.

A generator creates one particle per admitted request. The blend selects which
band creates each new particle. Existing particles are not changed. They expire
by their own lifetime. The result is a natural crossfade from small and light
to large and dark as the demand rises.

The blend is dormant unless the active mod uses the dynamic-blend identities
(section 6.2). The legacy three-layer presets keep the original behavior.

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

The recorded telemetry version is `0x00010006` (schema 1.06). The static
telemetry descriptor at `0x295de20` records `game_id = ats` and this value.
The install flow uses the exact build ID to select the correct ATS profile.
The ATS 1.60.x family is pinned to this one build. A different ATS 1.60.x
build is not supported.

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

The static telemetry descriptor at `0x2a67ca0` records `game_id = ats` and
`schema = 0x00010007` (1.07). The plugin receives `0x00010007` from
`scs_telemetry_init`. ATS 1.61.2.0s records the same schema value. The install
flow uses the exact build ID to select the ATS 1.61 profile.

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

### 12.4 ETS2 1.61.1.1s evidence

The ETS2 1.61.1.1s profile comes from the native Linux executable with SHA-256
`16659c0454622cb9718f8cbfc08af198f7143632f8a01c55f8620278e2d43bac`.
Its ELF build ID is `d2cb5aa082c669c8f5744ae329862f954dec71c2`.

The static telemetry descriptor at `0x2a5ba00` records `game_id = eut2` and
`schema = 0x00010014` (1.20). ETS2 1.61.1.0s records the same schema value.
The install flow uses the exact build ID to select the ETS2 1.61 profile.

This build moved from the 1.60 `spawn_function_entry` model to the 1.61
`list_allocate_entry` model. The shared list allocator starts at `0x1cb3d40`.
Its patched bytes are `55 48 89 e5 41 54 48 83 ec 18 4c 8b 27`. The base
constructor starts at `0x209ed60` and installs the `particle_gen_u` vtable
address point `0x2e35488`. The RTTI typeinfo for that vtable is
`N5prism14particle_gen_uE`. The derived `particle_emitter_u` address point is
`0x2e34bb8`.

The particle paths pass `generator + 0x668` to the allocator. They request a
`0x60`-byte node and check for `nullptr`. The spawn path loads the two-float
`particle_size` pair from `+0x54` at `0x209bb8b`. The count is at `+0x630` and
the lifetime is at `+0x62c`. The classifier uses the shared 1.61 helper with
the ETS2 vtable `0x2e35488`.

### 12.5 ATS 1.61.2.0s evidence

The ATS 1.61.2.0s profile comes from the native Linux executable with SHA-256
`9e6e82a56e045736558def1cb877b184ddc666c90af40c11667766d357c7a4a3`.
Its ELF build ID is `e515a3ae857a3a84319d3579b94ba3acbe31edb5`.

The static telemetry descriptor at `0x2a69140` records `game_id = ats` and
`schema = 0x00010007` (1.07). The plugin receives `0x00010007` from
`scs_telemetry_init`. The ATS 1.61.1.4s profile records the same schema value.

The shared list allocator starts at `0x1cc1310`. Its patched bytes are
`55 48 89 e5 41 54 48 83 ec 18 4c 8b 27`. The base constructor starts at
`0x20ac330` and installs the `particle_gen_u` vtable address point `0x2e43508`.
The RTTI typeinfo for that vtable is `N5prism14particle_gen_uE`. The derived
`particle_emitter_u` address point is `0x2e42c38`.

The particle paths pass `generator + 0x668` to the allocator. They request a
`0x60`-byte node and check for `nullptr`. The spawn path loads the two-float
`particle_size` pair from `+0x54` at `0x20a915b`. The count is at `+0x630` and
the lifetime is at `+0x62c`. The classifier uses the shared 1.61 helper with
the ATS vtable `0x2e43508`.

### 12.6 ATS 1.61.3.0s evidence

The ATS 1.61.3.0s profile comes from the native Linux executable with SHA-256
`f7be85bbe9fcb17e0dcee0b2ce5873bd56a88c03c2b0c8a4915ece19f5310370`.
Its ELF build ID is `f3dcc60665b5cfcb436fe3c6811bd4edcc8f679c`. The game log
records `American Truck Simulator init ver.1.61.3.0s (rev. c00bea67595e)`.

The static telemetry descriptor at `0x2a69480` records `game_id = ats` and
`schema = 0x00010007` (1.07). The plugin receives `0x00010007` from
`scs_telemetry_init`. ATS 1.61.1.4s and ATS 1.61.2.0s record the same schema
value. The install flow uses the exact build ID to select the correct ATS
profile.

This build keeps the 1.61 `list_allocate_entry` model. The shared list
allocator starts at `0x1cc1660`. Its patched bytes are
`55 48 89 e5 41 54 48 83 ec 18 4c 8b 27`. The base constructor starts at
`0x20ac680` and installs the `particle_gen_u` vtable address point
`0x2e43868`. The RTTI typeinfo for that vtable is
`N5prism14particle_gen_uE`.

The particle paths pass `generator + 0x668` to the allocator at `0x20a9863`.
They request a `0x60`-byte node and check `test %rax,%rax` for `nullptr` at
`0x20a986f`. The spawn path loads count at `+0x630` (`0x20a932e`), the
two-float `particle_size` pair at `+0x54` (`0x20a94a0`) and `+0x58`
(`0x20a94f2`), and the lifetime at `+0x62c` (`0x20a962e`). The classifier uses
the shared 1.61 helper with the ATS vtable `0x2e43868`.

The allocator, constructor, and vtable offsets all move by `+0x350` or
`+0x360` from ATS 1.61.2.0s. The patch bytes and every object field offset are
identical. The exact build ID therefore separates the two profiles.

### 12.7 ATS 1.61.3.1s evidence

The ATS 1.61.3.1s profile comes from the native Linux executable with SHA-256
`1e4bb272d62b6242e7be1aa3edd1500d125bd87c67bcb2b5e60e6ae4c439fa12`.
Its ELF build ID is `7306db39ca0aa6bd662adbe0a0f1fd4161c420ec`. The game log
records `American Truck Simulator init ver.1.61.3.1s (rev. da49adfbbbdb)`.

The static telemetry descriptor at `0x2a69640` records `game_id = ats` and
`schema = 0x00010007` (1.07). The plugin receives `0x00010007` from
`scs_telemetry_init`. ATS 1.61.1.4s, ATS 1.61.2.0s, and ATS 1.61.3.0s record
the same schema value. The install flow uses the exact build ID to select the
correct ATS profile.

This build keeps the 1.61 `list_allocate_entry` model. The shared list
allocator starts at `0x1cc1840`. Its patched bytes are
`55 48 89 e5 41 54 48 83 ec 18 4c 8b 27`. The base constructor starts at
`0x20ac860` and installs the `particle_gen_u` vtable address point
`0x2e43a48`. The RTTI typeinfo for that vtable is
`N5prism14particle_gen_uE`.

The particle paths pass `generator + 0x668` to the allocator at `0x20a9a43`.
They request a `0x60`-byte node and check `test %rax,%rax` for `nullptr` at
`0x20a9a4f`. The spawn path loads count at `+0x630` (`0x20a950e`), the
two-float `particle_size` pair at `+0x54` (`0x20a9680`) and `+0x58`
(`0x20a96d2`), and the lifetime at `+0x62c` (`0x20a980e`). The classifier uses
the shared 1.61 helper with the ATS vtable `0x2e43a48`.

The allocator and constructor offsets move by `+0x1e0` and the vtable by
`+0x1e0` from ATS 1.61.3.0s. The spawn-path evidence addresses move by `+0x1c0`
or `+0x1e0` because this build inserts a block in the spawn function. The patch
bytes and every object field offset are identical. The exact build ID therefore
separates the profiles.

### 12.8 ATS 1.61.3.18s evidence

The ATS 1.61.3.18s profile uses the supplied Linux executable `ats.1-61-3-18s`.
Its SHA-256 is `0d82a2057e403c5b6da13764535c93a1ce7da1590b5a134d1aca3b813908f263`.
Its GNU ELF build ID is `351f6a926a8af17ad0e603adc2f3b135a661cb5b`.
The release label comes from the supplied filename. No game log was available.

The static telemetry descriptor at `0x2a6c7a0` contains schema `0x00010008`.
Its game ID pointer at `0x2a6c7a8` points to `ats` at `0x29fd6ec`.
The profile records this schema. The exact build ID selects the pinned profile after the game ID matches.

The list allocator begins at `0x1cc4970`. Its first 13 bytes are
`55 48 89 e5 41 54 48 83 ec 18 4c 8b 27`.
The base constructor begins at `0x20af990`. Its first 18 bytes are
`55 48 89 e5 41 55 41 bd 00 01 00 00 41 54 53 48 89 fb`.
At `0x20af9e6`, the constructor installs vtable address point `0x2e46dc8`.
The vtable typeinfo at `0x2e46d70` names `N5prism14particle_gen_uE`.

At `0x20acb67`, the spawn path passes `generator + 0x668` to the allocator.
It requests a `0x60`-byte node at `0x20acb6e` and checks for a null result at
`0x20acb7f`. The spawn path reads count at `+0x630` (`0x20a9b2d`),
size at `+0x54` and `+0x58` (`0x20ac7bb`, `0x20ac802`), and lifetime at
`+0x62c` (`0x20ac93e`). The synthetic tests cover this layout and the
build-specific vtable. Live game behavior remains untested.

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
