# Ozero A3 — Stage 0 Review Response

Status: **desk response; no implementation or hardware validation**

Review date: 23 November 2026
Review basis: director review plus direct inspection of `main`, `a1/stage0-review` @ `74b1471` (decision `85df005`; headers `30ee4f0`), `agent/a2-stage0` @ `64b3458` (code `6d305b8`; IF-009 `d02598a`) and `a4/stage0-review-followup` @ `b4232a0`
Artifact commit: see [`EVIDENCE.md`](../EVIDENCE.md) §7 and `git log --oneline`

This is a response to the 23 November 2026 Stage 0 review. It is not an
acceptance certificate. It records what changed, what remains open, and what
was not tested.

## 1. Review actions

| # | Review action | Status | Artifact / evidence |
|---|---|---|---|
| 1 | Get A1 written review of `platform.h`/`block.h` before the AHCI probe: DMA sleep rules, reset, no-in-flight-DMA guarantee, address width, alignment, coherency | **A1 review received; A3 published IF-008 v1.1; A1 ACCEPT at `b9a69d8`; Director D-007 pending; AHCI probe blocked** | A1 `a1/stage0-review` @ `74b1471` (decision `85df005`; headers `30ee4f0`), `docs/if008-v1.1-review.md`; `docs/interfaces/IF-008-dma-platform.md`; §2 below |
| 2 | For each matrix row record exact spec revision, implementation status, emulator availability, real hardware, acceptance test | **done** | `matrix/hardware-support-matrix.csv` schema v2; `tools/validate_matrix.py` |
| 3 | Separate media from transport; keep HDD/SATA, SSD/SATA, SSD/NVMe as product cases, not one driver | **done** | `driver_family` column; `docs/01` §2–§3 |
| 4 | Defer ZNS and SATA port multiplier unless a concrete requirement justifies them | **done** | both rows are `deferred`; validator enforces it |
| 5 | Split Stage 5 into GPU objects, GPU VA mapping, residency/eviction, command validation/submission, completion fences | **done** | `docs/03` §13 gates 5A–5E |
| 6 | No GPU support claim until real Haswell or clearly scoped emulator tests exist | **done** | GPU row `not-started`; `docs/03` §14; `README` key position 7 |

Definition-of-done status:

| Criterion | Status |
|---|---|
| A1 signs DMA/platform contracts or API changes are made | **met for A1**: A1 signed the generic `mm.h`/`driver.h` surface and then ACCEPTed IF-008 v1.1 at `b9a69d8`; Director D-007 remains pending |
| Every matrix row has pinned source and test method | **done** |
| Status reflects implementation and testing, not ambition | **done**: all rows are `not-started` or `deferred` |
| AHCI probe begins only after DMA/reset contract approval | **blocked** |
| Haswell notes explicitly distinguish Gen7.5 from Gen8+ | **done** |

## 2. A1 review request and response

A1's written review is available on branch `a1/stage0-review` at commit
`30ee4f0`, in `docs/interface-draft.md` §11, `docs/a3-dma-contract.md` and
`include/ozero/mm.h` / `include/ozero/driver.h`. A1 signed the generic services
subject to one IOMMU blocker and five conditions, then recorded **ACCEPT** of
IF-008 v1.1 at A3 `b9a69d8` in `85df005`. The original questions are kept below
so the record shows what was asked; the disposition table records what was
answered.

### 2.0 A1 disposition (received 23 November 2026)

