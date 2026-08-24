# Raw Research Artefacts

This directory contains the raw output from the reverse engineering process.

## Files

| File | Description |
|------|-------------|
| `cfg_loader.txt` | Full disassembly of the MODGuard configuration loader function (1318 lines, ~45 KB). It captures the regex-based key=value parser that reads the `[RealisticSmoke]` configuration. |
| `function_list.txt` | Auto-extracted list of all function entry points in MODGuard.dll (4240 functions). This list is useful for cross-referencing when you build a call graph. |
| `ghidra_analysis.py` | Python script that uses PyGhidra 3.1.0 to load the ATS 1.55.0.0 binary into Ghidra and extract particle-related classes. See the header for usage. |
| `research_notes.md` | Working notes that document the tools, key addresses, function reference table, and analytical approach. |

## Usage Notes

### `cfg_loader.txt`
```
# Each line: <address>: <mnemonic> <operands>
# e.g. 0x180017bb6: cmp r14d, 0xfffff
# 
# Use grep to find references to specific strings or addresses:
grep "0x18008e900" cfg_loader.txt     # find "eut2" string references
grep "call 0x18001c650" cfg_loader.txt # find regex iterator calls
```

### `function_list.txt`
```
# One function per line:
# 0x1800359c0  (RVA 0x359c0)
# 
# Filter by address range:
awk '$1 >= "0x180035000" && $1 <= "0x180036000"' function_list.txt
```

### `ghidra_analysis.py`
```bash
export GHIDRA_INSTALL_DIR=/opt/ghidra
pip install pyghidra  # if not already
python3 ghidra_analysis.py
```

## Reproducibility

You can regenerate these artefacts when you run the analysis commands that produced them. See `../02-particle-classes.md` for the ATS binary analysis commands. See `../01-reverse-engineering.md` for the MODGuard function reference.

## Tools Required

- `capstone` Python package: `pip install capstone`
- `pefile` Python package: `pip install pefile`
- `lief` Python package: `pip install lief`
- Ghidra + PyGhidra (optional, for symbol-level analysis)
- Python 3.10+