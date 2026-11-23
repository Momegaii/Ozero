# Ozero (o0) A3 — Hardware Engineering, Stage 0

Repository purpose: Ozero is a new OS kernel plus Windows compatibility libraries and executables. This branch adds the A3 Stage 0 hardware-engineering package.

Status: **Stage 0 desk artifact; no implementation or hardware validation**

Briefing: `Ozero_A3_Briefing.pdf` (23 Nov 2026, Stage 0, 3 pages)
Scope: device model, storage, and Haswell Gen7.5

## What this is

This repository contains the Stage 0 hardware-engineering deliverables for
Ozero A3:

- a hardware support matrix with a specification and a test vehicle for every
  claimed device family;
- a storage architecture note with a device-independent block I/O API and
  protocol-specific driver boundaries;
- a Haswell Gen7.5 GPU research note that is explicitly separate from Gen8+;
- a driver interface draft for the platform contracts A3 needs from A1;
- a hardware test plan with QEMU and real-hardware procedures.

It is a design and verification package, not a driver implementation. The
only executable artifacts are the draft headers, the test/check scripts, the
matrix validator, the deliverable checker, and the read-only inventory script.

## Deliverables

| Deliverable | File | Status | Evidence |
|---|---|---|---|
| Hardware support matrix | [`docs/01-hardware-support-matrix.md`](docs/01-hardware-support-matrix.md), [`matrix/hardware-support-matrix.csv`](matrix/hardware-support-matrix.csv) | desk artifact | `tools/validate_matrix.py` PASS |
| Storage architecture note | [`docs/02-storage-architecture.md`](docs/02-storage-architecture.md) | desk artifact | specification review; block API reconciled to IF-008 v1.1 |
| Haswell Gen7.5 GPU note | [`docs/03-haswell-gen75-gpu.md`](docs/03-haswell-gen75-gpu.md) | desk artifact | i915 v6.6 source references; PRM references |
| Driver interface draft | [`docs/04-driver-interface-draft.md`](docs/04-driver-interface-draft.md), [`docs/interfaces/IF-008-dma-platform.md`](docs/interfaces/IF-008-dma-platform.md), [`include/ozero/platform.h`](include/ozero/platform.h) | desk artifact | header smoke PASS against A1 `30ee4f0` |
| Hardware test plan | [`docs/05-hardware-test-plan.md`](docs/05-hardware-test-plan.md), [`tools/collect_inventory.sh`](tools/collect_inventory.sh) | desk artifact | shell syntax check PASS |
| Stage 0 review response | [`docs/06-stage0-review-response.md`](docs/06-stage0-review-response.md) | desk artifact | review action matrix; IF-008 v1.1 published, A1 final approval pending |

Supporting files:

- [`docs/interfaces/IF-008-dma-platform.md`](docs/interfaces/IF-008-dma-platform.md)
  — reconciled A3 DMA/platform interface (v1.1, A1 final approval pending).
- [`include/ozero/block.h`](include/ozero/block.h) — block I/O contract.
- [`tests/header_smoke.c`](tests/header_smoke.c) — compile-only header check.
- [`tests/a1_header_smoke.c`](tests/a1_header_smoke.c) — fails unless A1 headers are used.
- [`tests/test_if008_contract.py`](tests/test_if008_contract.py) — IF-008
  contract guard.
- [`tests/test_matrix_semantics.py`](tests/test_matrix_semantics.py) — matrix
  semantics tests.
- [`tools/check_deliverables.py`](tools/check_deliverables.py) — docs and
  matrix completeness check.
- [`EVIDENCE.md`](EVIDENCE.md) — commands, outputs, commit hash, and source
  verification.

## Key positions

1. **Media, interface, protocol, and form factor are separate.** An SSD can
   be SATA, NVMe, or USB; an HDD can be SATA, USB, or legacy PATA. The driver
   binds to the controller and protocol, not to the marketing category.
2. **HDD and SSD are both first-class.** A SATA HDD and a SATA SSD share the
   AHCI driver; the block layer uses the rotational hint only as a scheduling
   input.
3. **NVMe is not a generic SSD driver.** NVMe binds to an NVMe controller and
   exposes namespaces. SATA SSDs go through AHCI; USB SSDs go through USB
   mass storage and SCSI.
4. **Haswell is Gen7.5, not Gen8.** Haswell uses aliasing PPGTT, a 31-bit
   (2 GB) PPGTT in i915, 32-bit relocation, legacy ring submission with
   `MI_SET_CONTEXT`, and 4 KB pages in i915. Gen8+ assumptions do not apply.
5. **GPU work starts with memory management, not command submission.** The
   Stage 5 gates are 5A GPU objects → 5B GPU VA mapping → 5C residency and
   eviction → 5D command validation and submission → 5E completion fences.
6. **The block layer is vendor-neutral; the drivers are not.** AHCI, NVMe,
   and USB keep their own command translation, queueing, error models, and
   reset paths.
