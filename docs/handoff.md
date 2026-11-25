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
| Hardening Wave 1–3, 5.0, Q | implemented; Wave 3 signed off 2026-11-25 | local QEMU plus CI matrix (run `45697656276` on `e066168`) |

## Wave 3 sign-off (2026-11-25)

Wave 3 (SMP and memory hardening) is signed off on `main` at `e066168`.

- **CI:** [run 45697656276](https://github.com/Momegaii/Ozero/actions/runs/45697656276)
  on `e066168` — `completed` / `success`, 14/14 jobs: 12 boot cells
  (`{bios, uefi} × {qemu64, Haswell-v1, max} × -smp {1, 2}`), `lock-order-boot`
  (`make LOCK_ORDER=1`, `-smp 2`) and `host-sanitizers`.
- **Gate:** every Wave 3 condition is present in that run: `-smp 2` cells, `-cpu max`
  cells (SMEP/SMAP) and a lock-order boot cell, all on the `main` HEAD commit.
- **Evidence type:** CI. Not Director-observed, not physical.
- **Check limit:** this workspace read the run and job conclusions from the Actions API
  but could not download the job logs (`403`, repository admin required). Marker
  assertions therefore rest on each boot job's exit status through
  `tools/qemu-smoke.sh`, which fails on any missing marker — not on a line-by-line log
  inspection. Re-read the logs in the Actions UI if a marker-level record is required.
- Host, local QEMU and analyzer evidence for Wave 3 is recorded in
  `docs/PROJECT_STATUS.md` and `docs/hardening-roadmap.md`.

## Open items

- Wave 3 sign-off is recorded below: run `45697656276` on `e066168` is green for the 12
  boot cells (`{bios, uefi} × {qemu64, Haswell-v1, max} × smp {1, 2}`), the
  `lock-order-boot` cell (`make LOCK_ORDER=1`, `-smp 2`) and `host-sanitizers`.
- CI status for the current `main` HEAD has **not** been inspected by an agent. No CI run
  URL is recorded here yet, and no CI-built ISO artifact has been published.
- User threads remain BSP-pinned until per-CPU TSS/IST exists.
- No physical-hardware evidence exists for any wave.
- Stage 7–10 not started. Wave 4.0 (ADRs 0012–0014) is open; no Wave 4 code has started.

## Rules that stay in force

- Evidence claims cite a commit on `main`.
- Host, QEMU/CI, Director-observed and physical evidence are reported separately.
- Anything unverifiable stays refused or quarantined, never assumed.
