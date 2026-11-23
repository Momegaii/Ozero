#!/usr/bin/env python3
"""Emit the raw evidence behind the technical findings in docs/10.

Every value printed here is read from the named file, and every file is named
with its SHA-256 so the finding can be re-checked against the exact bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import apiset  # noqa: E402
from pe_inspect import PEImage, ParseError  # noqa: E402


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def hexdump(data: bytes, base: int, limit: int = 64) -> list[str]:
    lines = []
    for start in range(0, min(len(data), limit), 16):
        chunk = data[start:start + 16]
        text = "".join(chr(byte) if 32 <= byte < 127 else "." for byte in chunk)
        lines.append(f"    {base + start:#08x}  {chunk.hex(' '):<47}  {text}")
    return lines


def cstring_at(data: bytes, offset: int) -> bytes:
    end = data.find(b"\x00", offset)
    return data[offset:end if end != -1 else offset + 64]


def section(title: str) -> None:
    print()
    print(title)
    print("-" * len(title))


def describe(path: Path) -> PEImage | None:
    try:
        image = PEImage(path)
    except (ParseError, OSError) as error:
        print(f"  cannot parse {path}: {error}")
        return None
    print(f"  file    : {path}")
    print(f"  sha256  : {sha256(path)}")
    print(f"  size    : {path.stat().st_size} bytes")
    return image


def finding_api_sets(schema: apiset.Schema, fixtures: list[Path]) -> None:
    section("F1  API set contracts imported by the fixtures (PE import directory)")
    print("  reference: https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets")
    for path in fixtures:
        if not path.exists():
            continue
        image = describe(path)
        if not image:
            continue
        data = image.reader.data
        for index, entry in enumerate(image.imports()):
            name = entry["dll"]
            if not apiset.is_apiset(name):
                continue
            offset = entry.get("dll_offset")
            raw = cstring_at(data, offset) if isinstance(offset, int) else b""
            try:
                resolved = schema.resolve(name)
                target = f"{resolved['host']} via {resolved['resolved_contract']}"
                if resolved["via_alias"]:
                    target += " (alias form)"
            except apiset.ApiSetError as error:
                target = f"ERROR {type(error).__name__}: {error}"
            print(f"  [{index:>2}] {name}")
            print(f"       name_rva={entry['dll_rva']:#x} file_offset={offset:#x} "
                  f"bytes={raw.hex()} ({raw.decode('ascii', 'replace')})")
            print(f"       resolves to {target}")


def finding_tls(path: Path) -> None:
    section("F2  TLS directory and callback array (PE data directory 9)")
    print("  reference: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-tls-directory")
    image = describe(path)
    if not image:
        return
    tls = image.tls()
    if not tls:
        print("  no TLS directory")
        return
    data = image.reader.data
    print(f"  directory_rva={tls['directory_rva']:#x} file_offset={tls['directory_offset']:#x} "
          f"directory_size={tls['directory_size']}")
    print("  IMAGE_TLS_DIRECTORY64 bytes:")
    print("\n".join(hexdump(data[tls["directory_offset"]:tls["directory_offset"] + 40], tls["directory_offset"])))
    print(f"  RawDataStartVA={tls['raw_data_start_va']:#x} RawDataEndVA={tls['raw_data_end_va']:#x} "
          f"template={tls['template_size']} bytes")
    print(f"  AddressOfCallBacks={tls['callbacks_va']:#x} -> rva={tls['callbacks_rva']} "
          f"file_offset={tls['callbacks_offset']}")
    if tls["callbacks_offset"] is not None:
        count = len(tls["callbacks"]) + 1
        raw = data[tls["callbacks_offset"]:tls["callbacks_offset"] + count * 8]
        print(f"  callback array ({count} slots including the null terminator):")
        print("\n".join(hexdump(raw, tls["callbacks_offset"], limit=count * 8)))
    print(f"  callbacks={[hex(value) for value in tls['callbacks']]} "
          f"terminated={tls['callbacks_terminated']}")


def finding_relocations(path: Path) -> None:
    section("F3  Base relocation blocks (PE data directory 5)")
    print("  reference: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-relocation-section")
    image = describe(path)
    if not image:
        return
    relocations = image.relocations()
    if not relocations:
        print("  no relocations")
        return
    print(f"  blocks={relocations['blocks']} entries={relocations['entries']} "
          f"types={relocations['types']} truncated={relocations['truncated']}")
    directory = image.directory("base_relocation")
    offset = image.rva_to_offset(directory["virtual_address"])
    data = image.reader.data
    cursor = offset
    for block in range(3):
        if cursor + 8 > len(data):
            break
        page_rva, block_size = struct.unpack_from("<II", data, cursor)
        entries = (block_size - 8) // 2 if block_size >= 8 else 0
        print(f"  block[{block}] file_offset={cursor:#x} PageRVA={page_rva:#x} "
              f"BlockSize={block_size} entries={entries}")
        print("\n".join(hexdump(data[cursor:cursor + min(block_size, 24)], cursor, limit=24)))
        cursor += block_size


def finding_chained_unwind(path: Path) -> None:
    section("F4  Chained unwind info (.pdata / UNWIND_INFO)")
    print("  reference: https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64")
    image = describe(path)
    if not image:
        return
    table = image.exception_table()
    if not table:
        print("  no exception directory")
        return
    print(f"  directory_rva={table['directory_rva']:#x} file_offset={table['directory_offset']:#x} "
          f"entries={table['entries']} remainder={table['remainder_bytes']} sorted={table['sorted']}")
    print(f"  versions={table['unwind_versions']} flags={table['unwind_flags']} "
          f"chained={table['chained_entries']} flag_violations={table['chained_flag_violations']}")
    first = table.get("first_chained")
    if not first:
        print("  no chained entry")
        return
    data = image.reader.data
    print(f"  first chained .pdata entry index={first['index']} file_offset={first['entry_offset']:#x}")
    print("\n".join(hexdump(data[first["entry_offset"]:first["entry_offset"] + 12],
                            first["entry_offset"], limit=12)))
    print(f"  BeginAddress={first['begin_rva']:#x} EndAddress={first['end_rva']:#x} "
          f"UnwindInfoAddress={first['unwind_rva']:#x} -> file_offset={first['unwind_offset']:#x}")
    print("  UNWIND_INFO header bytes:")
    print("\n".join(hexdump(data[first["unwind_offset"]:first["unwind_offset"] + 8],
                            first["unwind_offset"], limit=8)))
    print(f"  first_byte={first['first_byte']:#04x} version={first['first_byte'] & 0x7} "
          f"flags={first['flags']} (CHAININFO={bool(first['flags'] & 4)})")


def finding_load_config(path: Path) -> None:
    section("F5  Load configuration (PE data directory 10)")
    print("  reference: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-load-configuration-structure")
    print("  note: the specification table differs from winnt.h; this tool follows winnt.h")
    image = describe(path)
    if not image:
        return
    config = image.load_config()
    if not config:
        print("  no load config directory")
        return
    data = image.reader.data
    offset = config["directory_offset"]
    print(f"  directory_rva={config['directory_rva']:#x} file_offset={offset:#x} "
          f"directory_size={config['directory_size']}")
    print("  header bytes (Size, TimeDateStamp, Major, Minor, GlobalFlagsClear, Set, "
          "CriticalSectionDefaultTimeout):")
    print("\n".join(hexdump(data[offset:offset + 24], offset, limit=24)))
    print(f"  declared_size={config['declared_size']} security_cookie={config['security_cookie']:#x}")
    print(f"  guard_flags={config['guard_flags']:#x} "
          f"guard_cf_function_table={config['guard_cf_function_table']} "
          f"guard_cf_function_count={config['guard_cf_function_count']}")
    guard_offset = offset + 144
    print(f"  GuardFlags at file_offset {guard_offset:#x} (offset 144 in the x64 structure):")
    print("\n".join(hexdump(data[guard_offset:guard_offset + 8], guard_offset, limit=8)))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", type=Path, default=Path("data/apiset-schema.json"))
    parser.add_argument("--console", type=Path, default=Path("build/console_hello.exe"))
    parser.add_argument("--sevenzip", type=Path, default=Path(".cache/7zr.exe"))
    parser.add_argument("--native", type=Path, default=Path(".cache/_speedups.cp311-win_amd64.pyd"))
    parser.add_argument("--api-fixture", action="append", type=Path, default=[])
    args = parser.parse_args(argv)

    try:
        schema = apiset.Schema.load(args.schema)
    except apiset.InvalidSchema as error:
        print(f"schema error: {error}", file=sys.stderr)
        return 2

    fixtures = args.api_fixture or [args.console, Path("build/cpp_throw.exe")]
    print("# Technical findings — raw evidence")
    print("# generated by tools/finding_report.py")
    print("# every offset is a file offset unless it is labelled as an RVA")
    finding_api_sets(schema, fixtures)
    finding_tls(args.console)
    finding_relocations(args.sevenzip)
    finding_chained_unwind(args.native)
    finding_load_config(args.native)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
