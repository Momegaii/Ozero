# Ozero A3 — Stage 0 Review Response

Status: **desk response; no implementation or hardware validation**

Review date: 23 November 2026
Review basis: director-supplied agent summary, not direct repository inspection
Artifact commit: see [`EVIDENCE.md`](../EVIDENCE.md) §7 and `git log --oneline`

This is a response to the 23 November 2026 Stage 0 review. It is not an
acceptance certificate. It records what changed, what remains open, and what
was not tested.

## 1. Review actions

| # | Review action | Status | Artifact / evidence |
|---|---|---|---|
| 1 | Get A1 written review of `platform.h`/`block.h` before the AHCI probe: DMA sleep rules, reset, no-in-flight-DMA guarantee, address width, alignment, coherency | **open — AHCI probe blocked** | §2 below; no A1 signature exists |
| 2 | For each matrix row record exact spec revision, implementation status, emulator availability, real hardware, acceptance test | **done** | `matrix/hardware-support-matrix.csv` schema v2; `tools/validate_matrix.py` |
| 3 | Separate media from transport; keep HDD/SATA, SSD/SATA, SSD/NVMe as product cases, not one driver | **done** | `driver_family` column; `docs/01` §2–§3 |
| 4 | Defer ZNS and SATA port multiplier unless a concrete requirement justifies them | **done** | both rows are `deferred`; validator enforces it |
| 5 | Split Stage 5 into GPU objects, GPU VA mapping, residency/eviction, command validation/submission, completion fences | **done** | `docs/03` §13 gates 5A–5E |
| 6 | No GPU support claim until real Haswell or clearly scoped emulator tests exist | **done** | GPU row `not-started`; `docs/03` §14; `README` key position 7 |

Definition-of-done status:

| Criterion | Status |
|---|---|
| A1 signs DMA/platform contracts or API changes are made | **open** |
| Every matrix row has pinned source and test method | **done** |
| Status reflects implementation and testing, not ambition | **done**: all rows are `not-started` or `deferred` |
| AHCI probe begins only after DMA/reset contract approval | **blocked** |
| Haswell notes explicitly distinguish Gen7.5 from Gen8+ | **done** |

## 2. A1 review request

The following questions must be answered in writing before the AHCI probe
starts. The current headers are drafts; silence is not approval.

### 2.1 DMA and IOMMU

- Which contexts may call `dma_map_single`, `dma_map_sg`, and
  `dma_unmap_*`? May they sleep?
- Which contexts may call `dma_sync_single_for_cpu` and
  `dma_sync_single_for_device`? Are they non-sleeping?
- Does `dma_alloc_coherent` guarantee CPU/device coherency, or must A3 call
  `dma_sync_*` as well?
- What address width does `o0_dma_addr_t` carry on each platform? What is the
  segment alignment, boundary, and maximum segment size?
- Does A1 provide a bounce-buffer path for devices below the system's DMA
  mask? Who owns the bounce buffer?
- What is the lifetime of `struct o0_dma_domain`? Can it be shared by
  functions behind a bridge or IOMMU group?
- Are `iommu_map`/`iommu_unmap` allowed to sleep? Who invalidates the IOTLB?

### 2.2 Reset and no-in-flight DMA

- What do `O0_RESET_FUNCTION_LEVEL`, `O0_RESET_HOT`, `O0_RESET_LINK`,
  `O0_RESET_DEVICE`, and `O0_RESET_BUS` guarantee on Haswell?
- Does `pci_reset` guarantee that the device can no longer DMA when it
  returns?
- Who quiesces in-flight requests before reset? Does A1 wait for
  completions, or does A3?
- What state is preserved across reset: BARs, config space, MSI/MSI-X
  vectors, power state?
- How is a function-level reset (FLR) handled on a shared PCI function?

### 2.3 Firmware tables and memory map

- How long is an ACPI table returned by `acpi_find_table` valid?
- Does A1 copy tables from `EfiACPIReclaimMemory` before reclaim? If not,
  when must A3 copy them?
- How long is the UEFI memory map from `fw_memmap` valid?
- How long is a configuration table from `fw_config_table` valid?
- What happens to these pointers after `ExitBootServices` and
  `SetVirtualAddressMap`?

### 2.4 Interrupts and MMIO

- May `irq_request`, `irq_free`, and `irq_set_affinity` sleep?
- Who acknowledges the interrupt: A1's interrupt controller layer or A3's
  handler?
- How are legacy shared INTx interrupts distinguished from MSI/MSI-X?
- Is `mmio_read64` atomic on all supported platforms? If not, which registers
  are safe to read as one 64-bit access?
- Does `mmio_flush` guarantee that posted writes have reached the device?
- Are write-combining mappings allowed for all BARs, or only for
  framebuffer-like BARs?

### 2.5 Block contract

- Who owns a `struct o0_bio` after `o0_block_submit_bio` returns?
- In which contexts may `end_io` run? May it sleep?
- Is `end_io` called exactly once, including on timeout and reset?
- What is the maximum queue depth and tag width? Does the block layer or the
  driver allocate tags?
- Who handles a request that is in flight when a reset occurs?
- Are flush, FUA, discard, and write-zeroes mandatory, optional, or
  device-dependent?

### 2.6 Sign-off

