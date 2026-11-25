# Ozero handoff — current state

Single source of truth for "where are we now". Every claim here cites a commit on
`main`; `main` is the only branch of record.

## Line of record

- Branch: `main`
- Stage 6.4–6.8 boot integration merged: `7e26d26` (`feat(kernel): initialize shell and
  compositor in kmain`), followed by the ordering fix that keeps the desktop loop after
  the boot self-tests so `tools/qemu-smoke.sh` still sees its milestones.
- Earlier line of record: `5636e83` (`Connect input → window manager (Phase 6.8)`).

## What is in `main` and how it is evidenced

| Area | State | Evidence type |
|---|---|---|
| Stage 1 boot path | implemented | QEMU BIOS + UEFI/OVMF markers in CI |
| Stage 2 memory | implemented | build, host tests, QEMU boot |
| Stage 3 processes/threads/scheduler | implemented | host tests, QEMU BIOS boot |
| Stage 4 storage (RAM disk, O0FS draft) | implemented at substrate level | host tests, QEMU smoke |
| Stage 5 GPU software foundation | closed as software milestone; physical GPU deferred | host tests, QEMU PCI discovery markers |
| Stage 6.1–6.8 graphics/desktop | implemented, host-tested, wired into `kmain` | host tests; **no desktop QEMU acceptance claimed** |
| Hardening Wave 1–3, 5.0, Q | implemented | local QEMU plus CI matrix; Wave 3 not signed off |

## Open items

- Wave 3 sign-off is outstanding: SMP CI cells (`-smp 2`, `-cpu max`, `LOCK_ORDER=1` boot
  cell) must be green on a `main` commit and the Director must sign off. The workflow
  defines those cells (`.github/workflows/quality.yml`); the boot matrix is
  `firmware × {qemu64, Haswell-v1, max} × smp {1, 2}`, and `lock-order-boot` builds with
  `make LOCK_ORDER=1` at `-smp 2`.
- CI status for the current `main` HEAD has **not** been inspected by an agent. No CI run
  URL is recorded here yet, and no CI-built ISO artifact has been published.
- User threads remain BSP-pinned until per-CPU TSS/IST exists.
- No physical-hardware evidence exists for any wave.
- Stage 7–10 not started. Wave 4 is gated (see `docs/wave4.md`).

## Rules that stay in force

- Evidence claims cite a commit on `main`.
- Host, QEMU/CI, Director-observed and physical evidence are reported separately.
- Anything unverifiable stays refused or quarantined, never assumed.
