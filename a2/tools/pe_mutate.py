#!/usr/bin/env python3
"""Create deliberately malformed copies of a PE image for negative tests.

Each mutation targets one documented invariant, so a checker failure can be
attributed to a single defect:

  sections_swap            section VirtualAddress order breaks
  reloc_block_size         a base-relocation block claims an impossible size
  import_terminator        the all-zero import descriptor is overwritten
  pdata_remainder          the exception directory size is not a multiple of 12
  pdata_swap               .pdata entries are no longer sorted by BeginAddress
  chain_flags              a CHAININFO entry also claims EHANDLER
  loadconfig_size          the load-config Size field is absurd
  tls_callbacks_unmapped   AddressOfCallBacks points outside the image
  truncate_dos_header      the file ends inside IMAGE_DOS_HEADER
  truncate_nt_headers      the file ends inside the PE signature/COFF header
  truncate_optional_header the file ends inside IMAGE_OPTIONAL_HEADER64
  truncate_section_table   the file ends inside the section table
  import_name_rva_zero     the first import descriptor has Name RVA 0
  import_name_rva_oob      the first import descriptor Name RVA is outside the image

The output keeps the original file name plus a suffix, e.g.
``console_hello.sections_swap.exe``.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_inspect import PEImage, ParseError  # noqa: E402

MUTATIONS = [
    "sections_swap",
    "reloc_block_size",
    "import_terminator",
    "pdata_remainder",
    "pdata_swap",
    "chain_flags",
    "loadconfig_size",
    "tls_callbacks_unmapped",
    "truncate_dos_header",
    "truncate_nt_headers",
    "truncate_optional_header",
    "truncate_section_table",
    "import_name_rva_zero",
    "import_name_rva_oob",
]


class NotApplicable(Exception):
    pass


def put(data: bytearray, offset: int, payload: bytes, label: str) -> dict:
    if offset < 0 or offset + len(payload) > len(data):
        raise NotApplicable(f"{label}: offset {offset:#x} outside the file")
    original = bytes(data[offset:offset + len(payload)])
    data[offset:offset + len(payload)] = payload
    return {"offset": offset, "original": original.hex(), "patched": payload.hex(), "label": label}


def truncate(data: bytearray, size: int, label: str) -> dict:
    if size <= 0 or size >= len(data):
        raise NotApplicable(f"{label}: truncation size {size} is not inside the file")
    return {"offset": size, "original": "", "patched": "", "label": label, "truncate_to": size}


def section_table_offset(image: PEImage) -> int:
    return image.e_lfanew + 4 + 20 + image.size_of_optional_header


def data_directory_offset(image: PEImage, index: int) -> int:
    fixed_part = 112 if image.pe32_plus else 96
    return image.e_lfanew + 4 + 20 + fixed_part + index * 8


def apply(image: PEImage, data: bytearray, mutation: str) -> dict:
    if mutation == "sections_swap":
        if len(image.sections) < 2:
            raise NotApplicable("needs at least two sections")
        base = section_table_offset(image)
        first = base + 12
        second = base + 40 + 12
        a = struct.unpack_from("<I", data, first)[0]
        b = struct.unpack_from("<I", data, second)[0]
        patch = put(data, first, struct.pack("<I", b), "section[0].VirtualAddress")
        put(data, second, struct.pack("<I", a), "section[1].VirtualAddress")
        return patch

    if mutation == "reloc_block_size":
        directory = image.directory("base_relocation")
        if not directory:
            raise NotApplicable("no base relocation directory")
        offset = image.rva_to_offset(directory["virtual_address"])
        if offset is None:
            raise NotApplicable("relocation directory is unmapped")
        return put(data, offset + 4, struct.pack("<I", 0x00FFFFFF), "relocation[0].BlockSize")

    if mutation == "import_terminator":
        directory = image.directory("import")
        if not directory:
            raise NotApplicable("no import directory")
        offset = image.rva_to_offset(directory["virtual_address"])
        if offset is None:
            raise NotApplicable("import directory is unmapped")
        for index in range(4096):
            entry = offset + index * 20
            if entry + 20 > len(data):
                raise NotApplicable("no terminator inside the file")
            if not any(struct.unpack_from("<IIIII", data, entry)):
                return put(data, entry + 16, struct.pack("<I", 1), "import_terminator.FirstThunk")
        raise NotApplicable("no all-zero import descriptor found")

    if mutation == "pdata_remainder":
        directory = image.directory("exception")
        if not directory:
            raise NotApplicable("no exception directory")
        entry = data_directory_offset(image, 3)
        size = struct.unpack_from("<I", data, entry + 4)[0]
        return put(data, entry + 4, struct.pack("<I", size + 1), "data_directory[exception].Size")

    if mutation == "pdata_swap":
        table = image.exception_table()
        if not table or table["entries"] < 2:
            raise NotApplicable("needs at least two RUNTIME_FUNCTION entries")
        offset = table["directory_offset"]
        first = bytes(data[offset:offset + 12])
        second = bytes(data[offset + 12:offset + 24])
        patch = put(data, offset, second, "pdata[0]")
        put(data, offset + 12, first, "pdata[1]")
        return patch

    if mutation == "chain_flags":
        table = image.exception_table()
        if not table or not table.get("first_chained"):
            raise NotApplicable("no chained unwind entry")
        offset = table["first_chained"]["unwind_offset"]
        first = data[offset]
        return put(data, offset, bytes([first | 0x08]), "UNWIND_INFO.flags(EHANDLER)")

    if mutation == "loadconfig_size":
        config = image.load_config()
        if not config:
            raise NotApplicable("no load config directory")
        return put(data, config["directory_offset"], struct.pack("<I", 0xFFFF), "load_config.Size")

    if mutation == "tls_callbacks_unmapped":
        tls = image.tls()
        if not tls:
            raise NotApplicable("no TLS directory")
        pointer_size = 8 if image.pe32_plus else 4
        field = tls["directory_offset"] + 3 * pointer_size
        bogus = image.image_base + 0x0F000000
        payload = struct.pack("<Q", bogus) if image.pe32_plus else struct.pack("<I", bogus & 0xFFFFFFFF)
        return put(data, field, payload, "TLS.AddressOfCallBacks")

    if mutation == "truncate_dos_header":
        return truncate(data, 0x20, "IMAGE_DOS_HEADER")

    if mutation == "truncate_nt_headers":
        return truncate(data, image.e_lfanew + 10, "PE signature + COFF header")

    if mutation == "truncate_optional_header":
        optional = image.e_lfanew + 4 + 20
        return truncate(data, optional + 40, "IMAGE_OPTIONAL_HEADER64")

    if mutation == "truncate_section_table":
        return truncate(data, section_table_offset(image) + 20, "section table")

    if mutation in {"import_name_rva_zero", "import_name_rva_oob"}:
        directory = image.directory("import")
        if not directory:
            raise NotApplicable("no import directory")
        offset = image.rva_to_offset(directory["virtual_address"])
        if offset is None:
            raise NotApplicable("import directory is unmapped")
        if offset + 20 > len(data):
            raise NotApplicable("first import descriptor is outside the file")
        name_rva = 0 if mutation == "import_name_rva_zero" else image.size_of_image + 0x1000
        return put(data, offset + 12, struct.pack("<I", name_rva), "import[0].Name")

    raise NotApplicable(f"unknown mutation {mutation}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--mutation", action="append", choices=MUTATIONS)
    parser.add_argument("--out-dir", type=Path, default=Path("build/mutations"))
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    try:
        image = PEImage(args.source)
    except (ParseError, OSError) as error:
        print(f"cannot read {args.source}: {error}", file=sys.stderr)
        return 2

    args.out_dir.mkdir(parents=True, exist_ok=True)
    selected = args.mutation or MUTATIONS
    results = []
    failures = 0
    for mutation in selected:
        data = bytearray(args.source.read_bytes())
        try:
            patch = apply(image, data, mutation)
        except NotApplicable as error:
            results.append({"mutation": mutation, "status": "not-applicable", "detail": str(error)})
            continue
        output = args.out_dir / f"{args.source.stem}.{mutation}{args.source.suffix}"
        payload = bytes(data[:patch["truncate_to"]]) if "truncate_to" in patch else bytes(data)
        output.write_bytes(payload)
        results.append({
            "mutation": mutation,
            "status": "written",
            "output": str(output),
            "offset": patch["offset"],
            "original": patch["original"],
            "patched": patch["patched"],
            "label": patch["label"],
            "truncate_to": patch.get("truncate_to"),
        })
        failures += 0

    if args.json:
        print(json.dumps(results, indent=2))
    else:
        for item in results:
            if item["status"] == "written":
                if item.get("truncate_to") is not None:
                    print(f"WROTE {item['output']}  {item['label']} truncated at "
                          f"{item['truncate_to']:#x}")
                else:
                    print(f"WROTE {item['output']}  {item['label']} @ {item['offset']:#x} "
                          f"{item['original']} -> {item['patched']}")
            else:
                print(f"SKIP  {item['mutation']}: {item['detail']}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
