# Research Notes — Working Notes

## Approach Used

1. **Initial triage** with `file`, `objdump`, `pefile`, `lief` to identify format, sections, imports, exports
2. **Strings extraction** with `strings` (UTF-8 and UTF-16LE) to find config keys, URLs, class names
3. **Disassembly** with `capstone` for key functions (init, shutdown, channel callbacks)
4. **RTTI walking** to find class hierarchy in stripped ELF game binaries
5. **Ghidra headless** via PyGhidra for symbol/class extraction
6. **Vtable reverse engineering** via `[offset_to_top, typeinfo_ptr, fn_ptr...]` patterns
7. **Hook analysis** via MinHook error strings and channel callback disassembly

## Tools Used

| Tool | Version | Purpose |
|------|---------|---------|
| Python | 3.14 | All scripting |
| `capstone` | 5.0.9 | Disassembly |
| `pefile` | 2024.8.26 | PE parsing |
| `lief` | 1.0.0 | PE/ELF parsing |
| `pyelftools` | 0.33 | ELF section/symbol walking |
| Ghidra | 12.1.2 DEV | RTTI/symbol analysis |
| PyGhidra | 3.1.0 | Python scripting for Ghidra |

## Key File Offsets and Addresses

### MODGuard.dll

| Address | Type | Notes |
|---------|------|-------|
| `0x18008e900` | string | `"eut2"` |
| `0x18008e908` | string | `"ats"` |
| `0x18008e910` | string | `"eurotrucks2.exe"` / `"amtrucks.exe"` |
| `0x18008f170` | string | `"; ABSOLUTE RPM CUT-OFF..."` comment |
| `0x18008f350` | string | `"FinalSmokeMultiplier="` |
| `0x18008f380` | string | `"ContinuousSmoke="` |
| `0x18008f3a8` | string | `"SmokeHook="` |
| `0x18008f3ca` | string | `"[RealisticSmoke]"` |
| `0x18008f458` | string | `"Intensity="` |
| `0x18008f470` | string | `"LoadSensitivity="` |
| `0x18008f498` | string | `"AttackSmoothness="` |
| `0x18008f4ac` | string | `"&data="` |
| `0x18008f4b8` | string | `"&steamID="` |
| `0x18008f4c8` | string | `"&clientID="` |
| `0x18008f4d8` | string | `"https://leakingsv2.modguard.com.br/key?key="` |
| `0x18008e290` | string | `"0123456789ABCDEF"` (hex format chars) |
| `0x18008d7b8` | format | `"%02x%02x%02x%02x%02x%02x"` (MAC format) |
| `0x180090ff8` | float | `100.0f` |
| `0x180091000` | float | `2500.0f` |
| `0x180091008` | float | `10000.0f` |

### RTTI Typeinfo Locations (ATS 1.55.0.0)

| File offset | VA | Mangled name |
|-------------|------|--------------|
| 0x277a960 | `0x2b7a960` | `N5prism16model_particle_uE` |
| 0x2914eaf | `0x2d14eaf` | `N5prism16list_iterators_tINS_14particle_gen_u10particle_tEEE` |
| 0x2914f90 | `0x2d14f90` | `N5prism14particle_gen_uE` |
| 0x29146a0 | `0x2d146a0` | `N5prism18particle_emitter_uE` |

### Vtable Locations (ATS 1.55.0.0)

| Class | Vtable VA | Vtable pointer |
|-------|-----------|---------------|
| `prism::particle_gen_u` | `0x2d15030` | `0x2d15040` |
| `prism::particle_emitter_u` | `0x2d14708` | `0x2d14718` |

## Function Reference (Complete List)

### MODGuard Functions

