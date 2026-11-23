#!/usr/bin/env python3
"""Check that the Stage 0 deliverables exist and contain their required sections."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

REQUIRED_FILES = [
    "README.md",
    "EVIDENCE.md",
    "Makefile",
    "docs/01-hardware-support-matrix.md",
    "docs/02-storage-architecture.md",
    "docs/03-haswell-gen75-gpu.md",
    "docs/04-driver-interface-draft.md",
    "docs/05-hardware-test-plan.md",
    "docs/06-stage0-review-response.md",
    "matrix/hardware-support-matrix.csv",
    "include/o0/platform.h",
    "include/o0/block.h",
    "tests/header_smoke.c",
    "tests/test_matrix_semantics.py",
    "tools/validate_matrix.py",
    "tools/collect_inventory.sh",
]

REQUIRED_SECTIONS = {
    "docs/01-hardware-support-matrix.md": [
        "Storage support matrix",
        "Why each row is separate",
        "Representative device IDs",
        "Acceptance criteria",
        "References",
    ],
    "docs/02-storage-architecture.md": [
        "Device-independent block API",
        "AHCI / SATA",
        "NVMe",
        "USB mass storage",
        "Cross-protocol feature mapping",
        "References",
    ],
    "docs/03-haswell-gen75-gpu.md": [
        "Why Haswell is separate",
        "Global GTT",
        "Per-process GTT",
        "Residency, pinning, and eviction",
        "Relocation",
        "Command submission",
        "Stage 5 plan and gates",
        "References",
    ],
    "docs/04-driver-interface-draft.md": [
        "Discovery and resources",
        "MMIO",
        "DMA and IOMMU",
        "Interrupts",
        "Completion, fences, and deferred work",
        "Power, reset, and hotplug",
        "Contract checklist for A1",
        "References",
    ],
    "docs/05-hardware-test-plan.md": [
        "QEMU environment",
        "Real-hardware environment",
        "Coverage matrix",
        "Pass/fail criteria",
        "Repeatability",
        "Limitations",
        "References",
    ],
    "docs/06-stage0-review-response.md": [
        "Review actions",
        "A1 review request",
        "Matrix schema v2",
        "Status policy",
        "Required report back",
        "Not tested",
    ],
}

README_SECTIONS = ["Status", "Evidence", "Stage 0 review response", "Decisions", "Risks", "Next step"]


def main() -> int:
    errors: list[str] = []

    for rel in REQUIRED_FILES:
        path = ROOT / rel
        if not path.is_file():
            errors.append(f"missing file: {rel}")

    for rel, sections in REQUIRED_SECTIONS.items():
        path = ROOT / rel
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        for section in sections:
            if section.lower() not in text.lower():
                errors.append(f"{rel}: missing section {section!r}")
        if text.count("http://") + text.count("https://") < 5:
            errors.append(f"{rel}: fewer than five references")

    readme = ROOT / "README.md"
    if readme.is_file():
        text = readme.read_text(encoding="utf-8")
        for section in README_SECTIONS:
            if section.lower() not in text.lower():
                errors.append(f"README.md: missing {section!r}")

    platform = ROOT / "include/o0/platform.h"
    if platform.is_file():
        text = platform.read_text(encoding="utf-8")
        for symbol in ("O0_PLATFORM_ABI_VERSION", "o0_platform_ops", "mmio_map", "dma_map_sg", "irq_alloc", "fence_signal"):
            if symbol not in text:
                errors.append(f"include/o0/platform.h: missing {symbol!r}")

    block = ROOT / "include/o0/block.h"
    if block.is_file():
        text = block.read_text(encoding="utf-8")
        for symbol in ("o0_block_dev", "o0_bio", "o0_block_submit_bio", "o0_block_complete_bio", "O0_BLOCK_F_ROTATIONAL"):
            if symbol not in text:
                errors.append(f"include/o0/block.h: missing {symbol!r}")

    if (ROOT / "tools/validate_matrix.py").is_file():
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/validate_matrix.py")],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            errors.append("matrix validation failed")
            errors.extend(result.stdout.splitlines())
            errors.extend(result.stderr.splitlines())

    if errors:
        print("FAIL: deliverable check")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("PASS: deliverable check")
    print(f"  files: {len(REQUIRED_FILES)}")
    print(f"  documents checked: {len(REQUIRED_SECTIONS)}")
    print("  matrix validator: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
