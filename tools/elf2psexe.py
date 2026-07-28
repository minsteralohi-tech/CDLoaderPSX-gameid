#!/usr/bin/env python3
"""Convert a statically-linked 32-bit little-endian MIPS ELF into a PS1 PS-EXE.

Usage: elf2psexe.py input.elf output.exe [--sp 0x801FFF00] [--region "..."]

Only the pieces a PS1 loader needs are implemented: gather every allocated
PROGBITS section, lay them out contiguously starting at the lowest allocated
address, and emit the 0x800-byte PS-EXE header followed by the payload padded to
a multiple of 2048 bytes. .bss (NOBITS) is left to the program's own startup
code to clear.
"""

import struct
import sys

SHF_ALLOC = 0x2
SHT_PROGBITS = 1
SHT_NOBITS = 8


def read_u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def read_u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def parse_elf(data):
    if data[:4] != b"\x7fELF":
        raise SystemExit("error: not an ELF file")
    if data[4] != 1:
        raise SystemExit("error: not a 32-bit ELF")
    if data[5] != 1:
        raise SystemExit("error: not little-endian")
    machine = read_u16(data, 18)
    if machine != 8:  # EM_MIPS
        raise SystemExit("error: not a MIPS ELF (e_machine=%d)" % machine)

    entry = read_u32(data, 24)
    shoff = read_u32(data, 32)
    shentsize = read_u16(data, 46)
    shnum = read_u16(data, 48)

    sections = []
    for i in range(shnum):
        base = shoff + i * shentsize
        sh_type = read_u32(data, base + 4)
        sh_flags = read_u32(data, base + 8)
        sh_addr = read_u32(data, base + 12)
        sh_offset = read_u32(data, base + 16)
        sh_size = read_u32(data, base + 20)
        sections.append((sh_type, sh_flags, sh_addr, sh_offset, sh_size))
    return entry, sections


def build_image(sections, data):
    alloc = [s for s in sections if (s[1] & SHF_ALLOC) and s[4] > 0]
    if not alloc:
        raise SystemExit("error: no allocated sections found")

    progbits = [s for s in alloc if s[0] == SHT_PROGBITS]
    if not progbits:
        raise SystemExit("error: no loadable (PROGBITS) sections found")

    load_addr = min(s[2] for s in progbits)
    end_addr = max(s[2] + s[4] for s in progbits)
    image = bytearray(end_addr - load_addr)

    for sh_type, _flags, sh_addr, sh_offset, sh_size in progbits:
        off = sh_addr - load_addr
        image[off:off + sh_size] = data[sh_offset:sh_offset + sh_size]

    return load_addr, image


def main():
    args = sys.argv[1:]
    sp = 0x801FFF00
    region = "Sony Computer Entertainment Inc. for North America area"
    positional = []
    i = 0
    while i < len(args):
        if args[i] == "--sp":
            sp = int(args[i + 1], 0)
            i += 2
        elif args[i] == "--region":
            region = args[i + 1]
            i += 2
        else:
            positional.append(args[i])
            i += 1

    if len(positional) != 2:
        raise SystemExit(__doc__)

    in_path, out_path = positional
    with open(in_path, "rb") as f:
        data = f.read()

    entry, sections = parse_elf(data)
    load_addr, image = build_image(sections, data)

    # Pad payload to a multiple of 2048 bytes (required by the PS-EXE format).
    if len(image) % 2048 != 0:
        image += bytes(2048 - (len(image) % 2048))
    file_size = len(image)

    header = bytearray(0x800)
    header[0:8] = b"PS-X EXE"
    struct.pack_into("<I", header, 0x10, entry)       # initial_pc
    struct.pack_into("<I", header, 0x14, 0)           # initial_gp
    struct.pack_into("<I", header, 0x18, load_addr)   # load address
    struct.pack_into("<I", header, 0x1C, file_size)   # payload size
    struct.pack_into("<I", header, 0x28, 0)           # memfill start
    struct.pack_into("<I", header, 0x2C, 0)           # memfill size
    struct.pack_into("<I", header, 0x30, sp)          # initial sp base
    struct.pack_into("<I", header, 0x34, 0)           # initial sp offset
    marker = region.encode("ascii", "ignore")[:0x800 - 0x4C]
    header[0x4C:0x4C + len(marker)] = marker

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(image)

    print("PS-EXE written: %s" % out_path)
    print("  entry  = 0x%08X" % entry)
    print("  load   = 0x%08X" % load_addr)
    print("  size   = %d bytes (0x%X)" % (file_size, file_size))


if __name__ == "__main__":
    main()
