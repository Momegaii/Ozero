# Evidence — Ozero A3 Stage 0

Status: **Stage 0 desk artifact; no implementation or hardware validation**
Review date: 2026-11-23 (UTC)
Base commit on main: `147cc86c027cef7cec42525024d26dcccf66d7db`
Branch: `a3/stage0-review-response`
Stage 0 package commit: `a2890b5e3c070fb8a13fa64030a0a6e6a6e80714`
A1 review source: `origin/a1/stage0-review` @ `e2d7c0d`
A1 review response commit: this commit (HEAD)

Use `git rev-parse HEAD` and `git log -1 -- EVIDENCE.md` for the exact values.
The Stage 0 package commit contains the matrix schema v2, the review response,
the GPU gate split, and the validator/test changes.

This file records what was actually checked, what was read, and what was not
run. It is not a hardware validation report. No matrix row is `complete`.

## 1. Environment and scope

| Item | Value |
|---|---|
| Host OS | Debian container (OpenHands runtime) |
| Compiler | `gcc (Debian 14.2.0-19) 14.2.0` |
| Python | `3.13.15` |
| QEMU | **not installed** (`qemu-system-x86_64: command not found`) |
| Real Haswell hardware | **not attached** |
| Network | Available for specification and source retrieval |

Because QEMU is not installed and no real hardware is attached, no L1–L4
test from the test plan was executed. The support matrix and test plan are
planning artifacts, not proof of support. A successful compile, matrix
validation, or unit test is not hardware evidence.

## 2. Reproducible checks

Run from the repository root:

```sh
make check
```

Recorded output:

```text
cc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only tests/header_smoke.c
python3 tools/validate_matrix.py
PASS: matrix validation
  rows: 10
  statuses: deferred, not-started
  driver families: ahci, ahci-pm, ata-legacy, i915, nvme, nvme-zns, usb-bot, usb-uas, virtio-blk
  deferred: nvme-zns, sata-port-multiplier
  every row pins a revision, an emulator, real hardware and an acceptance test
python3 tools/check_deliverables.py
PASS: deliverable check
  files: 16
  documents checked: 6
  matrix validator: PASS
python3 -m unittest discover -s tests -p 'test_*.py'
.............
----------------------------------------------------------------------
Ran 13 tests in 0.003s

OK
sh -n tools/collect_inventory.sh
```

Individual commands:

| Check | Command | Result |
|---|---|---|
| Matrix structure | `python3 tools/validate_matrix.py` | PASS, 10 rows, 9 driver families |
| Deliverable completeness | `python3 tools/check_deliverables.py` | PASS, 16 files, 6 docs |
| Matrix semantics | `python3 -m unittest discover -s tests -p 'test_*.py'` | PASS, 13 tests |
| Header compile | `cc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only tests/header_smoke.c` | PASS |
| Inventory syntax | `sh -n tools/collect_inventory.sh` | PASS |

The matrix validator checks:

- required columns and non-empty cells;
- allowed `device_class` and `implementation_status` values;
- a pinned `spec_revision` per row, rejecting `.x` versions;
- a recognised specification, emulator, real-hardware entry, and acceptance
  test per row;
- `driver_family` does not name media or transport;
- presence of HDD/SATA, SSD/SATA, SSD/NVMe, USB BOT, and USB UAS rows;
- SATA HDD and SATA SSD share `ahci` but keep separate media;
- NVMe is not used for SATA rows;
- USB BOT and UAS remain distinct driver families;
- ZNS and SATA port multiplier are `deferred`;
- the GPU row is `not-started` and states no GPU support claim;
- media does not name USB, SATA, NVMe, PCIe, or PCI Express.

The matrix semantics tests include negative cases that prove the validator
rejects USB as media, unpinned `.x` revisions, and non-deferred optional rows.

## 3. Source verification

### 3.1 Specifications

