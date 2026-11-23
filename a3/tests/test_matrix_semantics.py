#!/usr/bin/env python3
"""Semantic checks for the hardware support matrix."""

from __future__ import annotations

import copy
import csv
import io
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import validate_matrix  # noqa: E402


def load_rows() -> dict[str, dict[str, str]]:
    with (ROOT / "matrix/hardware-support-matrix.csv").open(newline="", encoding="utf-8") as f:
        return {row["id"]: row for row in csv.DictReader(f)}


class TestMatrixSemantics(unittest.TestCase):
    def test_validator_passes(self) -> None:
        with redirect_stdout(io.StringIO()):
            self.assertEqual(validate_matrix.main(), 0)

    def test_required_families_present(self) -> None:
        rows = load_rows()
        required = {
            "hdd-sata-ahci",
            "ssd-sata-ahci",
            "ssd-nvme-pcie",
            "usb-msc-bot",
            "usb-uas",
        }
        self.assertTrue(required.issubset(rows))

    def test_every_row_has_review_fields(self) -> None:
        for rid, row in load_rows().items():
            with self.subTest(rid=rid):
                for field in (
                    "spec_revision",
                    "implementation_status",
                    "emulator_available",
                    "real_hardware",
                    "acceptance_test",
                ):
                    self.assertTrue(row[field].strip(), f"{rid} empty {field}")
                self.assertIn(row["implementation_status"], validate_matrix.ALLOWED_STATUS)

    def test_spec_revisions_are_pinned(self) -> None:
        for rid, row in load_rows().items():
            with self.subTest(rid=rid):
                self.assertNotIn(".x", row["spec_revision"].lower())

    def test_media_does_not_name_interface_or_protocol(self) -> None:
        for rid, row in load_rows().items():
            media = row["media"].lower()
            with self.subTest(rid=rid):
                for token in ("usb", "sata", "nvme", "pcie", "pci express"):
                    self.assertNotIn(token, media, f"{rid} media names {token}")

    def test_nvme_row_is_pcie_not_media(self) -> None:
        row = load_rows()["ssd-nvme-pcie"]
        self.assertEqual(row["interface"].lower(), "pci express")
        self.assertEqual(row["driver_family"], "nvme")
        self.assertIn("nvme", row["protocol"].lower())
        self.assertIn("2.4", row["spec_revision"])
        self.assertIn("2026-08-04", row["spec_revision"])
        self.assertNotIn("nvme", row["media"].lower())

    def test_sata_rows_share_ahci_but_keep_media_separate(self) -> None:
        rows = load_rows()
        hdd = rows["hdd-sata-ahci"]
        ssd = rows["ssd-sata-ahci"]
        self.assertEqual(hdd["driver_family"], "ahci")
        self.assertEqual(ssd["driver_family"], "ahci")
        self.assertIn("hdd", hdd["media"].lower())
        self.assertIn("ssd", ssd["media"].lower())
        for rid in ("hdd-sata-ahci", "ssd-sata-ahci"):
            with self.subTest(rid=rid):
                self.assertNotIn("nvme", rows[rid]["protocol"].lower())
                self.assertIn("sata", rows[rid]["interface"].lower())

    def test_usb_rows_use_distinct_drivers_not_media(self) -> None:
        rows = load_rows()
        self.assertEqual(rows["usb-msc-bot"]["driver_family"], "usb-bot")
        self.assertEqual(rows["usb-uas"]["driver_family"], "usb-uas")
        for rid in ("usb-msc-bot", "usb-uas"):
            with self.subTest(rid=rid):
                self.assertIn("usb", rows[rid]["interface"].lower())
                self.assertNotIn("usb", rows[rid]["media"].lower())

    def test_optional_rows_are_deferred(self) -> None:
        rows = load_rows()
        for rid in ("sata-port-multiplier", "nvme-zns"):
            with self.subTest(rid=rid):
                self.assertEqual(rows[rid]["implementation_status"], "deferred")
                self.assertEqual(rows[rid]["target"], "deferred")
                self.assertIn("deferred", rows[rid]["notes"].lower())

    def test_gpu_has_no_support_claim(self) -> None:
        row = load_rows()["gpu-haswell-gen75"]
        self.assertEqual(row["implementation_status"], "not-started")
        self.assertIn("no gpu support claim", row["notes"].lower())
        self.assertIn("5a-5e", row["acceptance_test"].lower())

    def test_validator_rejects_usb_as_media(self) -> None:
        rows = copy.deepcopy(list(load_rows().values()))
        for row in rows:
            if row["id"] == "usb-msc-bot":
                row["media"] = "USB flash"
        errors = validate_matrix.validate_rows(rows)
        self.assertTrue(any("media must not be USB" in error for error in errors))

    def test_validator_rejects_unpinned_spec(self) -> None:
        rows = copy.deepcopy(list(load_rows().values()))
        for row in rows:
            if row["id"] == "ssd-nvme-pcie":
                row["spec_revision"] = "NVMe Base 2.x"
        errors = validate_matrix.validate_rows(rows)
        self.assertTrue(any("not pinned" in error for error in errors))

    def test_validator_rejects_non_deferred_optional_row(self) -> None:
        rows = copy.deepcopy(list(load_rows().values()))
        for row in rows:
            if row["id"] == "nvme-zns":
                row["implementation_status"] = "not-started"
        errors = validate_matrix.validate_rows(rows)
        self.assertTrue(any("implementation_status 'deferred'" in error for error in errors))



if __name__ == "__main__":
    unittest.main()