7. **QEMU is a functional target, not hardware evidence.** QEMU NVMe is 1.4,
   QEMU AHCI is not timing-accurate, QEMU USB has no PHY, and QEMU has no
   faithful Haswell i915 model. There is no GPU support claim until real
   Haswell tests pass.

## Evidence

The detailed record, including the deliverables commit hash, is in
[`EVIDENCE.md`](EVIDENCE.md). The checks are reproducible from the repository
root:

```sh
make check
```

`make check` runs the individual checks:

```sh
python3 tools/validate_matrix.py
python3 tools/check_deliverables.py
python3 -m unittest discover -s tests -p 'test_*.py'
cc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only tests/header_smoke.c
make check-a1-headers A1_INCLUDE=/tmp/a1include
sh -n tools/collect_inventory.sh
```

Expected results:

- matrix validator: PASS, 10 rows, required storage families present;
- deliverable check: PASS, all required sections and references present;
- contract and matrix semantics tests: PASS, 19 tests;
- header smoke: PASS, no compiler diagnostics;
- A1-header smoke: PASS against `ozero/mm.h`, `ozero/driver.h` and `ozero/irq.h`;
- inventory script: PASS, no shell syntax errors.

Source verification performed for this draft:

- AHCI 1.3.1 specification (Intel; revision 1_3_1).
- ATA/ATAPI ACS-5 and Serial ATA Revision 3.5.
- NVMe Base 2.4 and NVMe over PCIe Transport 2.4, released 4 August 2026.
- NVMe ZNS Command Set 1.1 (deferred).
- USB 2.0, USB 3.2, USB MSC BOT 1.0, UAS 1.0, xHCI 1.2, and SCSI SBC-4.
- PCI Express Base 3.0 and Virtio 1.2.
- ACPI 6.6, May 2025.
- UEFI 2.11, December 2024.
- Linux i915 v6.6 source for Haswell feature tables, PPGTT, GGTT, eDRAM,
  relocation, and submission.
- QEMU documentation: NVMe emulation implements NVMe 1.4; xHCI and USB
  mass-storage device syntax.
- PCI ID database for QEMU and Haswell PCH IDs.

What was **not** run in this environment:

- QEMU is not installed here, so no L1 boot or I/O test was executed.
- No real Haswell hardware is attached, so no L2/L3/L4 GPU, eDRAM, SATA,
  NVMe, or USB test was executed.
- The test plan is ready to execute; its results are not claimed.

## Stage 0 review response

The 23 Nov 2026 review actions are tracked in
[`docs/06-stage0-review-response.md`](docs/06-stage0-review-response.md):

| Review action | Status | Artifact |
|---|---|---|
| A1 written review before AHCI probe | **received; A3 published IF-008 v1.1; A1 final approval pending** — AHCI probe blocked | A1 `a1/stage0-review` @ `30ee4f0`; `docs/a3-dma-contract.md`; `docs/interfaces/IF-008-dma-platform.md`; `docs/06` §2 |
| Pin spec revision, status, emulator, hardware, test per row | done in matrix schema v2 | `matrix/hardware-support-matrix.csv` |
| Separate media from transport; keep drivers distinct | done | `docs/01` §2–§3 |
| Defer ZNS and port multiplier | done | matrix rows `deferred` |
| Split Stage 5 into objects, VA, residency, submission, fences | done | `docs/03` §13 |
| No GPU support claim without real tests | done | matrix and `docs/03` §14 |

## Decisions and dependencies

