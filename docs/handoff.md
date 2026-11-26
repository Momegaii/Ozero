# Ozero handoff — current state

Single source of truth for "where are we now". Every claim here cites a commit on
`main`; `main` is the only branch of record.

## Wave 3 scheduler fix — reschedule kick on remote enqueue

**Status:** IMPLEMENTED — awaiting CI on `fix/wave3-smp-resched-kick`.

- **Defect.** `o0_sched_enqueue` pushed a thread onto a remote CPU's run queue
  without asking that CPU to look at it. A CPU halted in its idle loop only noticed
  the new thread at its next timer tick, and a CPU with no tick pending never
  noticed it. The wave3 stress test moves two workers between both CPUs once per
  iteration, so a worker could be left ready on a halted CPU; the main thread then
  blocked in `o0_thread_join` for the rest of the run.
- **Evidence.** `storage (virtio, smp=2)` in run 45805993581 and
  `boot (uefi, max, smp=2)` in run 45822308326 both stop right after
  `OZERO: smp user churn OK processes=16`, the statement before `smp_stop = 1` and
  the joins. The second cell attaches no virtio device, so the fault is not in the
  storage work. A worker that is still running sees `smp_stop` within microseconds
  and exits, so the worker that never returns is not running at all: it is ready
  and never scheduled.
- **Fix.** `kick_cpu()` sets the target's `resched_pending` and sends
  `O0_LAPIC_IPI_VECTOR`, the same mechanism the migration path already used. It is
  skipped for the current CPU, for offline CPUs and before the scheduler starts.
  No test semantics changed: the stress test and its markers are untouched.
- **Tests.** `tests/sched_test.c` adds `remote enqueue requests a reschedule on the
  target CPU` (one IPI, correct APIC id and vector, pending flag on the target
  only, thread queued on the target) and `enqueue for an offline CPU sends no IPI`.
  Both fail against the previous code, which sent nothing.
- **CI.** Pending. The failure is intermittent (two of the last three full runs), so
  a single green matrix run is weak evidence and the run is repeated.

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
| Wave 4.2–4.3 PCI storage (virtio-blk, AHCI, NVMe) | implemented, build-verified only | `make kernel` + host encoder test; **no run observed** |
| Stage 5 GPU software foundation | closed as software milestone; physical GPU deferred | host tests, QEMU PCI discovery markers |
| Stage 6.1–6.8 graphics/desktop | implemented, host-tested, wired into `kmain` | host tests; **no desktop QEMU acceptance claimed** |
| Hardening Wave 1–3, 5.0, Q | implemented; Wave 3 signed off 2026-11-25 | local QEMU plus CI matrix (run `45697656276` on `e066168`) |

## Wave 3 sign-off (2026-11-25)

Wave 3 (SMP and memory hardening) is signed off on `main` at `e066168`.
**Director sign-off confirmed 2026-11-25**: the evidence below is the CI run the Director
accepted, and the two open items (BSP-pinned user threads, Ring 3/SMAP untested on other
CPUs) are explicitly *not* covered by it.

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

## Wave 4.1 — PCI, MSI/MSI-X and DMA foundations (2026-11-25)

- **Commit:** `24b8d23` on `main`.
- **CI:** [run 45708532478](https://github.com/Momegaii/Ozero/actions/runs/45708532478) —
  `completed` / `success`, 14/14 jobs (12 boot cells, `lock-order-boot`,
  `host-sanitizers`). The docs commit `bbfbc25` is green in
  [run 45708413626](https://github.com/Momegaii/Ozero/actions/runs/45708413626), 14/14.
- **Evidence type:** CI, from job conclusions. Job logs need repository admin (`403`) and
  were not inspected here; the boot cells fail on any missing marker because
  `tools/qemu-smoke.sh` now requires `OZERO: pci caps msi=`.
- **Scope:** capability-list parsing from one config-space snapshot, BAR size from the
  all-ones mask, MSI/MSI-X programming and LAPIC vector dispatch, the DMA allocator with
  pinning, and host tests for both. No device driver consumes any of it yet.
- **Not verified:** no storage device, no physical hardware, no MSI-X interrupt from a
  real device. Wave 4.2 is not started.

## Wave 4.2–4.3 — PCI storage drivers (2026-11-26)

- **Commits:** virtio-blk in `96a0b11`; AHCI, NVMe, the storage cells and the
  virtio-blk BAR-probe correction in `775e333`, both on `main`.
- **Implemented:** three drivers behind `o0_block_register` with one queue of depth
  1 — legacy virtio PCI (`virtio_blk.c`), one AHCI port with command list, FIS,
  PRDT and Identify Device (`ahci.c`), and NVMe with one admin plus one I/O queue
  pair and MSI-X entries (`nvme.c`). Each has an injected past-end-of-medium read
  proof. Completion in all three is a bounded poll; interrupts are counted but are
  not load-bearing, and that is the documented claim.
- **CI:** `storage-boot` now runs three cells, `(virtio|ahci|nvme, smp=2)`, each
  requiring only its own driver's read/write and error-path markers.
- **Evidence type:** build only. `make kernel` passes with `-Werror` and the new
  `tests/storage_encode_test.c` passes in this workspace. No QEMU run was made
  here, no run on `775e333` has been read, and no log from the `ci-evidence`
  branch has been inspected. **No storage cell is claimed green.**
- **Not verified:** physical controllers (never claimed), NCQ, multiple queues,
  asynchronous submission, and any performance statement.
- **Correction:** `o0_pci_init` never probes BARs, so the Wave 4.2 virtio probe could
  not have found its device. `775e333` probes BAR0 in the probe path; the Wave 4.2
  cell is still awaiting its first observed run.

## Open items

- CI was inspected on the Wave 4.0 HEAD `3b48810`: 14/14 jobs `success` in run
  `45700432451`. No CI-built ISO artifact has been published.
- User threads remain BSP-pinned until per-CPU TSS/IST exists. [ADR 0015](decisions/0015-per-cpu-tss-ist.md)
  is the design note for that work; it is a blocking dependency before Stage 6 uses SMP
  beyond the BSP. Ring 3 and SMAP on non-boot CPUs are untested.
- No physical-hardware evidence exists for any wave.
- Stage 7–10 not started. Wave 4.0 is closed: ADRs 0012–0014 were approved by the
  Director on 2026-11-25 and the O0FS format is still a proposal. Wave 4.1 is
  implemented and CI-green on `24b8d23` (run `45708532478`). Wave 4.2 and 4.3 are
  implemented and build-verified on `775e333`; their three storage cells have not
  been observed green, so they are not evidence until a run is recorded here.

## Rules that stay in force

- Evidence claims cite a commit on `main`.
- Host, QEMU/CI, Director-observed and physical evidence are reported separately.
- Anything unverifiable stays refused or quarantined, never assumed.