| Source | Version / date | What was used |
|---|---|---|
| Intel AHCI | 1.3.1, revision 1_3_1 | ABAR, `PxCLB`/`PxFB`, `PxIS`/`PxIE`, `PxCMD`, `PxSACT`, `PxCI`, command list, command table, PRDT, FIS-based switching |
| NVM Express Base | 2.4, released 2026-08-04 | Controller architecture, queues, admin/NVM command sets, namespaces |
| NVMe over PCIe Transport | 2.4, released 2026-08-04 | PCIe binding |
| ACPI | 6.6, May 2025 | RSDP/XSDT/DSDT, MADT, SRAT, NFIT, PPTT, `_CRS`, `_OSC`, `_PRT` |
| UEFI | 2.11, December 2024 | System Table, Boot/Runtime Services, Configuration Table, memory map, `ExitBootServices`, Block I/O, PCI I/O, USB2 HC, GOP |
| USB 2.0 / 3.2 | USB 2.0; USB 3.2 (2017) | USB transport, descriptors, BOT/UAS context |
| USB BOT | 1.0 | CBW/CSW, one command per LUN, reset recovery |
| UAS | 1.0 | Command/status/data pipes, streams, task management |
| xHCI | 1.2 | Host controller registers, command/event/transfer rings, TRBs, doorbells, ports |
| SCSI SBC | SBC-4 | Block command set used by BOT and UAS |
| NVMe ZNS | Command Set 1.1 (deferred) | Zoned namespace commands, not implemented |
| PCI IDE | Controller Specification 1.0 | Legacy PATA programming interface |
| Virtio | Virtual I/O Device (VIRTIO) 1.2 | Test-only block device |
| PCI Express | PCI Express Base 3.0 | PCIe link, AER, MSI/MSI-X context |
| SATA-IO / T13 | Serial ATA Revision 3.5; ATA/ATAPI ACS-5 | SATA link/transport and ATA/ATAPI command set |
| Intel Haswell PRM | 2013–2014 volumes | GPU overview, command stream programming, MI instructions/structures, memory views, eDRAM |
| Intel SDM | current | CPU, memory, virtualization background |

Notes:

- The Intel AHCI PDF URL returns HTTP 403 to non-browser clients in this
  environment; the archived copy was used for section checks. The public URL
  is kept in the docs for readers with browser access.
- The Haswell PRM was retrieved from archived copies because the original
  01.org pages are retired. The archived landing page is cited in the GPU
  note.
- The Wayback Machine was intermittently unavailable during final link
  checks; the archived PRM content had already been retrieved and read.

### 3.2 Linux i915 source (v6.6)

The GPU note cites symbols, not only line numbers, because line numbers move.
The following files were read:

| File | Symbols / facts used |
|---|---|
| `drivers/gpu/drm/i915/i915_pci.c` | `GEN7_FEATURES`, `G75_FEATURES`, `GEN8_FEATURES`, `GEN_DEFAULT_PAGE_SIZES`, `GEN_DEFAULT_REGIONS` |
| `drivers/gpu/drm/i915/i915_drv.h` | `HAS_EDRAM`, `HAS_WT`, `HAS_FULL_PPGTT`, `HAS_LOGICAL_RING_CONTEXTS`, `HAS_64BIT_RELOC` |
| `drivers/gpu/drm/i915/gt/intel_ppgtt.c` | `gen7_ppgtt_enable`, `__ppgtt_create`, `vm.total = BIT_ULL(ppgtt_size)` |
| `drivers/gpu/drm/i915/gt/gen6_ppgtt.c` | `gen6_ppgtt_create`, `gen6_write_pde`, `pd_vma_bind`, `gen6_ppgtt_pin`, `gen7_ppgtt_enable` |
| `drivers/gpu/drm/i915/gt/intel_ggtt.c` | `init_aliasing_ppgtt`, `gen6_get_total_gtt_size`, `hsw_pte_encode`, `iris_pte_encode` |
| `drivers/gpu/drm/i915/gt/intel_gtt.h` | `HSW_CACHEABILITY_CONTROL`, `HSW_GTT_ADDR_ENCODE`, `HSW_PTE_ADDR_ENCODE` |
| `drivers/gpu/drm/i915/gt/intel_engine_regs.h` | `RING_PP_DIR_DCLV`, `PP_DIR_DCLV_2G`, `RING_PP_DIR_BASE`, `GFX_PPGTT_ENABLE` |
| `drivers/gpu/drm/i915/gt/intel_ring_submission.c` | `set_pp_dir`, `MI_SET_CONTEXT`, `MI_NOOP` workaround, Haswell engine setup |
| `drivers/gpu/drm/i915/gt/gen6_engine_cs.c` | `hsw_emit_bb_start`, `gen7_emit_flush_rcs`, `gen7_emit_breadcrumb_rcs` |
| `drivers/gpu/drm/i915/gt/intel_gpu_commands.h` | `__gen6_emit_bb_start`, `MI_BATCH_PPGTT_HSW`, `MI_BATCH_NON_SECURE_HSW` |
| `drivers/gpu/drm/i915/gem/i915_gem_execbuffer.c` | Execbuffer phases, relocation, `I915_EXEC_NO_RELOC` |
| `drivers/gpu/drm/i915/soc/intel_dram.c` | `intel_dram_edram_detect` |
| `drivers/gpu/drm/i915/i915_reg.h` | `HSW_EDRAM_CAP` (`0x120010`), `EDRAM_ENABLED` |