| ID | Decision | Recommendation | Owner |
|---|---|---|---|
| D-001 | A1/A2/A3 domain split and GitHub team slugs | Keep the split; A3 owns storage/GPU device protocols | A4 |
| D-002 | Branch isolation and review gate; protect `main` | Protect `main`; merge through PRs only | A4 |
| D-005 | Boot protocol and kernel handoff | Limine is a candidate only; A3 consumes A1's boot/platform data | A1 |
| D-006 | Accept Stage 0 | Not accepted; this package is desk evidence only | A4 |
| D-007 | Approve IF-008 v1.1 and the A1 sign-off gate | A3 published v1.1; A1 final approval and director approval pending | A1 + A3 |
| D-009 | Public header namespace | **Approved** in A4 `245007d`: canonical root is `include/ozero/` only; A1 `03f8883`; A3 is aligned; XB-007 is closed | A1 + A4 |
| D-010 | `tools/check-headers.sh` ownership | **Approved** in A4 `245007d`: A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`; A4 owns CI; XB-008 is closed | A1 + A4 |
| D-020 | DMA is constraint-driven, never device-class-driven | A3 supplies `o0_dma_constraint`; A1 does not infer it | A1 |
| D-021 | IOMMU policy: trusted ring-0 only in Stage 1 | No DMA domain, no IOVA and no translated DMA type; director approval pending | A1 |

A3 implementation choices (native AHCI/NVMe, NVMe 1.4 core, 4Kn/TRIM) are
recorded in `docs/02` and `docs/05`; they are not global decisions until the
director adds them to A4's register.

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| No QEMU or real hardware in this environment | The plan is unexecuted; support claims are unverified | Run L1 in CI; acquire Haswell GT2/GT3e and storage devices for L2 |
| Haswell firmware is older than ACPI 6.6/UEFI 2.11 | Newer-table assumptions fail on real machines | Version negotiation and legacy `_PRT`/INTx fallback |
| i915 source line numbers change | Citations drift | Cite file and symbol names, not only line numbers; pin v6.6 in EVIDENCE |
| NVMe 2.4 vs QEMU 1.4 | QEMU cannot validate 2.4 features | 1.4-compatible core; real-device tests for 2.4 features |
| eDRAM enable sequence is incomplete | GT3e may run without eDRAM or hang | Treat eDRAM as a PRM-defined platform sequence; validate on GT3e |
| Block API is published but not finally approved | A1/A3 may diverge on queueing, DMA or reset semantics | IF-008 v1.1 aligned to A1 `30ee4f0`; A1 final approval pending |
| No IOMMU in Stage 1 | A buggy or malicious driver can DMA anywhere; untrusted/userspace DMA is unsafe | Trusted ring-0 drivers only; escalate before untrusted DMA |
| A1 has not finally approved IF-008 v1.1 | The AHCI probe could still target an unapproved contract | A1 must approve this exact version before any AHCI probe |
| Remote branch is not yet reviewed | A4 audit and CI have not run on the pushed branch | Open a PR or request A4 audit; XB-005 remains open |
| A4 blocker rows lag A3 publication | XB-001/005/011 still describe A3 as unpublished | A4 refresh from A3 `b9a69d8`; D-009 and D-010 are already approved |
| Vendored A1 headers can drift | A3's copy of A1 `include/ozero/*.h` may age | Re-sync from `a1/stage0-review` before final review; `make check-a1-headers` compares against an external A1 tree |
| PRM links are archived/retired | References may rot | Keep archived links and local citation notes; verify before publication |

## Next step

**A1 must give final approval of
[`docs/interfaces/IF-008-dma-platform.md`](docs/interfaces/IF-008-dma-platform.md)
v1.1.** A3 has aligned `platform.h` and `block.h` to A1 `30ee4f0`: direct
inclusion of `include/ozero/*.h`, `struct o0_device_ops`, one contiguous buffer
per `o0_io_request`, no SG pointer, and reset that does not free buffers or
unmap BARs. The director must approve D-007 and D-021 and accept the no-IOMMU
Stage 1 limitation. D-009 and D-010 are already approved in A4 `245007d`:
`include/ozero/` is the only public root, and A1 owns the header checker. The
AHCI probe remains blocked until A1 and the director record approval.

## Repository layout

```
.
├── Ozero_A3_Briefing.pdf
├── Ozero_A3_Action_Brief_2026-11-23_v2.pdf
├── README.md
├── EVIDENCE.md
├── Makefile
├── .gitignore
├── AGENTS.md
├── docs/
│   ├── 01-hardware-support-matrix.md
│   ├── 02-storage-architecture.md
│   ├── 03-haswell-gen75-gpu.md
│   ├── 04-driver-interface-draft.md
│   ├── 05-hardware-test-plan.md
│   ├── 06-stage0-review-response.md
│   ├── 07-a3-action-brief-response.md
│   ├── decisions/
│   │   └── a3-decision-requests.md
│   └── interfaces/
│       ├── IF-008-dma-platform.md
│       ├── IF-008-v1.1-a1-review-packet.md
│       └── IF-009-a3-review.md
├── include/ozero/
│   ├── platform.h        # A3
│   ├── block.h           # A3
│   ├── mm.h              # A1 snapshot @ 30ee4f0
│   ├── driver.h          # A1 snapshot @ 30ee4f0
│   ├── irq.h             # A1 snapshot @ 30ee4f0
│   ├── status.h          # A1 snapshot @ 30ee4f0
│   ├── types.h           # A1 snapshot @ 30ee4f0
│   └── ...               # other A1 public headers
├── matrix/
│   └── hardware-support-matrix.csv
├── tests/
│   ├── a1_header_smoke.c
│   ├── header_smoke.c
│   ├── test_if008_contract.py
│   └── test_matrix_semantics.py
└── tools/
    ├── validate_matrix.py
    ├── check_deliverables.py
    └── collect_inventory.sh
```

## Reporting format

- **Status:** Stage 0 desk artifact; no implementation or hardware validation.
- **Evidence:** `EVIDENCE.md` (commit hash, commands, outputs, source
  versions).
- **Decisions needed:** D-001, D-002, D-005, D-006, D-007, D-020 and D-021 above.
- **Risks:** top three are unexecuted tests, Haswell firmware age, and the
  draft block API.
- **Next step:** obtain A1 final approval of IF-008 v1.1 and director approval of D-007/D-021, then start the AHCI probe. D-009 and D-010 are already resolved in A4 `245007d`.
