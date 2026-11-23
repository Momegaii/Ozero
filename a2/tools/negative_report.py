#!/usr/bin/env python3
"""Run the malformed-image suite and print the failure each mutation must cause.

This is the raw evidence behind the negative tests: for every mutation it
writes a patched copy, runs the structure checker on it, and requires the
expected check to fail. The unmutated image must pass.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pe_mutate  # noqa: E402
import pe_structs_check  # noqa: E402
from pe_inspect import PEImage  # noqa: E402

# mutation -> source, check that must fail, and optional detail substring
CASES = [
    {"mutation": "sections_swap", "source": "build/console_hello.exe",
     "expected": "sections_ascending"},
    {"mutation": "reloc_block_size", "source": "build/console_hello.exe",
     "expected": "relocation_blocks_consistent"},
    {"mutation": "import_terminator", "source": "build/console_hello.exe",
     "expected": "import_descriptors_terminated"},
    {"mutation": "pdata_remainder", "source": "build/console_hello.exe",
     "expected": "exception_table_entry_size"},
    {"mutation": "pdata_swap", "source": "build/console_hello.exe",
     "expected": "exception_table_sorted"},
    {"mutation": "tls_callbacks_unmapped", "source": "build/console_hello.exe",
     "expected": "tls_callbacks_terminated"},
    {"mutation": "chain_flags", "source": ".cache/_speedups.cp311-win_amd64.pyd",
     "expected": "chained_unwind_flags_clear"},
    {"mutation": "loadconfig_size", "source": ".cache/_speedups.cp311-win_amd64.pyd",
     "expected": "load_config_size_sane"},
    {"mutation": "truncate_dos_header", "source": "build/console_hello.exe",
     "expected": "parse", "detail": "truncated DOS header"},
    {"mutation": "truncate_nt_headers", "source": "build/console_hello.exe",
     "expected": "parse", "detail": "truncated NT headers"},
    {"mutation": "truncate_optional_header", "source": "build/console_hello.exe",
     "expected": "parse", "detail": "truncated optional header"},
    {"mutation": "truncate_section_table", "source": "build/console_hello.exe",
     "expected": "parse", "detail": "truncated section table"},
    {"mutation": "import_name_rva_zero", "source": "build/console_hello.exe",
     "expected": "import_names_mapped"},
    {"mutation": "import_name_rva_oob", "source": "build/console_hello.exe",
     "expected": "import_names_mapped"},
]


def check(path: Path, inventory: dict, by_name: dict) -> pe_structs_check.Results:
    results = pe_structs_check.Results()
    pe_structs_check.check_image(path, inventory, by_name, results)
    return results


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, default=Path("data/pe32plus-structures.json"))
    parser.add_argument("--out-dir", type=Path, default=Path("build/mutations"))
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    inventory = pe_structs_check.load_inventory(args.inventory)
    baseline_results = pe_structs_check.Results()
    by_name = pe_structs_check.check_inventory(inventory, baseline_results)
    if baseline_results.failures:
        print("inventory itself fails", file=sys.stderr)
        return 2

    args.out_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    failures = 0
    for case in CASES:
        mutation = case["mutation"]
        source = Path(case["source"])
        expected = case["expected"]
        expected_detail = case.get("detail", "")
        if not source.exists():
            rows.append({"mutation": mutation, "status": "skipped", "detail": f"{source} missing",
                         "expected_check": expected, "failures": []})
            continue
        baseline = check(source, inventory, by_name)
        if baseline.failures:
            rows.append({"mutation": mutation, "status": "baseline-failed",
                         "detail": f"{len(baseline.failures)} failures"})
            failures += 1
            continue
        image = PEImage(source)
        data = bytearray(source.read_bytes())
        try:
            patch = pe_mutate.apply(image, data, mutation)
        except pe_mutate.NotApplicable as error:
            rows.append({"mutation": mutation, "status": "not-applicable", "detail": str(error)})
            continue
        output = args.out_dir / f"{source.stem}.{mutation}{source.suffix}"
        payload = bytes(data[:patch["truncate_to"]]) if "truncate_to" in patch else bytes(data)
        output.write_bytes(payload)
        mutated = check(output, inventory, by_name)
        failed_checks = {row["check"] for row in mutated.failures}
        detail = next((row["detail"] for row in mutated.failures if row["check"] == expected), "")
        hit = expected in failed_checks and (not expected_detail or expected_detail in detail)
        if not hit:
            failures += 1
        rows.append({
            "mutation": mutation,
            "status": "pass" if hit else "fail",
            "source": str(source),
            "output": str(output),
            "expected_check": expected,
            "expected_detail": expected_detail,
            "failures": sorted(failed_checks),
            "detail": detail,
            "patch_offset": patch["offset"],
            "original": patch["original"],
            "patched": patch["patched"],
            "truncate_to": patch.get("truncate_to"),
        })

    if args.json:
        print(json.dumps(rows, indent=2))
    else:
        for row in rows:
            if row["status"] == "pass":
                if row.get("truncate_to") is not None:
                    print(f"PASS {row['mutation']:<24} -> {row['expected_check']:<24} "
                          f"truncated at {row['truncate_to']:#x}")
                else:
                    print(f"PASS {row['mutation']:<24} -> {row['expected_check']:<24} "
                          f"@ {row['patch_offset']:#x} {row['original']} -> {row['patched']}")
                print(f"     {row['detail']}")
            elif row["status"] in {"not-applicable", "skipped"}:
                print(f"SKIP {row['mutation']:<24} {row['detail']}")
            else:
                print(f"FAIL {row['mutation']:<24} expected {row.get('expected_check')} "
                      f"got {row.get('failures')}")
        applied = sum(1 for row in rows if row["status"] == "pass")
        skipped = sum(1 for row in rows if row["status"] in {"not-applicable", "skipped"})
        print(f"\n{applied} negative cases produced the expected failure, "
              f"{skipped} skipped, {failures} problems")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