### 3.3 Device IDs

| Device | ID | Source |
|---|---|---|
| QEMU q35 AHCI | `8086:2922` | PCI ID database; QEMU `ich9-ahci` |
| QEMU NVMe | `1b36:0010` | QEMU documentation; PCI ID database |
| QEMU xHCI | `1b36:000d` | QEMU documentation; PCI ID database |
| QEMU virtio-blk | `1af4:1001`, `1af4:1042` | QEMU documentation; PCI ID database |
| Haswell PCH SATA AHCI | `8086:8c02`, `8086:8c03` | PCI ID database |
| Haswell PCH xHCI | `8086:8c31` | PCI ID database |
| Haswell PCH EHCI | `8086:8c26` | PCI ID database |
| Haswell GT2 GPU | `8086:0412` | i915 PCI ID tables |
| Haswell ULT GT3 GPU | `8086:0a26` | i915 PCI ID tables |
| Haswell CRW GT3 GPU | `8086:0d26` | i915 PCI ID tables |

These IDs are examples for test discovery. The support policy binds by class
and protocol, not by a hard-coded VID/PID list.

## 4. Review action status

| # | Review action | Status | Artifact |
|---|---|---|---|
| 1 | A1 written review before AHCI probe | **received; conditions accepted; reconciliation pending — AHCI probe blocked** | `origin/a1/stage0-review` @ `e2d7c0d`, `docs/interface-draft.md` §11; `docs/06` §2 |
| 2 | Pin spec revision, status, emulator, hardware, test per row | done | matrix schema v2 |
| 3 | Separate media from transport; keep drivers distinct | done | `docs/01` §2–§3 |
| 4 | Defer ZNS and port multiplier | done | matrix rows `deferred` |
| 5 | Split Stage 5 into objects, VA, residency, submission, fences | done | `docs/03` §13 gates 5A–5E |
| 6 | No GPU support claim without real tests | done | matrix GPU row; `docs/03` §14 |

## 5. Decisions needed

| ID | Decision | Recommendation | Owner |
|---|---|---|---|
| D1 | A1 DMA API: Linux-like or minimal custom? | A1 signed constraint-driven `o0_dma_alloc`/`o0_dma_sync`/`o0_dma_free`; no domain handle in Stage 1. A3 reconciles `platform.h` to that surface | A1/A3 |
| D2 | AHCI/USB: native block drivers or a universal SCSI layer? | Native AHCI and NVMe block drivers; SCSI layer only for USB MSC/UAS | A1/A3 |
| D3 | Firmware baseline: UEFI-only or legacy BIOS/CSM? | UEFI 2.11 + ACPI 6.6 reference; Haswell-era compatibility; CSM out of scope | A1/A3 |
| D4 | Haswell test hardware: GT3e, GT2, or both? | Both: GT3e for eDRAM, GT2 for non-eDRAM | A4 |
| D5 | NVMe baseline: 1.4 or 2.4? | 1.4-compatible core; negotiate 2.4 features only when present | A3 |
| D6 | 4Kn and TRIM defaults? | Support 4Kn at block layer; advertise discard/write zeroes only when supported | A3 |

