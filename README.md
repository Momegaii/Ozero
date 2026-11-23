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
| Storage architecture note | [`docs/02-storage-architecture.md`](docs/02-storage-architecture.md) | desk artifact | specification review; block API draft |
| Haswell Gen7.5 GPU note | [`docs/03-haswell-gen75-gpu.md`](docs/03-haswell-gen75-gpu.md) | desk artifact | i915 v6.6 source references; PRM references |
| Driver interface draft | [`docs/04-driver-interface-draft.md`](docs/04-driver-interface-draft.md), [`include/o0/platform.h`](include/o0/platform.h) | desk artifact | header smoke compile PASS |
| Hardware test plan | [`docs/05-hardware-test-plan.md`](docs/05-hardware-test-plan.md), [`tools/collect_inventory.sh`](tools/collect_inventory.sh) | desk artifact | shell syntax check PASS |
| Stage 0 review response | [`docs/06-stage0-review-response.md`](docs/06-stage0-review-response.md) | desk artifact | review action matrix; A1 review still open |

Supporting files:

- [`include/o0/block.h`](include/o0/block.h) — draft block I/O contract.
- [`tests/header_smoke.c`](tests/header_smoke.c) — compile-only header check.
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
sh -n tools/collect_inventory.sh
```

Expected results:

- matrix validator: PASS, 10 rows, required storage families present;
- deliverable check: PASS, all required sections and references present;
- matrix semantics tests: PASS, 13 tests;
- header smoke: PASS, no compiler diagnostics;
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
| A1 written review before AHCI probe | **open** — AHCI probe blocked | `docs/06` §2 |
| Pin spec revision, status, emulator, hardware, test per row | done in matrix schema v2 | `matrix/hardware-support-matrix.csv` |
| Separate media from transport; keep drivers distinct | done | `docs/01` §2–§3 |
| Defer ZNS and port multiplier | done | matrix rows `deferred` |
| Split Stage 5 into objects, VA, residency, submission, fences | done | `docs/03` §13 |
| No GPU support claim without real tests | done | matrix and `docs/03` §14 |

## Decisions needed

| ID | Decision | Recommendation | Owner |
|---|---|---|---|
| D1 | A1 DMA API: Linux-like or minimal custom? | Minimal custom API with `o0_dma_domain`; mapping may sleep, sync may not | A1 |
| D2 | AHCI/USB: native block drivers or a universal SCSI layer? | Native AHCI and NVMe block drivers; SCSI layer only for USB MSC/UAS | A1/A3 |
| D3 | Firmware baseline: UEFI-only or legacy BIOS/CSM? | UEFI 2.11 + ACPI 6.6 as reference, with compatibility for Haswell-era ACPI 5.x/UEFI 2.3–2.4; CSM out of scope | A1/A3 |
| D4 | Haswell test hardware: GT3e, GT2, or both? | Both: GT3e for eDRAM and GT2 for the non-eDRAM path | A4 |
| D5 | NVMe baseline: 1.4 or 2.4? | 1.4-compatible core; negotiate 2.4 features only when present; QEMU is 1.4 | A3 |
| D6 | 4Kn and TRIM defaults? | Support 4Kn at the block layer; advertise discard/write zeroes only when the device supports them | A3 |

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| No QEMU or real hardware in this environment | The plan is unexecuted; support claims are unverified | Run L1 in CI; acquire Haswell GT2/GT3e and storage devices for L2 |
| Haswell firmware is older than ACPI 6.6/UEFI 2.11 | Newer-table assumptions fail on real machines | Version negotiation and legacy `_PRT`/INTx fallback |
| i915 source line numbers change | Citations drift | Cite file and symbol names, not only line numbers; pin v6.6 in EVIDENCE |
| NVMe 2.4 vs QEMU 1.4 | QEMU cannot validate 2.4 features | 1.4-compatible core; real-device tests for 2.4 features |
| eDRAM enable sequence is incomplete | GT3e may run without eDRAM or hang | Treat eDRAM as a PRM-defined platform sequence; validate on GT3e |
| Block API is a draft | A1/A3 may diverge on queueing, DMA sleep, or reset semantics | A1 review of `platform.h` and `block.h` before implementation |
| PRM links are archived/retired | References may rot | Keep archived links and local citation notes; verify before publication |

## Next step

**A1 reviews and signs off [`include/o0/platform.h`](include/o0/platform.h) and
[`include/o0/block.h`](include/o0/block.h), specifically the DMA sleep rules,
reset/no-in-flight-DMA guarantee, address width, alignment, coherency, and
firmware-table lifetime.** That review unblocks the first AHCI probe
implementation. Until A1 signs, the AHCI probe remains blocked; A3 must not
guess at contracts that are expensive to change later.

## Repository layout

```
.
├── Ozero_A3_Briefing.pdf
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
│   └── 06-stage0-review-response.md
├── include/o0/
│   ├── platform.h
│   └── block.h
├── matrix/
│   └── hardware-support-matrix.csv
├── tests/
│   ├── header_smoke.c
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
- **Decisions needed:** D1–D6 above.
- **Risks:** top three are unexecuted tests, Haswell firmware age, and the
  draft block API.
- **Next step:** A1 review of `include/o0/platform.h`.
