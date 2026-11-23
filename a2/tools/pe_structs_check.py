#!/usr/bin/env python3
"""Validate the PE32+ structure inventory and check it against real images.

Two kinds of checks run here:

1. Inventory checks: every structure has contiguous fields, a declared size
   that matches its field spans, and a citation.
2. Image checks: the documented offsets are used to read a real image, and
   the values must satisfy the rules the specification states (alignment,
   directory mapping, sorted sections, .pdata entry size, and so on).

A wrong offset in the inventory makes the semantic checks fail, so passing
images are evidence that the documented layout works on real input.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_inspect import PEImage, ParseError  # noqa: E402

EXPECTED_FIXED_SIZES = {
    "IMAGE_DOS_HEADER": 64,
    "IMAGE_FILE_HEADER": 20,
    "IMAGE_OPTIONAL_HEADER64": 112,
    "IMAGE_DATA_DIRECTORY": 8,
    "IMAGE_SECTION_HEADER": 40,
    "IMAGE_IMPORT_DESCRIPTOR": 20,
    "IMAGE_THUNK_DATA64": 8,
    "IMAGE_EXPORT_DIRECTORY": 40,
    "IMAGE_BASE_RELOCATION": 8,
    "IMAGE_TLS_DIRECTORY64": 40,
    "RUNTIME_FUNCTION": 12,
    "IMAGE_DELAYLOAD_DESCRIPTOR": 32,
    "IMAGE_BOUND_IMPORT_DESCRIPTOR": 8,
    "IMAGE_DEBUG_DIRECTORY": 28,
    "IMAGE_RESOURCE_DIRECTORY": 16,
    "IMAGE_RESOURCE_DIRECTORY_ENTRY": 8,
    "IMAGE_RESOURCE_DATA_ENTRY": 16,
    "IMAGE_COR20_HEADER": 72,
    "WIN_CERTIFICATE": 8,
}


class Results:
    def __init__(self) -> None:
        self.rows: list[dict] = []

    def add(self, scope: str, check: str, ok: bool, detail: str) -> None:
        self.rows.append({"scope": scope, "check": check, "ok": ok, "detail": detail})

    @property
    def failures(self) -> list[dict]:
        return [row for row in self.rows if not row["ok"]]

    def report(self) -> str:
        lines = []
        for row in self.rows:
            status = "PASS" if row["ok"] else "FAIL"
            lines.append(f"{status} {row['scope']:<28} {row['check']:<32} {row['detail']}")
        lines.append("")
        lines.append(f"{len(self.rows)} checks, {len(self.failures)} failed")
        return "\n".join(lines)


def load_inventory(path: Path) -> dict:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def check_inventory(inventory: dict, results: Results) -> dict[str, dict]:
    sources = inventory.get("sources", {})
    by_name: dict[str, dict] = {}
    for structure in inventory.get("structures", []):
        name = structure.get("name", "<unnamed>")
        by_name[name] = structure
        fields = structure.get("fields", [])
        cursor = 0
        contiguous = True
        detail = ""
        for field in sorted(fields, key=lambda item: item["offset"]):
            if field["offset"] != cursor:
                contiguous = False
                detail = f"field {field['name']} starts at {field['offset']}, expected {cursor}"
                break
            if field.get("tail"):
                break
            cursor += field["size"]
        results.add("inventory", f"{name}.fields_contiguous", contiguous, detail or f"spans 0..{cursor}")
        expected = EXPECTED_FIXED_SIZES.get(name)
        if structure.get("kind") == "fixed" and expected is not None:
            results.add(
                "inventory", f"{name}.declared_size",
                structure.get("size") == expected == cursor,
                f"declared={structure.get('size')} expected={expected} fields_end={cursor}",
            )
        source = structure.get("source")
        results.add(
            "inventory", f"{name}.citation",
            bool(source) and source in sources,
            f"source={source}",
        )
    return by_name


def probe(data: bytes, offset: int, size: int) -> int:
    return int.from_bytes(data[offset:offset + size], "little")


def check_image(path: Path, inventory: dict, by_name: dict[str, dict], results: Results) -> None:
    scope = path.name
    try:
        image = PEImage(path)
    except (ParseError, OSError) as error:
        results.add(scope, "parse", False, str(error))
        return
    data = image.reader.data
    results.add(scope, "parse", True, f"{len(data)} bytes")

    optional_fields = {field["name"]: field for field in by_name["IMAGE_OPTIONAL_HEADER64"]["fields"]}
    optional_start = image.e_lfanew + 4 + 20
    if image.pe32_plus:
        probed = {
            name: probe(data, optional_start + field["offset"], field["size"])
            for name, field in optional_fields.items()
            if name not in {"DataDirectory[0..N-1]"}
        }
        results.add(scope, "magic_is_pe32_plus", probed["Magic"] == 0x20B, f"{probed['Magic']:#x}")
        results.add(
            scope, "image_base_aligned",
            probed["ImageBase"] % 0x10000 == 0,
            f"{probed['ImageBase']:#x}",
        )
        results.add(
            scope, "section_file_alignment_relation",
            probed["SectionAlignment"] >= probed["FileAlignment"] > 0
            and (probed["FileAlignment"] & (probed["FileAlignment"] - 1)) == 0
            and 512 <= probed["FileAlignment"] <= 0x10000,
            f"section={probed['SectionAlignment']:#x} file={probed['FileAlignment']:#x}",
        )
        results.add(
            scope, "size_of_image_aligned",
            probed["SizeOfImage"] % probed["SectionAlignment"] == 0,
            f"{probed['SizeOfImage']:#x}",
        )
        results.add(
            scope, "size_of_headers_aligned",
            probed["SizeOfHeaders"] % probed["FileAlignment"] == 0,
            f"{probed['SizeOfHeaders']:#x}",
        )
        expected_optional = 112 + 8 * probed["NumberOfRvaAndSizes"]
        results.add(
            scope, "optional_header_size_matches_inventory",
            image.size_of_optional_header == expected_optional,
            f"file={image.size_of_optional_header} expected={expected_optional}",
        )
        for name in ("ImageBase", "SectionAlignment", "FileAlignment", "SizeOfImage",
                     "SizeOfHeaders", "NumberOfRvaAndSizes", "Subsystem"):
            attr = {
                "ImageBase": "image_base",
                "SectionAlignment": "section_alignment",
                "FileAlignment": "file_alignment",
                "SizeOfImage": "size_of_image",
                "SizeOfHeaders": "size_of_headers",
                "NumberOfRvaAndSizes": "number_of_rva_and_sizes",
                "Subsystem": "subsystem",
            }[name]
            actual = getattr(image, attr)
            results.add(
                scope, f"inventory_offset_{name}",
                probed[name] == actual,
                f"probed={probed[name]} parser={actual}",
            )
    else:
        results.add(scope, "pe32_image_skips_pe32_plus_offsets", True, "PE32 image")

    table_end = image.e_lfanew + 4 + 20 + image.size_of_optional_header + image.number_of_sections * 40
    results.add(scope, "section_table_inside_file", table_end <= len(data), f"end={table_end} size={len(data)}")

    ascending = all(
        image.sections[index]["virtual_address"] < image.sections[index + 1]["virtual_address"]
        for index in range(len(image.sections) - 1)
    )
    results.add(scope, "sections_ascending", ascending, f"{len(image.sections)} sections")

    adjacent = True
    detail = ""
    for index in range(len(image.sections) - 1):
        current = image.sections[index]
        following = image.sections[index + 1]
        align = image.section_alignment or 0x1000
        expected = current["virtual_address"] + ((current["virtual_size"] + align - 1) // align) * align
        if following["virtual_address"] != expected:
            adjacent = False
            detail = f"{current['name']} ends at {expected:#x}, {following['name']} at {following['virtual_address']:#x}"
            break
    results.add(scope, "sections_adjacent", adjacent, detail or "adjacent")

    if image.address_of_entry_point:
        entry_section = next(
            (section for section in image.sections
             if section["virtual_address"] <= image.address_of_entry_point
             < section["virtual_address"] + max(section["virtual_size"], section["raw_size"])),
            None,
        )
        executable = bool(entry_section and entry_section["characteristics"] & 0x20000000)
        results.add(
            scope, "entry_point_in_executable_section", executable,
            entry_section["name"] if entry_section else "unmapped",
        )

    mapping_errors = []
    for directory in image.directories:
        if not directory["virtual_address"]:
            continue
        if directory["name"] == "certificate":
            if directory["virtual_address"] + directory["size"] > len(data):
                mapping_errors.append(f"{directory['name']} outside file")
            continue
        if directory["virtual_address"] >= image.size_of_image:
            mapping_errors.append(f"{directory['name']} rva {directory['virtual_address']:#x} >= SizeOfImage")
            continue
        if image.rva_to_offset(directory["virtual_address"]) is None:
            mapping_errors.append(f"{directory['name']} rva {directory['virtual_address']:#x} unmapped")
    results.add(scope, "directories_map_into_sections", not mapping_errors, "; ".join(mapping_errors) or "ok")

    exceptions = image.exception_table()
    if exceptions:
        unwind_ok = all(key in {"0", "1", "2", "unmapped"} for key in exceptions["unwind_versions"])
        results.add(
            scope, "exception_table_entry_size",
            exceptions["remainder_bytes"] == 0 and exceptions["entries"] > 0,
            f"entries={exceptions['entries']} remainder={exceptions['remainder_bytes']}",
        )
        results.add(scope, "unwind_versions_known", unwind_ok, str(exceptions["unwind_versions"]))
        results.add(
            scope, "unwind_rvas_all_mapped",
            exceptions["unwind_versions"].get("unmapped", 0) == 0,
            str(exceptions["unwind_versions"]),
        )
        results.add(
            scope, "unwind_version_supported",
            exceptions["version_violations"] == 0,
            f"violations={exceptions['version_violations']} versions={exceptions['unwind_versions']}",
        )
        results.add(
            scope, "exception_table_sorted",
            exceptions["sorted"],
            "sorted by BeginAddress" if exceptions["sorted"] else "BeginAddress decreases",
        )
        results.add(
            scope, "chained_unwind_flags_clear",
            exceptions["chained_flag_violations"] == 0,
            f"chained={exceptions['chained_entries']} violations={exceptions['chained_flag_violations']}",
        )

    relocations = image.relocations()
    if relocations:
        results.add(
            scope, "relocation_blocks_consistent",
            not relocations["truncated"] and relocations["blocks"] > 0,
            f"blocks={relocations['blocks']} entries={relocations['entries']} types={relocations['types']}",
        )

    if image.imports():
        directory = image.directory("import")
        end_of_table = image.rva_to_offset(directory["virtual_address"]) + directory["size"]
        terminated = False
        cursor = image.rva_to_offset(directory["virtual_address"])
        while cursor + 20 <= min(end_of_table, len(data)):
            if not any(probe(data, cursor + delta, 4) for delta in (0, 4, 8, 12, 16)):
                terminated = True
                break
            cursor += 20
        results.add(scope, "import_descriptors_terminated", terminated,
                    f"{len(image.imports())} descriptors")
        descriptors = len(image.imports())
        results.add(
            scope, "import_table_within_directory",
            (descriptors + 1) * 20 <= directory["size"],
            f"descriptors={descriptors} directory={directory['size']}",
        )
        bad_names = [
            f"{entry['dll_rva']:#x}"
            for entry in image.imports()
            if entry["dll_rva"] == 0 or entry["dll_offset"] is None
        ]
        results.add(
            scope, "import_names_mapped",
            not bad_names,
            "all import Name RVAs map to file offsets"
            if not bad_names else f"unmapped Name RVAs: {', '.join(bad_names)}",
        )

    tls = image.tls()
    if tls:
        expected_size = 40 if image.pe32_plus else 24
        results.add(
            scope, "tls_directory_size",
            tls["directory_size"] >= expected_size,
            f"directory={tls['directory_size']} expected>={expected_size}",
        )
        if tls["callbacks_va"] == 0:
            results.add(scope, "tls_callbacks_terminated", True, "no callback array")
        else:
            results.add(
                scope, "tls_callbacks_terminated",
                tls["callbacks_terminated"] and len(tls["callbacks"]) < 64
                and tls["callbacks_offset"] is not None,
                f"{len(tls['callbacks'])} callbacks, terminated={tls['callbacks_terminated']}, "
                f"array_offset={tls['callbacks_offset']}",
            )

    clr = image.clr_header()
    if clr:
        results.add(scope, "clr_header_size", clr["cb"] == 72, f"cb={clr['cb']}")

    load_config = image.load_config()
    if load_config:
        sane = 4 <= load_config["declared_size"] <= 4096
        results.add(
            scope, "load_config_size_sane", sane,
            f"declared={load_config['declared_size']} directory={load_config['directory_size']}",
        )
        results.add(
            scope, "load_config_size_matches_directory",
            load_config["declared_size"] <= load_config["directory_size"]
            and load_config["directory_size"] - load_config["declared_size"] <= 16,
            f"declared={load_config['declared_size']} directory={load_config['directory_size']}",
        )
        guard_flags = load_config.get("guard_flags")
        if guard_flags is not None:
            table = load_config.get("guard_cf_function_table")
            count = load_config.get("guard_cf_function_count")
            consistent = (not table and not count) or (bool(table) and bool(count))
            results.add(
                scope, "load_config_cfg_table_consistent",
                consistent or not (guard_flags & 0x100),
                f"guard_flags={guard_flags:#x} table={table} count={count}",
            )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, default=Path("data/pe32plus-structures.json"))
    parser.add_argument("--json", action="store_true")
    parser.add_argument("images", nargs="*", type=Path)
    args = parser.parse_args(argv)

    results = Results()
    inventory = load_inventory(args.inventory)
    by_name = check_inventory(inventory, results)
    for path in args.images:
        check_image(path, inventory, by_name, results)

    if args.json:
        print(json.dumps({"rows": results.rows, "failures": len(results.failures)}, indent=2))
    else:
        print(results.report())
    return 1 if results.failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
