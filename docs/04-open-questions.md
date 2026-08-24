# Open Questions — Resolution Status

This document tracks the 6 open questions from the first MODGuard reverse engineering pass. It gives the resolution status after deeper analysis.

---

## Summary Table

| # | Question | Status | Confidence |
|---|----------|--------|------------|
| Q1 | model_particle / particle_gen class layout | ✅ **Resolved** | High (via ATS/ETS2 binary analysis) |
| Q2 | ThrottleCurve shape (linear vs power) | ⚠️ **Best guess** | Medium (linear multiplier most likely) |
| Q3 | Steam-ID acquisition path | ⚠️ **Partial** | Low (no Steam API imports; source unknown) |
| Q4 | License refresh interval | ✅ **Resolved** | High (300-frame poll counter found) |
| Q5 | `next_float_buffer` callback signature | ✅ **Resolved** | High (SDK struct layout checked) |
| Q6 | Register-call signature | ✅ **Resolved** | High (register_for_event, not channel) |

---

## Q1 — Particle class layout

### Question
What is the exact in-memory structure of `model_particle`, `particle_gen`, and the live
particle instances? What offsets do class_id and size live at?

### Resolution
**Resolved** by static analysis of the game binaries:

- **ATS 1.55.0.0** (`/mnt/ssd1/SteamLibrary/steamapps/common/American Truck Simulator/bin/linux_x64/amtrucks`)
- **ETS2 1.55.0.0** (`/mnt/ssd1/SteamLibrary/steamapps/common/Euro Truck Simulator 2/bin/linux_x64/eurotrucks2`)

### Confirmed class hierarchy

**ATS** (uses `particle_emitter_u` wrapper):
```
model_particle_u → particle_emitter_u (0x1b40 bytes) → particle_gen_u (0x1a68 bytes) → list_t<particle_t>
```

**ETS2** (no emitter wrapper):
```
model_particle_u → particle_gen_u → list_t<particle_t>
```

### Confirmed class sizes (ATS 1.55.0.0)

| Class | Size | Vtable Address |
|-------|------|----------------|
| `prism::particle_gen_u` | **0x1a68** | VA `0x2d15030` |
| `prism::particle_emitter_u` | **0x1b40** | VA `0x2d14708` |
| `prism::particle_t` (list item) | **0x40** | n/a |

### Live particle (`particle_t`) layout

```c
struct particle_live_t {              // size 0x40 (64 bytes)
    void*    fwd;                     // +0x00
    void*    prev;                    // +0x08
    uint32_t class_id;                // +0x10  (== 5 for smoke_new)
    uint32_t _pad14;
    float    size;                    // +0x18  (bucket selection)
    float    spawn_acc;               // +0x1C
    /* 32 bytes of additional state */
    void*    next;                    // +0x40  (next particle in list)
};
```

### Verification
- RTTI strings: `N5prism14particle_gen_uE`, `N5prism16model_particle_uE`, `N5prism18particle_emitter_uE`
- Class sizes from `mov esi, <imm>` in deleting destructors (for example, `0x1a68`, `0x1b40`)
- Inheritance from vtable[2] thunks
- MODGuard disassembly confirms `class_id@+0x10`, `size@+0x18`, and stride `0x40`

### Detail
See `02-particle-classes.md` for the full details.

---

## Q2 — `ThrottleCurve` shape

### Question
Is `ThrottleCurve` used as a linear multiplier, an exponent (power), or something else?

### Resolution
**Best guess: linear multiplier.**

### Evidence

1. **No `powf`/`logf`/`expf` calls**:
   - `expf` and `powf` strings exist in `.rdata` at file offsets 0x902a0/0x902a8, but there are zero xrefs to them
   - These are leftover CRT function names. MODGuard does not call them.
   - MODGuard does not import `powf`, `expf`, or `logf` from any DLL
   - There are no `_log_`, `_exp_`, or `__powf2` symbols either

2. **Channel 2 callback uses only multiplication**:
   - The per-frame loop at `0x180033640` computes `mulss`, `addss`, `divss` only
   - There are no `pow`, `log`, or `exp` instructions

3. **Default value is 1.0** in the `.bak` files. This makes the linear case an identity. It is consistent with a multiplicative interpretation.

4. **SCS SDK comment style**: the SiiNunit configuration files have a comment `; THROTTLE PROFILE`.
   This suggests that `ThrottleCurve` is a profile shape parameter, not a power exponent.

### Possible interpretations (in order of likelihood)

