# Game-Side Particle Class Hierarchy
## Reverse Engineering of ATS 1.55.0.0 and ETS2 1.55.0.0 Binaries

This document describes the game-side C++ classes that MODGuard hooks into. The analysis used static binary inspection of the Linux ELF binaries of *American Truck Simulator* and *Euro Truck Simulator 2*, version 1.55.0.0.

---

## 1. Class Hierarchy Overview

### 1.1 ATS 1.55.0.0 — Full hierarchy

```
prism::list_t<prism::particle_gen_u, prism::particle_t>
        ↑
prism::particle_gen_u                          (base, size 0x1a68)
        ↑ single inheritance
prism::particle_emitter_u                      (derived, size 0x1b40)
        ↑ single inheritance
prism::model_particle_u                         (the hookup class)
        ↑
[ SiiNunit binding: model_particle : model.particle.smoke_new ]
```

### 1.2 ETS2 1.55.0.0 — Simpler hierarchy (no emitter wrapper)

```
prism::list_t<prism::particle_gen_u, prism::particle_t>
        ↑
prism::particle_gen_u                          (base, size unknown for ETS2)
        ↑ single inheritance
prism::model_particle_u                         (the hookup class)
        ↑
[ SiiNunit binding: model_particle : model.particle.smoke_new ]
```

The key difference is that ATS has a `particle_emitter_u` wrapper. This wrapper contains 3 `particle_gen_u` children (one per "layer" of the smoke effect). ETS2 has only the direct `model_particle_u → particle_gen_u → particle_t` chain.

MODGuard must handle both layouts. The hook targets the `model_particle` SiiNunit loader. This loader works for both.

---

## 2. Confirmed Class Sizes and Vtable Addresses

### 2.1 From `amtrucks` 1.55.0.0 (`/mnt/ssd1/SteamLibrary/steamapps/common/American Truck Simulator/bin/linux_x64/amtrucks`)

| Class | Size | Vtable Address | Vtable Pointer | Notes |
|-------|------|----------------|----------------|-------|
| `prism::particle_gen_u` | **0x1a68** | VA `0x2d15030` | VA `0x2d15040` | size from `mov esi, 0x1a68` in deleting dtor |
| `prism::particle_emitter_u` | **0x1b40** | VA `0x2d14708` | VA `0x2d14718` | size from `mov esi, 0x1b40` in deleting dtor |
| `prism::model_particle_u` | not directly extracted | — | — | very large container class |
| `prism::particle_t` (list item) | **0x40** | — | — | stride checked via MODGuard disasm |

### 2.2 From `eurotrucks2` 1.55.0.0 (`/mnt/ssd1/SteamLibrary/steamapps/common/Euro Truck Simulator 2/bin/linux_x64/eurotrucks2`)

| Class | Size | Notes |
|-------|------|-------|
| `prism::particle_gen_u` | not extracted | layout can differ from ATS |
| `prism::particle_emitter_u` | **NOT PRESENT** | ETS2 has no `prism::particle_emitter_u` RTTI symbol |
| `prism::particle_t` | **0x40** | assumed same |

The absence of `particle_emitter_u` in ETS2 means the ETS2 hookup chain is shorter:

```
ETS2: model_particle_u -> particle_gen_u -> particle_t
ATS:  model_particle_u -> particle_emitter_u (3 sub-particle_gen_u at +0x1a70/+0x1a88/+0x1aa0)
                           -> particle_gen_u -> particle_t
```

---

## 3. How the Classes Were Identified

### 3.1 RTTI extraction

Both binaries are stripped. They still contain C++ RTTI strings in `.rodata`. Each class has:

- A **`__class_type_info`** struct with: `[vtable_ptr (8B)] [name_ptr (8B)]`
- A mangled name string like `N5prism14particle_gen_uE`

The `name_ptr` field points to the mangled name. The mangled name contains the namespace and class name.

For ATS 1.55.0.0, the RTTI strings are at:

| File offset | VA | Mangled name |
|-------------|------|--------------|
| 0x277a960 | `0x2b7a960` | `N5prism16model_particle_uE` |
| 0x2914eaf | `0x2d14eaf` | `N5prism16list_iterators_tINS_14particle_gen_u10particle_tEEE` |
| 0x2914f90 | `0x2d14f90` | `N5prism14particle_gen_uE` |
| 0x29146a0 | `0x2d146a0` | `N5prism18particle_emitter_uE` |
| 0x25523ed | `0x29523ed` | `N5prism14particle_gen_u` (without `E` — field name) |

The search pattern `N5prism\d+([a-z_]+)E?` finds 2632 unique `prism::` type names. 91 of these names relate to particles and models.

### 3.2 Vtable location

Vtables in ELF C++ binaries follow this layout:

