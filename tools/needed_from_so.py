#!/usr/bin/env python3
"""needed_from_so.py -- write tools/imports_needed.txt from the game's .so
without pyelftools (the same result as runtime/tools/gen_imports.py --libs).

Reads the ELF32 .dynsym of each module named in tools/imports.cfg (MODULES)
and lists every undefined symbol that no module exports, with its weak flag.
Symbol NAMES only: no game code is copied.

  python3 tools/needed_from_so.py <folder with libModernCombat4.so>
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def modules_from_cfg(path):
    mods = []
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].split()
            if line and line[0] == "MODULES":
                mods += line[1:]
    return mods


def dynsym(path):
    """Yield (name, shndx, bind) for every .dynsym entry of an ELF32 LE file."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
        sys.exit(f"{path}: not a 32-bit little-endian ELF")
    e_shoff, = struct.unpack_from("<I", d, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    secs = [struct.unpack_from("<IIIIIIIIII", d, e_shoff + i * e_shentsize) for i in range(e_shnum)]
    for s in secs:
        if s[1] == 11:  # SHT_DYNSYM
            strtab = secs[s[6]]
            str_off = strtab[4]
            for i in range(s[5] // 16):
                name_off, _val, _size, info, _other, shndx = struct.unpack_from("<IIIBBH", d, s[4] + i * 16)
                end = d.index(b"\0", str_off + name_off)
                yield d[str_off + name_off:end].decode(), shndx, info >> 4


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    libdir = sys.argv[1]
    mods = modules_from_cfg(os.path.join(HERE, "imports.cfg"))
    und, exp = {}, set()
    for m in mods:
        for name, shndx, bind in dynsym(os.path.join(libdir, m)):
            if not name:
                continue
            if shndx == 0:  # SHN_UNDEF
                weak = bind == 2  # STB_WEAK
                und[name] = und.get(name, True) and weak
            elif bind != 0:  # not STB_LOCAL
                exp.add(name)
    need = {n: w for n, w in und.items() if n not in exp}
    out = os.path.join(HERE, "imports_needed.txt")
    with open(out, "w") as f:
        f.write("# symbol weak(0/1) -- regenerate with gen_imports.py --libs\n")
        for n in sorted(need):
            f.write(f"{n} {1 if need[n] else 0}\n")
    print(f"{out}: {len(need)} imports from {', '.join(mods)}")


if __name__ == "__main__":
    main()