1. **Linear multiplier**: `contribution = throttle_smoothed * ThrottleCurve` (most likely)
2. **Linear divisor**: `contribution = throttle_smoothed / ThrottleCurve` (possible)
3. **Power**: `contribution = pow(throttle_smoothed, ThrottleCurve)` (needs a custom implementation)
4. **Unused**: parsed but ignored in the formula (least likely)

### Cannot fully confirm without
You cannot fully confirm this without dynamic instrumentation of MODGuard with the mod active in the game.

---

## Q3 — Steam-ID acquisition

### Question
How does MODGuard get the user Steam ID to send to the license server?

### Resolution
**Partial** — source unknown.

### Findings

- **URL template contains `&steamID=`** (file offset 0x8f4b8, VA 0x180090eb8)
- **MODGuard does NOT import any Steam API** (no `steam_api*.dll`, no `ISteam*` functions)
- **`steamworks` SDK symbols are not in the binary**
- **Possible sources** (in order of likelihood):
  1. **Shared memory** with another mod or the game launcher
  2. **Windows registry**: `HKCU\Software\Valve\Steam\ActiveProcess` (Steam standard key)
  3. **The `.ini` configuration file** (supplied by the user via a `[MODFiles]` or similar section)
  4. **The `IPHLPAPI::GetAdaptersInfo` output**. The MAC is already in use. The SteamID can be read from a Windows key.

### Note
The Linux Steam runtime (`~/.steam/...`) provides `steam_api.so` or `libsteam_api.so`.
A Linux-native MODGuard replacement can use `ISteamUser::GetSteamID()` directly.

---

## Q4 — License refresh interval

### Question
How often does MODGuard check the license again?

### Resolution
**Resolved**: every ~5-10 seconds (300 frames), via main-thread polling.

### Findings

The worker thread body at `0x180031f80` does **NOT** loop. It runs once and returns:

```asm
0x180031f80: sub rsp, 0x28
... (3 mutex-locked calls + queue cleanup)
0x18003200e: add rsp, 0x28
0x180032012: ret                ; <-- thread exits
```

The main thread (channel 2 callback) **polls the worker result queue every 300 frames**:

```asm
; Channel 2 callback at 0x180034947:
0x180034947: movzx eax, word ptr [rip + 0x757ea]   ; load 16-bit counter
0x18003494e: inc ax                                   ; counter++
0x180034951: mov word ptr [rip + 0x757e0], ax       ; store (counter at 0x1800aa1d8)
0x180034958: mov ecx, 0x12c                          ; ecx = 300
0x18003495d: cmp ax, cx                             ; if (counter != 300) skip
0x180034960: jne 0x1800349b8
0x180034962: mov word ptr [rip + 0x757cf], bp       ; counter = 0
...                                                       (drain worker queue)
```

### Refresh rate

| Game framerate | Refresh interval |
|---------------:|-----------------:|
| 60 FPS | 5 seconds |
| 30 FPS | 10 seconds |
| 120 FPS | 2.5 seconds |

The counter (`uint16` at `[rip + 0x757e0]` = `0x1800aa1d8`) is **per-channel-2-invocation**, not per-second. The actual interval depends on the game frame rate.

### Worker thread (HTTP request)

Each license check spawns a fresh worker thread via `CreateThread` at `0x180032060`. The worker:
1. Acquires mutex, calls 3 functions (`0x180036c50`, `0x180037210`, `0x18002e4a0`)
2. Releases mutex, does cleanup
3. Returns

The HTTP request happens inside `0x18002e4a0`. This function calls `0x1800470b0`, the URL builder. The URL builder uses WinHttpCrackUrl → WinHttpOpen → WinHttpConnect → WinHttpOpenRequest → ... .

### The first guess was wrong
I first thought the worker thread ran periodically with `Sleep(300s)`. This is **incorrect**. The main thread spawns the worker fresh each time it wants to check again, at ~5-10s intervals.

---

## Q5 — `next_float_buffer` callback signature

### Question
What is the exact signature of the SCS SDK `next_float_buffer` callback used by MODGuard?

### Resolution
**Resolved**: the SDK struct layout is checked against public reverse-engineering projects.

### Findings

The SDK struct layout (checked against `drysius/scs-telemetry-rs` and `RenCloud/scs-sdk-plugin`):

```c
struct scs_telemetry_init_params_v100 {
    scs_init_params common;            // +0x00 (32 bytes)
    scs_register_for_event_fn     register_for_event;       // +0x20
    scs_unregister_from_event_fn  unregister_from_event;    // +0x28
    scs_register_for_channel_fn   register_for_channel;     // +0x30
    scs_unregister_from_channel_fn unregister_from_channel;  // +0x38
};

struct scs_telemetry_init_params_v101 {
    scs_telemetry_init_params_v100 common;
    scs_next_float_value_fn      next_float_value;          // +0x40
    scs_next_string_value_fn     next_string_value;         // +0x48
    scs_next_vector_value_fn     next_vector_value;         // +0x50
};
```

