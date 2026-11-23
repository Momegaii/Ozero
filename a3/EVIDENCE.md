# Evidence — Ozero A3 Stage 0

Status: **Stage 0 desk artifact; no implementation or hardware validation**
Review date: 2026-11-23 (UTC)
Base commit on main: `147cc86c027cef7cec42525024d26dcccf66d7db`
Branch: `a3/stage0-review-response`
Stage 0 package commit: `a2890b5e3c070fb8a13fa64030a0a6e6a6e80714`
A1 review source: `origin/a1/stage0-review` @ `30ee4f0`
A1 IF-008 decision: **ACCEPT** at `b9a69d8`; A1 commit `4716e09`
A4 register basis: `origin/a4/stage0-review-followup` @ `3ae7aca`
A3 action brief reviewed: `Ozero_A3_Action_Brief_2026-11-23_v2.pdf`
A3 proceed brief reviewed: `Ozero_A3_Proceed_Brief_2026-11-23.pdf`
A1 review response commit: this commit (HEAD)
IF-008 version: 1.1
IF-008 reconciliation commit: this commit (HEAD); A1 header snapshot: `30ee4f0`
Remote branch: `origin/a3/stage0-review-response` (pushed 2026-11-23)

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
  files: 24
  documents checked: 7
  matrix validator: PASS
python3 -m unittest discover -s tests -p 'test_*.py'
...................
----------------------------------------------------------------------
Ran 19 tests in 0.005s