```
vtable[-2]: int64 offset_to_top  (often 0)
vtable[-1]: typeinfo pointer     (points to typeinfo struct)
vtable[ 0]: first virtual function pointer
vtable[ 1]: second virtual function pointer
...
```

To find a vtable, search for the pattern:

```
[ int64 offset_to_top ]  [ typeinfo_ptr (8B) ]  [ fn_ptr_0 ] [ fn_ptr_1 ] ...
```

For ATS 1.55.0.0:

| Class | Vtable VA | Vtable pointer (= vtable + 0x10) | Confirmed via |
|-------|-----------|-------------------------------|---------------|
| `particle_gen_u` | `0x2d15030` | `0x2d15040` | typeinfo at `0x2d14fb0` |
| `particle_emitter_u` | `0x2d14708` | `0x2d14718` | typeinfo at `0x2d146a0` |

### 3.3 Class size extraction

Each class has two destructors:

- **Complete object destructor** (at `vtable[0]`): walks sub-objects and base class
- **Deleting destructor** (at `vtable[1]`): calls complete dtor + `operator delete(this, size)`

The deleting destructor contains:

```asm
call <complete_dtor>            ; run dtor
mov  rdi, this_ptr              ; restore this
mov  esi, <class_size>          ; size for operator delete
jmp  <operator_delete>          ; tail-call
```

The `class_size` is the immediate operand of `mov esi, ...` in the deleting dtor.

### 3.4 Inheritance verification

`particle_emitter_u::vtable[2]` (at VA `0x1fad020`) is a thunk:

```asm
call 0x1c811f0             ; typeinfo lookup
mov  edi, 0x2d14c80         ; shared type_info string
jmp  0x1fb2bd0             ; → particle_gen_u::vtable[2]
```

The target of this thunk is `0x1fb2bd0`. This address is exactly `particle_gen_u::vtable[2]`. This checks single inheritance.

---

## 4. Live Particle (`particle_t`) Memory Layout

**Stride**: 0x40 (64) bytes. The stride was checked by `add rbx, 0x40` in the MODGuard channel-3 callback.

```c
struct particle_live_t {              // size 0x40 (64 bytes)
    void*    fwd;                     // +0x00  (forward pointer / vtable)
    void*    prev;                    // +0x08  (back pointer / etc.)
    uint32_t class_id;                // +0x10  (== 5 for smoke_new)
    uint32_t _pad14;
    float    size;                    // +0x18  (bucket selection)
    float    spawn_acc;               // +0x1C
    /* 32 bytes of additional state from +0x20 to +0x3F */
    void*    next;                    // +0x40  (next particle in linked list)
};
```

### 4.1 MODGuard disassembly that shows the layout

Channel 3 callback (`0x180033e90`) raw bytes from `0x180033f43`:

```
83 7b 10 05              ; cmp dword ptr [rbx+0x10], 5         ; class_id check
75 53                    ; jne skip
f3 0f 10 73 18           ; movss xmm6, dword ptr [rbx+0x18]   ; load size
...
48 8b 43 40              ; mov rax, qword ptr [rbx+0x40]      ; next pointer
48 83 c3 40              ; add rbx, 0x40                       ; advance
48 85 c0                 ; test rax, rax
75 87                    ; jne loop
```

### 4.2 Iteration pattern (reconstructed)

```c
for (rbx = list_head; rbx != NULL; rbx += 0x40) {
    // [rbx + 0x40] is read BEFORE advancing; it's the next iterator's first field
    if (*(uint32_t*)(rbx + 0x10) == 5 /* smoke_new */) {
        float size = *(float*)(rbx + 0x18);
        // ... bucket selection ...
        *(float*)(rbx + 0x18) = new_size;
    }
}
```

The "next pointer" check is `*(uint64_t*)(old_rbx + 0x40) == 0`. The loop body uses the new rbx. This matches the vtable link pattern.

---

## 5. `particle_gen_u` Full Class Layout

### 5.1 Sub-object fields (extracted from destructor)

The complete object destructor (`0x1fae370`) accesses:

```c
destructor(this):
    // Free shared_ptr list at +0x1a50
    if (this->list_at_0x1a50) {
        atomic_dec(this->list_at_0x1a50.refcount);
    }
    // Free config-style fields at +0x1a48 (u16) and +0x1a4c (u32)
    if (this->u16_at_0x1a48 != 0xffff) free(this->u16_at_0x1a48);
    if (this->u32_at_0x1a4c != 0xffffffff) free(this->u32_at_0x1a4c);
    if (this->ptr_at_0x1a40) { decref(this->ptr_at_0x1a40->refcount); }
    
    // Linked list of particles at +0x650 (list_t<particle_t>)
    // Traversal:
    for (n = this->list_at_0x650; n != &this->list_at_0x660; n = n->next) {
        delete_particle(n->data);
    }
    
    // Sub-objects (likely list_t or similar containers)
    // at offsets +0x80, +0x638, +0x640, +0x650, +0x660
}
```

