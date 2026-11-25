"""Check Ozero's host-side kernel build and test dependencies."""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQUIRED_FILES = (
    "Makefile",
    "kernel/src/main.c",
    "kernel/linker.lds",
    "boot/limine.conf",
    "third_party/limine/limine.h",
    "tools/qemu-smoke.sh",
    "tools/framebuffer-check.py",
    "docs/decisions/0001-boot-and-kernel-language.md",
)
REQUIRED_TOOLS = ("python3", "make", "gcc", "ld", "curl", "tar", "xorriso")
OPTIONAL_TOOLS = ("qemu-system-x86_64", "gdb")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ci", action="store_true", help="Use concise CI output.")
    args = parser.parse_args()

    missing_files = [name for name in REQUIRED_FILES if not (ROOT / name).is_file()]
    required = {name: shutil.which(name) for name in REQUIRED_TOOLS}
    optional = {name: shutil.which(name) for name in OPTIONAL_TOOLS}

    if not args.ci:
        print("Ozero x86-64 developer readiness")
        print("================================")
    for name, path in required.items():
        print(f"{'OK' if path else 'MISSING'}: {name}" + (f" ({path})" if path else ""))
    for name, path in optional.items():
        print(f"{'OK' if path else 'OPTIONAL MISSING'}: {name}" + (f" ({path})" if path else ""))
    for name in missing_files:
        print(f"MISSING: required repository file {name}")

    if missing_files or any(path is None for path in required.values()):
        print("ERROR: install missing dependencies or restore missing files.")
        return 1

    print("STATUS: host dependencies and kernel source scaffold are present.")
    print("NOTE: this check does not prove that the kernel boots on physical hardware.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
