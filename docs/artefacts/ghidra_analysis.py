#!/usr/bin/env python3
import pyghidra
pyghidra.start()

def main():
    binary_path = "/tmp/ats_155_binary"
    
    with pyghidra.open_program(binary_path, analyze=False) as flat_api:
        program = flat_api.getCurrentProgram()
        st = program.getSymbolTable()
        
        # Try iterating by name pattern
        print("=" * 80)
        print("RTTI symbols (ZTS/ZTI/ZTV) for particle-related classes")
        print("=" * 80)
        count = 0
        for name_pattern in ["particle", "Particle", "model_particle"]:
            try:
                sym_iter = st.getSymbols(name_pattern)
                for sym in sym_iter:
                    name = sym.getName()
                    if (name.startswith("_ZTS") or name.startswith("_ZTI") or name.startswith("_ZTV")):
                        addr = sym.getAddress().getOffset()
                        print("  0x%x  %s" % (addr, name))
                        count += 1
                        if count > 100:
                            break
            except Exception as e:
                print(f"  Error with pattern '{name_pattern}': {e}")
        print("Total: %d" % count)

if __name__ == "__main__":
    main()