OK
sh -n tools/collect_inventory.sh
```

Individual commands:

| Check | Command | Result |
|---|---|---|
| Matrix structure | `python3 tools/validate_matrix.py` | PASS, 10 rows, 9 driver families |
| Deliverable completeness | `python3 tools/check_deliverables.py` | PASS, 24 files, 7 docs |
| Matrix semantics | `python3 -m unittest discover -s tests -p 'test_*.py'` | PASS, 19 tests |
| Header compile, vendored A1 headers | `cc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only tests/header_smoke.c` | PASS |
| Header compile, A1 headers | `make check-a1-headers A1_INCLUDE=/tmp/a1include` | PASS |
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
| 1 | A1 written review before AHCI probe | **received; IF-008 v1.1 published; A1 ACCEPT at `b9a69d8`; Director D-007 pending — AHCI probe blocked** | `origin/a1/stage0-review` @ `30ee4f0`, `docs/interface-draft.md` §11 and `docs/a3-dma-contract.md`; `docs/interfaces/IF-008-dma-platform.md` |
| 2 | Pin spec revision, status, emulator, hardware, test per row | done | matrix schema v2 |
| 3 | Separate media from transport; keep drivers distinct | done | `docs/01` §2–§3 |
| 4 | Defer ZNS and port multiplier | done | matrix rows `deferred` |
| 5 | Split Stage 5 into objects, VA, residency, submission, fences | done | `docs/03` §13 gates 5A–5E |
| 6 | No GPU support claim without real tests | done | matrix GPU row; `docs/03` §14 |

## 5. Decisions needed

| ID | Decision | Recommendation | Owner |
|---|---|---|---|
| D-001 | A1/A2/A3 domain split and GitHub team slugs | Keep the split; A3 owns storage/GPU device protocols | A4 |
| D-002 | Branch isolation and review gate; protect `main` | Protect `main`; merge through PRs only | A4 |
| D-005 | Boot protocol and kernel handoff | Limine is a candidate only; A3 consumes A1's boot/platform data | A1 |
| D-006 | Accept Stage 0 | Not accepted; this package is desk evidence only | A4 |
| D-007 | Approve IF-008 v1.1 and the A1 sign-off gate | A3 published v1.1; A1 ACCEPT at `b9a69d8`; Director D-007 pending | A1 + A3 |
| D-009 | Public header namespace | **Approved** in A4 `3ae7aca`: canonical root is `include/ozero/` only; A1 `03f8883`; A3 aligned; XB-007 closed | A1 + A4 |
| D-010 | `tools/check-headers.sh` ownership | **Approved** in A4 `3ae7aca`: A1 owns checker and static asserts; A4 owns CI; XB-008 closed | A1 + A4 |
| D-020 | DMA is constraint-driven, never device-class-driven | A3 supplies `o0_dma_constraint`; A1 does not infer it | A1 |
| D-021 | IOMMU policy: trusted ring-0 only in Stage 1 | No DMA domain, no IOVA and no translated DMA type; director approval pending | A1 |

A1's review is signed with one IOMMU blocker and five conditions. A3 accepts
the conditions for trusted ring-0 Stage 1 and has published IF-008 v1.1.
`include/ozero/platform.h` includes A1's signed `include/ozero/*.h` directly;
`block.h` embeds one `o0_io_request`, has no SG pointer, and splits large
transfers into multiple buffers and requests. A3 implements
`struct o0_device_ops`; reset does not free buffers or unmap BARs. The AHCI
probe remains blocked until the Director approves D-007 and D-021. A1 accepted
IF-008 v1.1 at `b9a69d8` on 23 November 2026.

## 6. Unresolved risks

- No QEMU or real hardware in this environment; all support claims are
  unverified.
- Haswell firmware may be older than ACPI 6.6/UEFI 2.11.
- QEMU NVMe is 1.4, so it cannot validate NVMe 2.4 features.
- QEMU has no faithful Haswell i915 model; GPU work needs real GT2/GT3e.
- eDRAM enable sequence is not implemented or validated.
- A1's no-IOMMU blocker: Stage 1 returns physical, untranslated DMA addresses;
  untrusted or userspace DMA is unsafe.
- A1 accepted IF-008 v1.1 at `b9a69d8` (`4716e09`). A1's conditions are
  accepted in `docs/interfaces/IF-008-v1.1-acceptance.md`; Director approval is
  still required.
- Director approval of D-007 and D-021 is pending.
- D-009 and D-010 are approved in A4 `3ae7aca`: `include/ozero/` is the only
  public root and A1 owns the header checker. XB-007 and XB-008 are closed.
- A4's XB-001 and XB-005 evidence still describes A3 as unpublished; XB-011 is
  closed but cites `15cd9ce`. A4 should refresh from the current A3 branch head.
- The branch is pushed, but no pull request is open; A4's audit does not cover
  the current head, so XB-005 remains open.
- Archived PRM and specification links may rot.

## 7. Commit, branch, and files

Exact values at evidence-write time:

```
branch: a3/stage0-review-response
base commit on main: 147cc86c027cef7cec42525024d26dcccf66d7db
stage 0 package commit: a2890b5e3c070fb8a13fa64030a0a6e6a6e80714
a1 review source: origin/a1/stage0-review @ 30ee4f0
a1 review response commit: 897e5ce7afcc055572a735e7c0ccc6d62d42d8cb
if-008 version: 1.0
```

Get the exact commits with:

```sh
git rev-parse HEAD
git log --oneline --decorate -3
git log -1 --oneline -- EVIDENCE.md
```

Remote push status (2026-11-23): **pushed**. `git push -u origin
a3/stage0-review-response` created `origin/a3/stage0-review-response` and set
the local branch to track it. No pull request was created. The local patch
remains at `/workspace/a3-stage0-review-response.patch`.

A1 review response files:

```text
M  AGENTS.md
M  Makefile
M  README.md
M  docs/02-storage-architecture.md
M  docs/04-driver-interface-draft.md
M  docs/05-hardware-test-plan.md
M  docs/06-stage0-review-response.md
A  docs/07-a3-action-brief-response.md
A  docs/08-control-plane-integration.md
A  docs/09-proceed-brief-response.md
A  docs/decisions/a3-decision-requests.md
A  docs/interfaces/IF-008-dma-platform.md
A  docs/interfaces/IF-008-v1.1-acceptance.md
A  docs/interfaces/IF-008-v1.1-a1-review-packet.md
A  docs/interfaces/IF-009-a3-review.md
M  include/ozero/block.h
M  include/ozero/platform.h
A  include/ozero/boot.h
A  include/ozero/cpu.h
A  include/ozero/driver.h
A  include/ozero/ipc.h
A  include/ozero/irq.h
A  include/ozero/mm.h
A  include/ozero/o0.h
A  include/ozero/status.h
A  include/ozero/thread.h
A  include/ozero/types.h
M  tests/header_smoke.c
A  tests/a1_header_smoke.c
A  tests/test_if008_contract.py
M  tools/check_deliverables.py
M  EVIDENCE.md
```

Files added or changed by the Stage 0 package:

```text
A  .gitignore
A  AGENTS.md
A  EVIDENCE.md
A  Makefile
A  Ozero_A3_Briefing.pdf
A  Ozero_A3_Action_Brief_2026-11-23_v2.pdf
A  Ozero_A3_Proceed_Brief_2026-11-23.pdf
M  README.md
A  docs/01-hardware-support-matrix.md
A  docs/02-storage-architecture.md
A  docs/03-haswell-gen75-gpu.md
A  docs/04-driver-interface-draft.md
A  docs/05-hardware-test-plan.md
A  docs/06-stage0-review-response.md
A  include/ozero/block.h
A  include/ozero/platform.h
A  matrix/hardware-support-matrix.csv
A  tests/header_smoke.c
A  tests/test_matrix_semantics.py
A  tools/check_deliverables.py
A  tools/collect_inventory.sh
A  tools/validate_matrix.py
```

### 7.1 IF-008 reconciliation checks

```sh
cc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only tests/header_smoke.c
# PASS: A1 snapshot in include/ozero

make check-a1-headers A1_INCLUDE=/tmp/a1include
# PASS: external A1 30ee4f0 mm.h/driver.h/irq.h; a1_header_smoke.c fails if they are absent

python3 tools/check_deliverables.py
# PASS: files: 24, documents checked: 7
```

The tree commits a snapshot of A1's `include/ozero/*.h` at `30ee4f0` so the
branch is self-contained. The external check copies A1's headers from
`origin/a1/stage0-review` into `/tmp/a1include/ozero/` and puts that path first,
so it verifies the A3 headers against A1's current tree rather than the
snapshot.

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
- No Director D-007/D-021 approval and no acceptance of the no-IOMMU Stage 1
  limitation. A1 accepted IF-008 v1.1 at `b9a69d8`.
- No AHCI probe work; it remains blocked until the interface is approved.

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