| Item | A1 disposition | A3 response |
|---|---|---|
| Address width | Signed: `addr_mask` + `max_segment`, validated; CPU physical width is only an upper bound | Accepted |
| IOMMU assumptions | **Blocker**: no IOMMU in Stage 1 (Limine disables VT-d/AMD-Vi); returned addresses are physical and untranslated; no domain handle | Accepted for trusted ring-0 Stage 1; escalate before untrusted or userspace DMA |
| Coherency | Signed: explicit `coherent` flag plus `o0_dma_sync`; no implicit maintenance | Accepted |
| Map/unmap lifetime | Signed: BAR until unmap; DMA buffer until free; both after quiesce. Reset invalidates in-flight requests and device state, but does **not** free kernel DMA buffers or unmap BARs; the driver reinitialises | Accepted; A3 will not free buffers or unmap BARs in `reset` |
| Reset and in-flight DMA | Signed: `o0_device_dma_quiesce` then `o0_device_reset`; `o0_io_cancel` is best-effort and completion may still occur | Accepted; A3 will quiesce before reset and keep buffers alive until completion |
| Block I/O shape | Signed: `o0_io_limits` for transfer size, queue depth and alignment; one contiguous buffer per `o0_io_request`; block semantics stay in A3 | Accepted; `block.h` embeds one `struct o0_io_request` and carries no SG pointer |
| Interrupt routing | Signed: GSI/vector split; MSI/MSI-X vector allocation; handler context rules | Accepted |
| Driver callbacks | Signed: A3 implements `struct o0_device_ops` (`dma_constraints`, `dma_quiesce`, `reset`, `io_submit`, `io_cancel`); A1 wrappers validate | Accepted; A3 will register the table with `o0_device_register_ops` |
| Device-class neutrality | Signed and automated by `tools/check-headers.sh` | Accepted |

A1's conditions (latest, `30ee4f0`):

1. No untrusted or userspace driver receives DMA before an IOMMU domain exists.
   Stage 1 is trusted ring-0 drivers only (D-021). **A3 accepts for Stage 1.**
2. A3 must not assume one `o0_dma_buffer` covers a transfer larger than
   `max_segment`; it splits the transfer into multiple buffers and requests.
   There is no scatter-gather pointer in `o0_io_request` in Stage 1.
   **A3 accepts; `block.h` embeds one `o0_io_request` and documents the split.**
3. A3 must not assume BAR mappings come from the HHDM; they are dedicated
   UC/NX mappings. **A3 accepts; AHCI and GPU drivers must use
   `o0_device_map_bar`.**
4. A3 must not assume reset preserves queue state, DMA buffers or BAR mappings.
   **A3 accepts; `reset` only resets A3 protocol state.**
5. Any semantic change is a versioned interface change, not a driver-local
   decision. **A3 accepts.**

A3 reconciliation completed in IF-008 v1.1:

- `include/ozero/platform.h` includes A1's `<ozero/types.h>`, `<ozero/status.h>`,
  `<ozero/irq.h>`, `<ozero/mm.h>` and `<ozero/driver.h>` directly. It does not
  redeclare A1 types and does not define `o0_dma_domain`, `dma_map_sg`,
  `dma_map_single`, `iommu_map`, `o0_reset_kind`, `o0_platform_ops`,
  `dma_domain_t`, `iommu_domain_t`, `iova_t` or `dma_addr_t`.
- `include/ozero/block.h` embeds `struct o0_io_request io` in `struct o0_bio`;
  `io.complete` is the only A1 completion callback. A bio larger than
  `max_segment` is split into multiple buffers and requests. There is no SG
  pointer in the generic request.
- A3 implements `struct o0_device_ops` and registers it with
  `o0_device_register_ops`; A1 wrappers validate and dispatch.
- `reset` resets A3 protocol state after A1's `o0_device_dma_quiesce` and
  `o0_device_reset`; it does not free A1 DMA buffers or unmap BARs.
- `tests/header_smoke.c` and `tests/a1_header_smoke.c` compile against A1's
  actual headers at `30ee4f0`.

Remaining before the AHCI probe:

- A1 has ACCEPTed IF-008 v1.1 at `b9a69d8` (`85df005`).
- The director must approve global D-007 and D-021, and accept the no-IOMMU
  Stage 1 limitation for trusted ring-0 drivers.
- D-009 is **Approved** in A4 `b4232a0`: `include/ozero/` is the only public
  root, A1 `03f8883` implements it, and XB-007 is closed. A3 is aligned.
- D-010 is **Approved** in A4 `b4232a0`: A1 owns `tools/check-headers.sh` and
  `tests/abi_static_asserts.c`; A4 owns CI integration; XB-008 is closed.
- XB-011 is closed in A4 `b4232a0`; the A4 candidate still lacks `a3/` and root `include/ozero/`. A3 PR #4 is open into the A4 candidate.

