# AGENTS.md — Ozero A3 Stage 0

This file is the A3 package memory under `a3/`. The repository root is the A4
control plane and has its own `AGENTS.md`.

## Project purpose

Stage 0 hardware-engineering package for Ozero (o0) A3: device model, storage,
and Intel Haswell Gen7.5. The repository is documentation-first; there is no
driver implementation yet.

## Commands

```sh
make check
```

This runs the matrix validator, deliverable checker, 19 contract and matrix semantics
tests, the header compile, and the inventory shell syntax check.

The tree vendors a snapshot of A1's public headers under `include/ozero/`.
To compile against an external A1 tree instead:

```sh
make check-a1-headers A1_INCLUDE=/path/to/a1/include
```

The directory must contain `ozero/mm.h`, `ozero/driver.h` and `ozero/irq.h`.
The target puts `A1_INCLUDE` first so the external headers win.

`tools/collect_inventory.sh` is read-only. It prints an inventory to stdout;
redirect it to a file. Some sections need root and use `sudo -n` when
available.

## Conventions

- Keep media, interface, protocol, and form factor separate. Never write
  "NVMe SSD" as if NVMe were a media type.
- HDD and SSD are both first-class. Bind to the controller/protocol, not to
  the marketing category.
- NVMe is not a generic SSD driver. SATA SSDs use AHCI; USB SSDs use USB mass
  storage and SCSI.
- Haswell is Gen7.5. Do not transfer Gen8+ assumptions: no full 48-bit PPGTT,
  no 64-bit relocation, no execlists, no 4-level page walk.
- eDRAM is a cache/platform enable sequence, not VRAM.
- GPU work starts with GGTT/PPGTT and residency, not command submission.
- Every matrix row needs a pinned specification revision, implementation
  status, emulator, real hardware, and acceptance test.
- `driver_family` is the driver boundary. HDD/SATA and SSD/SATA share `ahci`;
  NVMe, USB BOT, USB UAS, and legacy ATA are distinct.
- ZNS and SATA port multiplier are deferred; do not add them to the Stage 0
  claim without a concrete requirement.
- Stage 5 is split into 5A objects, 5B VA mapping, 5C residency/eviction,
  5D command validation/submission, and 5E completion fences.
- No GPU support claim until real Haswell tests pass; QEMU is not a Haswell
  i915 emulator.
- Cite primary specifications and i915 source by file/symbol, not only by
  line number.

## Current state

- Deliverables: `docs/01`–`docs/10`, `docs/decisions/`, `docs/interfaces/`,
  `include/ozero/*.h` (A3 `platform.h` and `block.h`, plus an A1 public-header
  snapshot at `30ee4f0`), `matrix/*.csv`, `tools/*`.
- Static checks pass: matrix validation, deliverable check, 19 semantics
  tests, header compile, inventory syntax.
- Matrix schema v2 records pinned spec revision, implementation status,
  emulator, real hardware, and acceptance test per row.
- All rows are `not-started` or `deferred`; no row is `complete`.
- No QEMU or real hardware was available in the authoring environment; L1–L4
  tests are planned but not executed.
- A1 review received on `a1/stage0-review` @ `74b1471` (headers `30ee4f0`):
  signed with an IOMMU blocker and five conditions; A1 recorded **ACCEPT** of
  IF-008 v1.1 at `b9a69d8` in decision `85df005`. A3 accepts the conditions for
  trusted ring-0 Stage 1.
- A3 published `docs/interfaces/IF-008-dma-platform.md` v1.1. `platform.h`
  includes A1's `<ozero/*.h>` directly and does not redeclare A1 types.
  `block.h` embeds one `o0_io_request`, has no SG pointer, and splits large
  transfers into multiple buffers and requests. A3 implements
  `struct o0_device_ops`; reset does not free buffers or unmap BARs.
- D-009 and D-010 are approved in A4 `b4232a0`: `include/ozero/` is the only
  public root, A1 owns the header checker, and XB-007/XB-008 are closed.
- A1 review packet: `docs/interfaces/IF-008-v1.1-a1-review-packet.md`; A1
  recorded ACCEPT at `b9a69d8` in `85df005` (review head `74b1471`). A3 acceptance is in
  `docs/interfaces/IF-008-v1.1-acceptance.md`.
- Decision wording for D-007 and D-021: `docs/decisions/a3-decision-requests.md`.
- IF-009 hardware-boundary review: `docs/interfaces/IF-009-a3-review.md`; A3
  accepts IF-009 v1.0 at A2 `d02598a` with no hardware-type leakage. A1 recorded
  **ACCEPT (A1 side) with conditions** at `a17d126`; A2 signature recording
  remains open at `6d305b8` / `64b3458`.
- Stage 0 is **NOT ACCEPTED**; D-006, D-007, D-021 and D-031 remain Proposed.
  A1 accepted IF-008 v1.1 at `b9a69d8`; the AHCI probe stays blocked until the
  Director records D-007 and D-021. The A3 package is under `a3/`; the A4
  control plane owns the shared root. A4 candidate `b4232a0` does not contain
  `a3/` or root `include/ozero/`; PR #4 is open into the A4 candidate. A3 is a
  separate branch until A4 merges it.