A1's review is signed with one IOMMU blocker and four conditions. A3 accepts
the conditions for trusted ring-0 Stage 1. The AHCI probe remains blocked until
`include/o0/platform.h` and `include/o0/block.h` are reconciled with A1's
`include/o0/mm.h`/`include/o0/driver.h` surface and the director accepts the
IOMMU risk.

## 6. Unresolved risks

- No QEMU or real hardware in this environment; all support claims are
  unverified.
- Haswell firmware may be older than ACPI 6.6/UEFI 2.11.
- QEMU NVMe is 1.4, so it cannot validate NVMe 2.4 features.
- QEMU has no faithful Haswell i915 model; GPU work needs real GT2/GT3e.
- eDRAM enable sequence is not implemented or validated.
- A1's no-IOMMU blocker: Stage 1 returns physical, untranslated DMA addresses;
  untrusted or userspace DMA is unsafe.
- A3 `platform.h` draft uses `o0_dma_domain`, `dma_map_sg`, and `iommu_map`;
  A1's signed surface has none of these. The AHCI probe must not start on the
  unreconciled draft.
- `block.h` still uses `o0_reset_kind`; A1's `o0_device_reset` has no kind.
- Archived PRM and specification links may rot.

## 7. Commit, branch, and files

Exact values at evidence-write time:

```
branch: a3/stage0-review-response
base commit on main: 147cc86c027cef7cec42525024d26dcccf66d7db
stage 0 package commit: a2890b5e3c070fb8a13fa64030a0a6e6a6e80714
a1 review source: origin/a1/stage0-review @ e2d7c0d
a1 review response commit: this commit (HEAD)
```

Get the exact commits with:

```sh
git rev-parse HEAD
git log --oneline --decorate -3
git log -1 --oneline -- EVIDENCE.md
```

A1 review response files:

```text
M  AGENTS.md
M  README.md
M  docs/04-driver-interface-draft.md
M  docs/06-stage0-review-response.md
M  include/o0/block.h
M  include/o0/platform.h
M  EVIDENCE.md
```

Files added or changed by the Stage 0 package:

```text
A  .gitignore
A  AGENTS.md
A  EVIDENCE.md
A  Makefile
A  Ozero_A3_Briefing.pdf
M  README.md
A  docs/01-hardware-support-matrix.md
A  docs/02-storage-architecture.md
A  docs/03-haswell-gen75-gpu.md
A  docs/04-driver-interface-draft.md
A  docs/05-hardware-test-plan.md
A  docs/06-stage0-review-response.md
A  include/o0/block.h
A  include/o0/platform.h
A  matrix/hardware-support-matrix.csv
A  tests/header_smoke.c
A  tests/test_matrix_semantics.py
A  tools/check_deliverables.py
A  tools/collect_inventory.sh
A  tools/validate_matrix.py
```

## 8. Not tested

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
- No director acceptance of A1's no-IOMMU blocker.
- No reconciled `platform.h`/`block.h` build against A1's `mm.h`/`driver.h`.

## 9. Next evidence to collect

1. L1 QEMU run: boot the test image on q35 with AHCI HDD, AHCI SSD, NVMe,
   USB BOT, and UAS devices; capture `lspci -nnvv`, `lsblk`, `dmesg`, and
   `fio` results.
2. L2 real hardware: Haswell GT2 and GT3e machines with a SATA HDD, SATA SSD,
   NVMe device, USB BOT stick, and UAS enclosure; capture the inventory from
   `tools/collect_inventory.sh`.
3. L3 error recovery: `blkdebug` fault injection in QEMU and real-device
   reset/hotplug tests.
4. GPU gates 5A–5E: objects, GGTT/PPGTT, residency/eviction,
   validation/submission, and completion fences.
5. Firmware: ACPI/UEFI table dump and `_CRS`/`_PRT`/`_OSC` decoding on the
   actual Haswell firmware.