| RVA | Purpose |
|-----|---------|
| `0x002d00` | Deferred init: log status + spawn worker |
| `0x002d9c` | Log: "License key validated" |
| `0x002ddc` | Log: "Smoke mod found" |
| `0x002e46` | Log: "Developer credits" |
| `0x003dc0` | Game ID from PE substring scan (UTF-16LE) |
| `0x003ff0` | Game detection entry: returns 1=ETS2, 2=ATS |
| `0x013c40` | MAC address formatter (`%02x%02x%02x%02x%02x%02x`) |
| `0x016250` | Config regex setup |
| `0x017150` | **Big config loader** (1318 lines of disasm) |
| `0x01c650` | std::regex iterator |
| `0x01d8a0` | Typeinfo destructor helper |
| `0x01de90` | regex iterator::increment |
| `0x01fa10` | regex iterator (alternate) |
| `0x02dde0` | Hash table (FNV-1a): used for MAC lookup |
| `0x02e0a0` | Worker thread result drain |
| `0x031460` | **MinHook installer** (essential_hook + particle_alloc_hook) |
| `0x031f80` | Worker thread body (single-pass, no loop) |
| `0x032060` | Worker thread start |
| `0x032190` | Shutdown helper |
| `0x031b80` | State save |
| `0x033640` | **Channel 2 callback (FRAME_END)** — main update loop |
| `0x033e90` | **Channel 5 callback (CONFIGURATION)** — particle classifier |
| `0x0342c0` | Settings validator |
| `0x034550` | Channel 3 (PAUSED) callback — sets paused flag |
| `0x0345f0` | Channel 4 (STARTED) callback — clears started flag |
| `0x034690` | Channel 1 (FRAME_START) callback |
| `0x034c40` | Channel 2 entry: `jmp 0x180033640` |
| `0x036c50` | Worker queue management |
| `0x037210` | Queue destructor helper |
| `0x0470b0` | URL builder (assembles HTTP request) |
| `0x047225` | `WinHttpCrackUrl` |
| `0x04724a` | `WinHttpOpen` |
| `0x047278` | `WinHttpSetOption` |
| `0x04728f` | `WinHttpConnect` |
| `0x0472d3` | `WinHttpOpenRequest` |
| `0x0472fb` | `WinHttpSendRequest` |
| `0x04730a` | `WinHttpReceiveResponse` |
| `0x04733c` | `WinHttpQueryHeaders` |
| `0x047358` | `WinHttpCloseHandle` (1st of 3) |
| `0x047361` | `WinHttpCloseHandle` (2nd) |
| `0x04736a` | `WinHttpCloseHandle` (3rd) |
| `0x050790` | TLS callback 1 |
| `0x0507f8` | TLS callback 2 |
| `0x04d1ac` | `SRWLock::acquire` |
| `0x04d1b4` | `SRWLock::release` |
| `0x0503e4` | `operator new(size_t)` |
| `0x0503b0` | `operator delete(void*)` |
| `0x056adc` | Key validator (HWID hash) |
| `0x359c0` | **`scs_telemetry_init` (export)** |
| `0x362d0` | **`scs_telemetry_shutdown` (export)** |

## Key RTTI Symbols Count

- **MODGuard.dll**: 12261 unique `prism::` mangled type names (because MODGuard links against SCS SDK headers)
- **amtrucks (ATS 1.55)**: 12265 unique `prism::` type names
- **eurotrucks2 (ETS2 1.55)**: similar count

## Significant Compile-Time Observations

1. **MODGuard's BSS data is huge** (~970 KB of uninitialized data). This means it allocates
   large working buffers at runtime.
2. **MODGuard embeds MSVC regex** (`std::regex` is statically linked). This adds significant
   code size for the config parser.
3. **MODGuard uses lots of standard C++ library types**: `std::string`, `std::vector`, `std::map`,
   `std::regex`, etc.
4. **MODGuard has internal installer state** at fixed addresses in BSS — this means each
   game process state is initialized to known patterns at startup.

## Cross-Validation Method

For each major finding, we cross-checked:
- The RTTI string in `.rodata` matches the demangled name
- The vtable pointer in the constructor matches the address of the vtable
- The function entry point in the vtable matches the disassembled entry
- The class size constant in the deleting destructor matches the inferred offset range

## Specific Patterns That Helped

1. **`call rel32; mov esi, <size>; jmp operator_delete`** — class size + deleting dtor signature
2. **`[RDI + 0x40]`, `[RDI + 0x10]`, `[RDI + 0x18]`** — particle_gen field offsets
3. **`mov [rdi], <vtable_ptr>` in constructor body** — identifies the class
4. **Mangled name strings starting with `N5prism`** — namespace + class name encoded
5. **`48 89 5c 24 XX` (mov [rsp+XX], rbx) followed by `48 83 ec YY`** — function prologue with
   specific stack frame size
