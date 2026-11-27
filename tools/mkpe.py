#!/usr/bin/env python3
"""Builds an Ozero Stage 7 PE32+ (x86-64) image from a linked ELF64 object.

Usage: python3 tools/mkpe.py <input.elf> <output.pe> [image-base]

The input ELF must be linked at a fixed address inside the Ozero user image
window (see include/ozero/uapi.h), e.g. 0x40000000, with page-aligned
.text/.rdata/.data sections. The output is a PE32+ image the Ozero PE loader
can map directly. No imports and no relocations: ntdll/kernel32 are linked
statically into the image (Stage 7 limitation, see ADR 0021).

This is a host build tool (Python), not part of the kernel or runtime.
"""

import os
import struct
import sys

ELFCLASS64 = 2
EM_X86_64 = 62

SECTION_ALIGN = 0x1000
FILE_ALIGN = 0x200
OPT_HDR_SIZE = 236  # PE32+ optional header with 16 data directories

SECTION_ORDER = (".text", ".rdata", ".data")

SECTION_CHARACTERISTICS = {
    ".text": 0x60000020,   # CNT_CODE | MEM_EXECUTE | MEM_READ
    ".rdata": 0x40000040,  # CNT_INITIALIZED_DATA | MEM_READ
    ".data": 0xC0000040,   # CNT_INITIALIZED_DATA | MEM_READ | MEM_WRITE
}


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def align_up(v, a):
    return (v + a - 1) & ~(a - 1)


def fail(msg):
    sys.stderr.write("mkpe: error: %s\n" % msg)
    sys.exit(1)