The questions in §2.1-§2.5 are the original A3 request, kept for audit. A1's
answers are in §2.0 and IF-008 v1.1. Symbols in those questions that are absent
from A1's latest headers, such as `dma_map_single`, `o0_dma_domain` and
`iommu_map`, were answered by removal; they are not current ABI requirements.
Unanswered questions would need a new IF-008 version.

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
| A1 owner | A1 | Signed its Stage 1 surface with IOMMU blocker and five conditions; recorded **ACCEPT** of IF-008 v1.1 at `b9a69d8` (`85df005`) | 2026-11-23 | `a1/stage0-review` @ `74b1471`; `docs/if008-v1.1-review.md`; headers `30ee4f0` |
| A3 owner | A3 | Accepted A1 conditions; published IF-008 v1.1 and acceptance record | 2026-11-23 | `docs/interfaces/IF-008-v1.1-acceptance.md`; A1 `85df005` |

### 2.7 Global decisions and cross-agent blockers

Global IDs come from A4's decision register and cross-agent blocker register.

| ID | Dependency | State | A3 note |
|---|---|---|---|
| D-001 | A1/A2/A3 domain split and team slugs | Proposed | A3 owns storage/GPU device protocols; no change requested |
| D-002 | Branch isolation, review gate and `main` protection | Proposed | A3 has not pushed to `main`; branch protection is A4's check |
| D-005 | Boot protocol and kernel handoff | Proposed | A3 consumes A1's boot/platform data; Limine is a candidate only |
| D-006 | Accept Stage 0 | Proposed | Director did not accept Stage 0; this package is desk evidence |
| D-007 | Approve IF-008 and the A1 sign-off gate | Proposed | A1 ACCEPT at `b9a69d8` (`85df005`); Director D-007 pending |
| D-009 / XB-007 | Public header namespace | **Approved; XB-007 Closed** | A4 `b4232a0`: `include/ozero/` only; A1 `03f8883`; A3 aligned |
| D-010 / XB-008 | `tools/check-headers.sh` ownership | **Approved; XB-008 Closed** | A4 `b4232a0`: A1 owns checker and static asserts; A4 owns CI |
| D-020 | DMA is constraint-driven, never device-class-driven | Proposed | A3 supplies `o0_dma_constraint`; A1 never infers it from a storage class |
| D-021 | IOMMU policy: trusted ring-0 only in Stage 1 | Proposed | A3 accepts no IOMMU domain, no IOVA and no translated DMA type in Stage 1 |
| D-031 | Branch protection | Proposed | No merge to `main`; A3 PR #4 targets the A4 candidate, not `main` |
| XB-001 | A1 reviews A3 IF-008 before AHCI probe | **Closed** | A1 ACCEPT recorded at `b9a69d8` (`85df005`); A4 `b4232a0` closed the row |
| XB-005 | A3 device tests and hardware requirements have evidence | Open | Static checks only; no QEMU or hardware run |
| XB-011 | Publish A3 so A4 can refresh the audit | **Closed** | A4 `b4232a0` closed the row; A3 PR #4 is open into the A4 candidate |
| XB-004 | Limine pin and boot protocol | Open | A3 consumes A1's boot/platform data; the MBR harness is not Limine evidence |

A3 does not start AHCI probe work while D-007 and D-021 are Proposed. XB-001 is closed.

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
- Decisions needed: D-001, D-002, D-005, D-006, D-007, D-020, D-021 and D-031 in [`README.md`](../README.md); IF-008 v1.1 is A1-ACCEPTed at `b9a69d8`; Director D-006/D-007/D-021/D-031 remain pending. D-009 and D-010 are approved in A4 `b4232a0`.
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
- No Director approval of D-007 or D-021, and no acceptance of the no-IOMMU
  Stage 1 limitation.
- No AHCI probe work; it remains blocked until the interface is approved.

A successful header compile, shell syntax check, matrix validation, or unit
test does not change this list.

## 7. References

- A1, *Ozero Stage 0 interface draft* §11 and *A3 DMA and platform interface:
  A1 approval conditions*, branch `a1/stage0-review` @ `30ee4f0`,
  `docs/interface-draft.md`, `docs/a3-dma-contract.md`,
  `include/ozero/mm.h`, `include/ozero/driver.h`.
- A3, *IF-008: A3 DMA and platform interface* v1.1,
  [`docs/interfaces/IF-008-dma-platform.md`](interfaces/IF-008-dma-platform.md).
- A4, *Global decision register* and *Cross-agent blocker register*:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/decisions/decision-log.md>
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/process/cross-agent-blockers.md>
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