### MODGuard usage

MODGuard accesses `params[0x48]` (9 references) and `params[0x50]` (2 references). These are the `next_string_value` and `next_vector_value` callbacks of the v1.01 extension.

These callbacks return pointers to internal SDK buffers. The mod can read these buffers but cannot modify them.
MODGuard calls them at frame boundaries to refresh its cached telemetry values.

### Note
The `next_float_value` callback at `params[0x40]` is not directly referenced, but it must exist (the SDK provides it). MODGuard can use it indirectly via the broader refresh logic.

---

## Q6 — Register-call signature

### Question
What does the SDK call site at `0x180035c60` do?

```asm
0x180035c53: xor r8d, r8d                  ; r8 = 0
0x180035c56: lea rdx, [rip - 0x15cd]       ; rdx = function pointer at 0x180034690
0x180035c5d: mov ecx, r15d                ; ecx = 1, 2, 5, 3, or 4
0x180035c60: call rax                     ; rax = register_fn
```

### Resolution
**Resolved**: it is a **register_for_event** call, not register_for_channel.

### Initial confusion
I first thought MODGuard called `register_for_channel(name, index, type, flags, callback, context)`.
The rdx argument pointed to real machine code, not an ASCII string. This is invalid for the standard SDK signature `(name, index, type, flags, callback, context)`.

### Resolution via SDK offset disambiguation

I checked the SDK function pointers at the correct offsets. The result is:

```asm
; Init: 0x1800359fe: mov [rip + 0x747fb], rax  <- params[0x20] stored at [0x1800aa200]
; Init: 0x180035a14: mov [rip + 0x74875], rax  <- params[0x30] stored at [0x1800aa290]
```

| Offset | Field | Stored at |
|--------|-------|-----------|
| `+0x20` | `register_for_event` | `[0x1800aa200]` |
| `+0x28` | `unregister_from_event` | `[0x1800aa1a8]` |
| `+0x30` | `register_for_channel` | `[0x1800aa290]` |
| `+0x38` | `unregister_from_channel` | `[0x1800aa2f8]` |

Then at `0x180035c3d`: `rax = [rip + 0x745bc]` loads from `[0x1800aa200]` = **`register_for_event`**.

### Confirmed signature

```c
typedef void (SCSAPI_CONV *scs_register_for_event_v100_t)(
    scs_u32_t event_id,
    scs_event_callback_t callback,
    scs_context_t context
);
```

### Call site interpretation

```c
for (int i = 0; i < 5; i++) {
    int event_id = ???;    // 1, 2, 5, 3, 4 (out of order)
    void* callback = ???;  // points to MODGuard's event handler function
    void* context = NULL;
    register_for_event(event_id, callback, context);
}
```

### Event ID → callback mapping

| Event ID | Event name (SCS SDK) | Callback function | Purpose |
|----------:|---------------------|-------------------|---------|
| **1** | `SCS_TELEMETRY_EVENT_FRAME_START` | `0x180034690` | Pre-frame telemetry refresh |
| **2** | `SCS_TELEMETRY_EVENT_FRAME_END` | `0x180034c40` (→ `0x180033640`) | Main update loop + 300-frame poll |
| **3** | `SCS_TELEMETRY_EVENT_PAUSED` | `0x180034550` | Set paused flag |
| **4** | `SCS_TELEMETRY_EVENT_STARTED` | `0x1800345f0` | Clear started flag |
| **5** | `SCS_TELEMETRY_EVENT_CONFIGURATION` | `0x180033e90` | Configuration event handler (particle classifier) |

### Note
This is a critical insight: MODGuard uses **events** (lifecycle hooks) rather than channels
(callbacks on telemetry value changes). It reads telemetry values **passively** via `next_*_value`
callbacks rather than via channel-update callbacks.

---

## Confidence Levels

| Question | Confidence | Reasoning |
|----------|------------|-----------|
| Q1 | High | Checked by 3 independent methods (RTTI, vtable layout, MODGuard disasm cross-check) |
| Q2 | Medium | Strong indirect evidence (no powf, no `pow` instructions), but not confirmed without dynamic testing |
| Q3 | Low | No Steam API imports. There are multiple plausible sources. It needs dynamic testing. |
| Q4 | High | Exact 300-frame counter found in disassembly. The worker thread is confirmed single-shot. |
| Q5 | High | Checked against public open-source SDK reverse-engineering projects |
| Q6 | High | Resolved by tracing exact offsets of SDK struct fields stored in BSS |