def main(argv):
    if len(argv) < 3:
        fail("usage: mkpe.py <input.elf> <output.pe> [image-base]")
    inp, outp = argv[1], argv[2]
    image_base = int(argv[3], 0) if len(argv) > 3 else 0x40000000

    with open(inp, "rb") as f:
        elf = f.read()

    if elf[:4] != b"\x7fELF":
        fail("not an ELF file")
    if elf[4] != ELFCLASS64:
        fail("not ELF64")
    if u16(elf, 0x12) != EM_X86_64:
        fail("not x86-64")

    e_shoff = u64(elf, 0x28)
    e_shentsize = u16(elf, 0x3A)
    e_shnum = u16(elf, 0x3C)
    e_shstrndx = u16(elf, 0x3E)
    e_entry = u64(elf, 0x18)

    shdrs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        shdrs.append({
            "name": u32(elf, off),
            "type": u32(elf, off + 4),
            "flags": u64(elf, off + 8),
            "addr": u64(elf, off + 16),
            "offset": u64(elf, off + 24),
            "size": u64(elf, off + 32),
        })

    strtab_off = u64(elf, e_shoff + e_shstrndx * e_shentsize + 24)

    def name(i):
        end = elf.index(b"\0", strtab_off + shdrs[i]["name"])
        return elf[strtab_off + shdrs[i]["name"]:end].decode("ascii", "replace")

    by_name = {}
    for i, s in enumerate(shdrs):
        if s["type"] == 0x8:  # SHT_NOBITS sections have no file contents
            continue
        by_name[name(i)] = s

    # Ensure all expected sections exist (empty if not present in the ELF).
    for sname in SECTION_ORDER:
        if sname not in by_name:
            by_name[sname] = {
                "name": sname,
                "type": 1,  # SHT_PROGBITS
                "flags": 0,
                "addr": 0,
                "offset": 0,
                "size": 0,
            }

    pe_sections = []
    next_vaddr = image_base
    for sname in SECTION_ORDER:
        s = by_name[sname]
        content = elf[s["offset"]:s["offset"] + s["size"]]
        # Page-align the section start within the image window.
        vaddr = align_up(next_vaddr, SECTION_ALIGN)
        pe_sections.append({
            "name": sname,
            "addr": vaddr,
            "size": s["size"],
            "content": content,
            "characteristics": SECTION_CHARACTERISTICS[sname],
        })
        next_vaddr = vaddr + s["size"]

    entry_rva = e_entry - image_base
    if entry_rva < 0:
        fail("entry point 0x%x below image base 0x%x" % (e_entry, image_base))

    # Virtual sizes / RVAs (RVA = vaddr - image_base).
    max_end_rva = 0
    for s in pe_sections:
        s["rva"] = s["addr"] - image_base
        s["end_rva"] = s["rva"] + s["size"]
        max_end_rva = max(max_end_rva, s["end_rva"])
    size_of_image = align_up(max_end_rva, SECTION_ALIGN)

    # File layout.
    nt_off = 0x80
    coff_off = nt_off + 4
    opt_off = coff_off + 20
    sect_off = opt_off + OPT_HDR_SIZE
    headers_end = sect_off + len(pe_sections) * 40
    size_of_headers = align_up(headers_end, FILE_ALIGN)

    file_off = size_of_headers
    for s in pe_sections:
        s["raw_off"] = file_off
        s["raw_size"] = align_up(s["size"], FILE_ALIGN) if s["size"] else 0
        file_off += s["raw_size"]

    # Assemble.
    out = bytearray(file_off)

    # DOS header.
    out[0:2] = b"MZ"
    struct.pack_into("<I", out, 0x3C, nt_off)

    # NT signature.
    out[nt_off:nt_off + 4] = b"PE\x00\x00"

    # COFF file header.
    struct.pack_into("<H", out, coff_off + 0, EM_X86_64)       # Machine
    struct.pack_into("<H", out, coff_off + 2, len(pe_sections))  # NumberOfSections
    struct.pack_into("<I", out, coff_off + 8, 0)               # TimeDateStamp
    struct.pack_into("<I", out, coff_off + 12, 0)              # PointerToSymbolTable
    struct.pack_into("<I", out, coff_off + 16, 0)              # NumberOfSymbols
    struct.pack_into("<H", out, coff_off + 18, OPT_HDR_SIZE)   # SizeOfOptionalHeader
    struct.pack_into("<H", out, coff_off + 20, 0)              # Characteristics

    # Optional header PE32+.
    def w16(off, v):
        struct.pack_into("<H", out, opt_off + off, v)

    def w32(off, v):
        struct.pack_into("<I", out, opt_off + off, v)

    def w64(off, v):
        struct.pack_into("<Q", out, opt_off + off, v)

    w16(0, 0x20B)      # Magic = PE32+
    w32(16, entry_rva) # AddressOfEntryPoint
    w64(20, image_base)  # ImageBase
    w32(28, SECTION_ALIGN)  # SectionAlignment
    w32(32, FILE_ALIGN)     # FileAlignment
    w32(52, size_of_image)  # SizeOfImage
    w32(56, size_of_headers)  # SizeOfHeaders
    w16(64, 3)              # Subsystem = console
    w32(104, 16)            # NumberOfRvaAndSizes
    # data directories (offset 112) all zero

    # Section table.
    cursor = sect_off
    for s in pe_sections:
        name_bytes = s["name"].encode("ascii")
        out[cursor:cursor + len(name_bytes)] = name_bytes
        struct.pack_into("<I", out, cursor + 8, s["size"])            # VirtualSize
        struct.pack_into("<I", out, cursor + 12, s["rva"])            # VirtualAddress
        struct.pack_into("<I", out, cursor + 16, s["raw_size"])       # SizeOfRawData
        struct.pack_into("<I", out, cursor + 20, s["raw_off"])        # PointerToRawData
        struct.pack_into("<I", out, cursor + 36, s["characteristics"]) # Characteristics
        cursor += 40

    # Section raw data.
    for s in pe_sections:
        out[s["raw_off"]:s["raw_off"] + s["size"]] = s["content"]

    with open(outp, "wb") as f:
        f.write(out)

    print("mkpe: %s -> %s (image base 0x%x, entry rva 0x%x, size 0x%x)"
          % (inp, outp, image_base, entry_rva, size_of_image))


if __name__ == "__main__":
    main(sys.argv)