### 5.2 Inferred field layout

```c
struct particle_gen_u {                  // size 0x1a68 (6,760 bytes)
    void*    vtable;                      // +0x00 (8 bytes)
    /* fields 0x08..0x1a47 = many sub-systems */
    uint16_t  config_field_0;              // +0x1a48 (e.g. particles_max_count)
    uint32_t  config_field_1;              // +0x1a4c (e.g. time_to_live)
    particle_gen_u_particle_list_t list1;  // +0x1a50 (16 bytes: ptr + refcount)
    /* more fields 0x1a60..0x637 */
    void*    list_root;                   // +0x638 (particle list iterator root?)
    void*    sub_list_vtable;             // +0x640 (set to 0x2d15010 in dtor)
    void*    list_head;                   // +0x648 (cleared to 0 in dtor)
    void*    list_at_0x650;               // +0x650 (start sentinel of list_t<particle_t>)
    /* more fields up to 0x668 */
    void*    list_end;                     // +0x660 (end sentinel of list_t<particle_t>)
    /* lots more state 0x668..0x1a67 */
};
```

The size 0x1a68 means this class is large. It embeds many sub-systems for particle lifecycle management.

---

## 6. `particle_emitter_u` Full Class Layout

### 6.1 Inherited from `particle_gen_u`

`particle_emitter_u` inherits all fields of `particle_gen_u` (starting at offset +0). It adds 3 sub-objects at offsets `+0x1a70`, `+0x1a88`, and `+0x1aa0`. These offsets are evenly spaced by 0x18 = 24 bytes.

These 3 sub-objects are likely the **3 child `particle_gen_u` instances**. These instances do the particle spawning (one per "layer" of the smoke effect as defined in `smoke_new.sii`).

### 6.2 Constructor pattern (checked via destructor logic)

```c
class particle_emitter_u : public particle_gen_u {
    /* inherited: 0..0x1a67 (particle_gen_u state) */
    particle_gen_u*   children[3];   // at +0x1a70, +0x1a88, +0x1aa0 (each 0x18 bytes)
};
```

The `0x18 = 24 bytes` per child slot is large for a pointer (8 bytes). It can be an `owner_ptr_t<particle_gen_u>` (24 bytes) or a `unique_ptr_with_deleter` (24 bytes).

### 6.3 Deleting destructor at `0x1fa6e00`

```asm
0x1fa6e00: push rbp; mov rbp, rsp; push rbx; mov rbx, rdi
0x1fa6e08: lea rdi, [rdi + 0x1aa0]       ; child 2 (last)
0x1fa6e0f: sub rsp, 8
0x1fa6e13: mov [rdi - 0x1aa0], 0x2d14718  ; set vtable to particle_emitter_u's
0x1fa6e1e: call 0x1c17280                ; ~list_t destructor for child 2
0x1fa6e23: lea rdi, [rbx + 0x1a88]       ; child 1
0x1fa6e2a: call 0x1c17280                ; ~list_t destructor for child 1
0x1fa6e2f: lea rdi, [rbx + 0x1a70]       ; child 0 (first)
0x1fa6e36: call 0x1c17280                ; ~list_t destructor for child 0
0x1fa6e3b: mov rdi, rbx                 ; restore 'this'
0x1fa6e3e: call 0x1fae370                ; call base class dtor (particle_gen_u)
0x1fa6e43: mov rdi, rbx
0x1fa6e46: mov rbx, [rbp - 8]
0x1fa6e4a: mov esi, 0x1b40              ; <-- SIZE of particle_emitter_u
0x1fa6e4f: leave
0x1fa6e50: jmp 0x289195b                 ; tail-call operator delete(this, 0x1b40)
```

---

## 7. Virtual Function Tables (reconstructed)

### 7.1 `particle_emitter_u` vtable (`VA 0x2d14708`)

| Index | Function | Purpose |
|-------|----------|---------|
| `[0]` | `0x1fa6ba0` | Complete object destructor |
| `[1]` | `0x1fa6e00` | Deleting destructor (calls dtor + free(0x1b40)) |
| `[2]` | `0x1fad020` | Thunk to `particle_gen_u::vtable[2]` |
| `[3]` | `0x1fad050` | Initializer (calls 0x1cab910, 0x1c6fd40) |
| `[4]` | `0x1c6fb50` | `ret` (no-op virtual) |
| `[5]` | `0x1c6fb60` | `ret` (no-op virtual) |
| `[6]` | `0x1fa6b90` | Getter returning string ptr `0x2d14c80` |
| `[7]` | `0x1fad360` | Jump to `0x1fb2ce0` |
| `[8]` | `0x1fad370` | Jump to `0x1fb2cf0` |
| `[9]` | `0x1fad380` | Jump to `0x1fb2d00` |

