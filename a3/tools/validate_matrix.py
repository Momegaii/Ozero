#!/usr/bin/env python3
"""Validate the Ozero Stage 0 hardware support matrix.

The matrix is a design artifact: every row must pin a specification revision,
name an implementation status, an emulator, real hardware and an acceptance
test, and keep media separate from transport and protocol.
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

REQUIRED_COLUMNS = [
    "id",
    "device_class",
    "driver_family",
    "media",
    "interface",
    "protocol",
    "target",
    "spec_revision",
    "implementation_status",
    "emulator_available",
    "real_hardware",
    "acceptance_test",
    "known_gaps",
    "notes",
]

ALLOWED_STATUS = {
    "not-started",
    "design-only",
    "probe-only",
    "partial",
    "complete",
    "deferred",
}

ALLOWED_CLASS = {"storage", "gpu", "virtual"}

SPEC_HINTS = (
    "AHCI",
    "NVMe",
    "USB",
    "ATA",
    "PCI",
    "SCSI",
    "Virtio",
    "i915",
    "PRM",
)
TEST_HINTS = ("QEMU", "real", "IGT", "hardware", "nvme-cli", "smartctl", "fio", "deferred")

FORBIDDEN_DRIVER_FAMILIES = {"sata", "usb", "ssd", "hdd", "pci", "pcie", "storage"}

REQUIRED_FAMILIES = {
    "hdd-sata-ahci": ("storage", "ahci", "HDD", "SATA"),
    "ssd-sata-ahci": ("storage", "ahci", "SSD", "SATA"),
    "ssd-nvme-pcie": ("storage", "nvme", "SSD", "PCI Express"),
    "usb-msc-bot": ("storage", "usb-bot", "media", "USB"),
    "usb-uas": ("storage", "usb-uas", "media", "USB"),
}


def validate_rows(rows: list[dict[str, str]]) -> list[str]:
    errors: list[str] = []
    seen: set[str] = set()

    for line, row in enumerate(rows, start=2):
        rid = (row.get("id") or "").strip()
        if not rid:
            errors.append(f"line {line}: empty id")
            continue
        if rid in seen:
            errors.append(f"line {line}: duplicate id {rid!r}")
        seen.add(rid)

        for column in REQUIRED_COLUMNS:
            if not (row.get(column) or "").strip():
                errors.append(f"line {line} ({rid}): empty {column}")

        status = (row.get("implementation_status") or "").strip()
        if status not in ALLOWED_STATUS:
            errors.append(f"line {line} ({rid}): invalid implementation_status {status!r}")

        cls = (row.get("device_class") or "").strip()
        if cls not in ALLOWED_CLASS:
            errors.append(f"line {line} ({rid}): invalid device_class {cls!r}")

        family = (row.get("driver_family") or "").strip().lower()
        if family in FORBIDDEN_DRIVER_FAMILIES:
            errors.append(f"line {line} ({rid}): driver_family {family!r} names media or transport")

        spec = (row.get("spec_revision") or "").strip()
        if ".x" in spec.lower():
            errors.append(f"line {line} ({rid}): spec_revision is not pinned: {spec!r}")
        if not any(hint.upper() in spec.upper() for hint in SPEC_HINTS):
            errors.append(f"line {line} ({rid}): spec_revision has no recognised specification")

        test = (row.get("acceptance_test") or "").strip().lower()
        if not any(hint.lower() in test for hint in TEST_HINTS):
            errors.append(f"line {line} ({rid}): acceptance_test has no recognised test vehicle")

    by_id = {row["id"]: row for row in rows}

    for rid, (cls, family, media_hint, interface_hint) in REQUIRED_FAMILIES.items():
        row = by_id.get(rid)
        if row is None:
            errors.append(f"missing required family row {rid!r}")
            continue
        if row["device_class"] != cls:
            errors.append(f"{rid}: expected device_class {cls!r}")
        if row["driver_family"].lower() != family:
            errors.append(f"{rid}: expected driver_family {family!r}")
        if media_hint.lower() not in row["media"].lower():
            errors.append(f"{rid}: expected media to mention {media_hint!r}")
        if interface_hint.lower() not in row["interface"].lower():
            errors.append(f"{rid}: expected interface to mention {interface_hint!r}")

    hdd = by_id.get("hdd-sata-ahci", {})
    ssd = by_id.get("ssd-sata-ahci", {})
    if hdd.get("driver_family") != ssd.get("driver_family"):
        errors.append("SATA HDD and SATA SSD must share the AHCI driver family")
    if hdd.get("driver_family") != "ahci":
        errors.append("SATA HDD must use the AHCI driver family")
    if "nvme" in hdd.get("protocol", "").lower():
        errors.append("hdd-sata-ahci: HDD must not use an NVMe protocol")
    if "nvme" in ssd.get("protocol", "").lower():
        errors.append("ssd-sata-ahci: SATA SSD must not use an NVMe protocol")

    nvme = by_id.get("ssd-nvme-pcie", {})
    if nvme.get("interface", "").lower() != "pci express":
        errors.append("ssd-nvme-pcie: NVMe row must use PCI Express, not a media name")
    if nvme.get("driver_family") != "nvme":
        errors.append("ssd-nvme-pcie: expected driver_family 'nvme'")
    if "2.4" not in nvme.get("spec_revision", "") or "2026-08-04" not in nvme.get("spec_revision", ""):
        errors.append("ssd-nvme-pcie: spec_revision must pin NVMe Base 2.4 (2026-08-04)")

    bot = by_id.get("usb-msc-bot", {})
    uas = by_id.get("usb-uas", {})
    if bot.get("driver_family") == uas.get("driver_family"):
        errors.append("USB BOT and UAS must remain distinct driver families")
    if bot.get("driver_family") != "usb-bot":
        errors.append("usb-msc-bot: expected driver_family 'usb-bot'")
    if uas.get("driver_family") != "usb-uas":
        errors.append("usb-uas: expected driver_family 'usb-uas'")

    legacy = by_id.get("hdd-legacy-ata", {})
    if "ahci" in legacy.get("protocol", "").lower():
        errors.append("hdd-legacy-ata: legacy ATA must not use an AHCI protocol")

    for rid in ("sata-port-multiplier", "nvme-zns"):
        row = by_id.get(rid, {})
        if row.get("implementation_status") != "deferred":
            errors.append(f"{rid}: review requires implementation_status 'deferred'")
        if row.get("target") != "deferred":
            errors.append(f"{rid}: review requires target 'deferred'")
        if "deferred" not in row.get("notes", "").lower():
            errors.append(f"{rid}: notes must explain the deferral")

    gpu = by_id.get("gpu-haswell-gen75", {})
    if gpu.get("implementation_status") != "not-started":
        errors.append("gpu-haswell-gen75: no GPU support claim before real tests")
    if "no gpu support claim" not in gpu.get("notes", "").lower():
        errors.append("gpu-haswell-gen75: notes must state no GPU support claim")

    for rid in ("ssd-sata-ahci", "ssd-nvme-pcie"):
        media = by_id.get(rid, {}).get("media", "").lower()
        if "sata" in media or "nvme" in media or "pcie" in media:
            errors.append(f"{rid}: media must not name an interface or protocol")
    for rid in ("usb-msc-bot", "usb-uas"):
        media = by_id.get(rid, {}).get("media", "").lower()
        if "usb" in media:
            errors.append(f"{rid}: media must not be USB; USB belongs in interface")

    return errors


def main() -> int:
    path = Path(__file__).resolve().parents[1] / "matrix" / "hardware-support-matrix.csv"
    if not path.is_file():
        print(f"FAIL: missing {path}")
        return 1

    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames != REQUIRED_COLUMNS:
            print("FAIL: unexpected columns")
            print(f"  expected: {REQUIRED_COLUMNS}")
            print(f"  actual:   {reader.fieldnames}")
            return 1
        rows = list(reader)

    errors = validate_rows(rows)
    if errors:
        print("FAIL: matrix validation found problems")
        for error in errors:
            print(f"  - {error}")
        return 1

    statuses = sorted({row["implementation_status"] for row in rows})
    families = sorted({row["driver_family"] for row in rows})
    deferred = sorted(row["id"] for row in rows if row["implementation_status"] == "deferred")
    print("PASS: matrix validation")
    print(f"  rows: {len(rows)}")
    print(f"  statuses: {', '.join(statuses)}")
    print(f"  driver families: {', '.join(families)}")
    print(f"  deferred: {', '.join(deferred)}")
    print("  every row pins a revision, an emulator, real hardware and an acceptance test")
    return 0


if __name__ == "__main__":
    sys.exit(main())