| Role | Name | Decision | Date | Notes |
|---|---|---|---|---|
| A1 owner | _pending_ | _pending_ | _pending_ | Must answer §2.1–§2.5 |
| A3 owner | _pending_ | _pending_ | _pending_ | Must not start AHCI probe before A1 |

## 3. Matrix schema v2

The matrix now has these columns:

`id, device_class, driver_family, media, interface, protocol, target,
spec_revision, implementation_status, emulator_available, real_hardware,
acceptance_test, known_gaps, notes`

Key changes:

- `driver_family` makes the driver boundary explicit: `ahci`, `nvme`,
  `usb-bot`, `usb-uas`, `ata-legacy`, `ahci-pm`, `nvme-zns`, `virtio-blk`,
  `i915`.
- `spec_revision` is pinned. The validator rejects `.x` versions.
- `implementation_status` uses `not-started`, `design-only`, `probe-only`,
  `partial`, `complete`, or `deferred`. No row claims implementation.
- `emulator_available`, `real_hardware`, and `acceptance_test` are separate
  fields, so a QEMU model is not mistaken for hardware evidence.
- HDD/SATA and SSD/SATA share `ahci` but keep different media. NVMe, BOT,
  UAS, and legacy ATA are distinct families.
- `sata-port-multiplier` and `nvme-zns` are `deferred`; they are not Stage 0
  commitments.

Pinned revisions used in the matrix:

| Area | Revision |
|---|---|
| AHCI | AHCI 1.3.1 |
| ATA | ATA/ATAPI ACS-5 |
| SATA | Serial ATA Revision 3.5 |
| NVMe | NVMe Base Specification 2.4 (2026-08-04); NVMe over PCIe Transport 2.4 (2026-08-04) |
| PCIe | PCI Express Base 3.0 |
| USB | USB 2.0; USB 3.2 |
| BOT | USB MSC Bulk-Only Transport 1.0 |
| UAS | USB Attached SCSI Protocol 1.0 |
| xHCI | xHCI 1.2 |
| SCSI | SCSI SBC-4 |
| Legacy ATA | ATA/ATAPI ACS-5; PCI IDE Controller Specification 1.0 |
| ZNS | NVMe ZNS Command Set 1.1 (deferred) |
| Virtio | Virtual I/O Device (VIRTIO) 1.2 |
| Haswell GPU | Intel Haswell PRM Volumes 1-5 (2013-2014); Linux i915 v6.6 |

## 4. Status policy

- `not-started`: no driver code and no accepted test evidence.
- `design-only`: interface or design text exists, but no driver code.
- `probe-only`: the device can be identified; I/O is not validated.
- `partial`: some I/O works, but the acceptance test is incomplete.
- `complete`: the acceptance test passes on the named hardware or emulator.
- `deferred`: deliberately not a current commitment.

All storage rows are `not-started`; ZNS and port multiplier are `deferred`.
No row is `complete`. A successful compile is not evidence for any row.

## 5. Required report back

- Exact branch, commit, and files: see [`EVIDENCE.md`](../EVIDENCE.md) §7 and
  the final review response. The artifact commit is the commit that contains
  this file and the matrix schema v2.
- Commands and raw output: `make check` output is recorded in
  [`EVIDENCE.md`](../EVIDENCE.md) §2.
- Decisions needed: D1–D6 in [`README.md`](../README.md); A1 questions in §2.
- Unresolved risks: see [`README.md`](../README.md) risks table and §6 below.
- Explicit not-tested list: §6 below.

## 6. Not tested

The following were **not** run in the authoring environment:

- No QEMU boot, AHCI, NVMe, USB BOT, UAS, legacy ATA, or virtio-blk test.
- No physical Haswell SATA, NVMe, USB, or PATA device test.
- No AHCI probe, command-list walk, NCQ, hotplug, or error-recovery test.
- No NVMe queue, namespace, PRP/SGL, MSI-X, or reset test.
- No USB enumeration, BOT reset recovery, UAS task-management, or hotplug
  test.
- No Haswell GPU discovery, GGTT, PPGTT, relocation, submission, fence,
  eDRAM, hang-recovery, or IGT test.
- No ACPI/UEFI table parsing on real Haswell firmware.
- No suspend/resume, runtime power management, or thermal test.
- No performance or endurance measurement.
- No A1 sign-off on the DMA, reset, firmware, interrupt, MMIO, or block
  contracts.

A successful header compile, shell syntax check, matrix validation, or unit
test does not change this list.

## 7. References

- Intel, *Serial ATA AHCI 1.3.1*.
  <https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf>
- NVM Express, *Specifications* (NVMe Base 2.4, NVMe over PCIe 2.4, ZNS 1.1).
  <https://nvmexpress.org/specifications/>
- UEFI Forum, *Specifications* (UEFI 2.11, ACPI 6.6).
  <https://uefi.org/specifications>
- Intel, *Intel 64 and IA-32 Architectures Software Developer's Manual*.
  <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
- Linux kernel, *i915* documentation.
  <https://docs.kernel.org/gpu/i915.html>
- QEMU, *NVMe Emulation* and *USB Devices*.
  <https://www.qemu.org/docs/master/system/devices/nvme.html>
  <https://www.qemu.org/docs/master/system/devices/usb.html>
