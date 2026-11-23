import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class If008ContractTests(unittest.TestCase):
    def setUp(self):
        self.platform = (ROOT / "include/ozero/platform.h").read_text(encoding="utf-8")
        self.block = (ROOT / "include/ozero/block.h").read_text(encoding="utf-8")
        self.if008 = (ROOT / "docs/interfaces/IF-008-dma-platform.md").read_text(
            encoding="utf-8"
        )

    def test_platform_uses_a1_signed_surface(self):
        a1 = "\n".join(
            (ROOT / rel).read_text(encoding="utf-8")
            for rel in (
                "include/ozero/types.h",
                "include/ozero/status.h",
                "include/ozero/irq.h",
                "include/ozero/mm.h",
                "include/ozero/driver.h",
            )
        )
        for symbol in (
            "o0_dma_constraint",
            "o0_dma_alloc",
            "o0_dma_sync",
            "o0_dma_free",
            "o0_device_dma_quiesce",
            "o0_device_reset",
            "o0_device_map_bar",
            "o0_device_irq",
            "o0_device_register_ops",
            "O0_MAP_UC",
        ):
            self.assertIn(symbol, a1)

    def test_platform_version_is_1_1(self):
        self.assertIn("0x00010001u", self.platform)

    def test_platform_has_no_unsupported_dma_or_reset_assumptions(self):
        for symbol in (
            "o0_dma_domain",
            "dma_map_sg",
            "dma_map_single",
            "iommu_map",
            "o0_reset_kind",
            "o0_platform_ops",
            "dma_domain_t",
            "iommu_domain_t",
            "iova_t",
            "dma_addr_t",
        ):
            self.assertNotIn(symbol, self.platform)

    def test_block_reset_has_no_kind_argument(self):
        self.assertIsNone(
            re.search(r"\(\*reset\)\s*\(\s*struct o0_block_dev\s*\*\s*dev\s*,", self.block)
        )
        self.assertIn("o0_status (*reset)(struct o0_block_dev *dev);", self.block)

    def test_block_embeds_one_generic_request(self):
        self.assertIn("struct o0_io_request io;", self.block)
        self.assertNotIn("struct o0_io_request *sg", self.block)

    def test_if008_records_global_decision_and_blocker(self):
        self.assertIn("IF-008", self.if008)
        self.assertIn("D-007", self.if008)
        self.assertIn("XB-001", self.if008)
        self.assertIn("trusted ring-0", self.if008)
        self.assertIn("o0_device_ops", self.if008)


if __name__ == "__main__":
    unittest.main()