### 7.2 `particle_gen_u` vtable (`VA 0x2d15030`)

| Index | Function | Purpose |
|-------|----------|---------|
| `[0]` | `0x1fae370` | Complete object destructor |
| `[1]` | `0x1fae500` | Deleting destructor (calls dtor + free(0x1a68)) |
| `[2]` | `0x1fb2bd0` | Add particle to active list |
| `[3]` | `0x1fb2c00` | Initialization with type validation |
| `[4]` | `0x1c6fb50` | `ret` (no-op virtual) |
| `[5]` | `0x1c6fb60` | `ret` (no-op virtual) |
| `[6]` | `0x1fada80` | Getter returning string ptr `0x2d15520` |
| `[7]` | `0x1fb2ce0` | Jump to `0x1c6fb80` (event handler thunk) |
| `[8..]` | `0x1fb2cf0`, `0x1fb2d00`, `0x1fb2d10`, `0x1fb2d20` | Field-clear functions |

---

## 8. `.pmg` Hookup String Layout

Checked by inspection of the `.pmg` files in `PM_389_Smoke.zip`:

```
... <ptrA: uint32 LE>  <0xFFFFFFFF × 2>  <ptrB: uint32 LE>  'model.particle.smoke_new'  <00 00 00 00>
    <float[6]  24 B>   <0xFF000000>      <0x00000000 × 2>   ← 36-byte entry body
    <float[6]  24 B>   <0xFF000000>      <0x00000000 × 2>   ← next body (one per LOD)
    ...
```

The hookup name `"model.particle.smoke_new"` (24 ASCII chars) is preceded by:
- `ptrA`: file-internal offset back into the geometry block
- 2 × `0xFFFFFFFF` sentinels
- `ptrB`: offset to LOD/skin index table

Each LOD and instance has a **36-byte entry body** (`float[6] + 0xFF000000 + 0x0 × 2`).

The string is followed by exactly **4 NUL bytes** for 4-byte alignment. Then the body follows.

---

## 9. ATS vs ETS2 Comparison Table

| Aspect | ATS 1.55 | ETS2 1.55 |
|--------|-----------|-----------|
| `prism::particle_gen_u` | ✓ | ✓ |
| `prism::particle_emitter_u` | ✓ (size 0x1b40) | **✗ — absent** |
| `prism::model_particle_u` | ✓ | ✓ |
| `prism::particle_t` | ✓ | ✓ (assumed) |
| SiiNunit binding | `model_particle : model.particle.smoke_new` | same |
| Active emitter count | 3 (gen_array[0..2]) | 1 (gen_array[0] — original) |
| Modern smoke stack | 3 layered emitters | 1 emitter |
| Hookup string in `.pmg` | `model.particle.smoke_new` | `model.particle.smoke_new` |

---

## 10. Implications for MODGuard

The MODGuard hook targets the **runtime `model_particle_u` instance**. The SiiNunit parser loads this instance when the game starts a new truck. The hook works in both ATS and ETS2 because:

1. Both games call the SiiNunit loader to resolve `model.particle : model.particle.smoke_new`.
2. The loader allocates a `model_particle_u`. It binds 3 (ATS) or 1 (ETS2) child particle emitters to this instance.
3. The hook intercepts the particle allocator. This allows MODGuard to:
   - In ATS, walk into `particle_emitter_u → 3 × particle_gen_u → list_t<particle_t>`
   - In ETS2, walk into `particle_gen_u → list_t<particle_t>` directly
4. The particle bucket assignment (100/2500/10000) matches the MODGuard per-frame iteration in both.

### 10.1 Linux reimplementation strategy

On Linux and Proton, MODGuard cannot use MinHook (x86-64 inline hooking is not supported). Instead:
1. The Linux SCS SDK plugin (loaded via `bin/linux_x64/plugins/`) can read telemetry directly.
2. To modify particle behaviour, the Linux plugin can:
   - Modify the .sii and .sii.bak configuration files to enable or disable the smoke mod (passive)
   - Write to a shared memory region that the game reads (active; this needs the cooperation of the game)
   - Use the `LD_PRELOAD` function of Proton to hook libc functions that the game uses (fragile)
3. The cleanest Linux path is to provide an alternative SDK plugin that:
   - Implements the same RealisticSmoke formula
   - Writes particle scaling factors to a known memory location that the game polls
   - Or directly manipulates the .sii.bak to restore 3-layered emitters at game start