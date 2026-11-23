# AGENTS.md — Ozero A3 Stage 0

## Project purpose

Stage 0 hardware-engineering package for Ozero (o0) A3: device model, storage,
and Intel Haswell Gen7.5. The repository is documentation-first; there is no
driver implementation yet.

## Commands

```sh
make check
```

This runs the matrix validator, deliverable checker, 13 matrix semantics
tests, the header compile, and the inventory shell syntax check.

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

- Deliverables: `docs/01`–`docs/06`, `include/o0/*.h`, `matrix/*.csv`,
  `tools/*`.
- Static checks pass: matrix validation, deliverable check, 13 semantics
  tests, header compile, inventory syntax.
- Matrix schema v2 records pinned spec revision, implementation status,
  emulator, real hardware, and acceptance test per row.
- All rows are `not-started` or `deferred`; no row is `complete`.
- No QEMU or real hardware was available in the authoring environment; L1–L4
  tests are planned but not executed.
- A1 review received on `a1/stage0-review` @ `e2d7c0d`: signed with an IOMMU
  blocker and four conditions. A3 accepts the conditions for trusted ring-0
  Stage 1.
- Next decision: reconcile `include/o0/platform.h` and `include/o0/block.h`
  with A1's signed `include/o0/mm.h`/`include/o0/driver.h` surface. The AHCI
  probe is blocked until that reconciliation and director acceptance of the
  IOMMU risk.
