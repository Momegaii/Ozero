# Ozero handoff — current state

Single source of truth for "where are we now". Every claim here cites a commit on
`main`; `main` is the only branch of record.

## Wave 4.3 AHCI — cell green (emulator only)

**Status:** CI VERIFIED — NOT MERGED. Branch `feat/wave4.3-ahci-cell`, PR #10.

- The `storage (ahci, smp=2)` cell attaches
  `-drive id=ad0,file=build/storage.img,if=none,format=raw -device ide-hd,drive=ad0`
  to the q35 onboard `ich9-ahci` controller and requires
  `OZERO: ahci read/write OK` and `OZERO: ahci error path OK`.
- The matrix moved to an `include:` list so each cell carries its own QEMU arguments;
  the previous `||` chain in `QEMU_EXTRA_ARGS` could not express a third cell.
- The NVMe cell stays disabled until the AHCI work is merged.
- **Commits:** `6ef26b8` (cell), `8d75d06` (driver corrections), `0397433`
  (device interrupt gates and fault reporting).
- **Run:** `45854731661` — success. Every cell green, including
  `storage (ahci, smp=2)`, `storage (virtio, smp=2)`, `lock-order-boot`,
  `host-sanitizers` and all twelve boot cells.
- **Serial markers read from that run:** `ahci ready port=5 sectors=16384`,
  `ahci read/write OK irq=0 sectors=16384`, `ahci error path OK`,
  `user entry OK`.
- **Emulator:** `qemu-system-x86_64` on a q35 machine, 512 MiB, `-smp 2`,
  `-device ide-hd` behind the onboard `ich9-ahci`. Emulator evidence only; no
  physical controller has been tested.
- **Local checks:** `make kernel`, `make test`, `make test-sanitize` pass.
  `make check` cannot run in this workspace (no `xorriso`, no QEMU), so the CI
  matrix is the only boot evidence.
- **Root cause of the long red run:** the driver's command table put the PRDT at
  `0x70` instead of `0x80`, so IDENTIFY moved 512 bytes into an empty descriptor;
  and the first MSI landed on an IDT gate that did not exist, which raised #GP
  and halted the machine after the storage test had already passed.

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
| Wave 4.2–4.3 PCI storage (virtio-blk, AHCI, NVMe) | AHCI and virtio-blk cells green in CI; NVMe still build-verified only | QEMU cells in run `45854731661`; emulator only |
| Stage 5 GPU software foundation | closed as software milestone; physical GPU deferred | host tests, QEMU PCI discovery markers |
| Stage 6.1–6.8 graphics/desktop | implemented, host-tested, wired into `kmain`; Phase 6.7 custom O0 desktop UI implemented (spheres, particles, nodes, taskbar, panel, tray, assistant) | host tests; **no desktop QEMU acceptance claimed** |
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

## Wave 4.5 — Crash-consistent O0FS (2026-11-26)

- **Commit:** `f66b082` on branch `kilo/late-alpaca-bx7`, not pushed to `main`.
- **ADR:** [0018](decisions/0018-o0fs-redo-journal-implementation.md) — `Accepted`.
- **CI:** not yet observed; `make test` (fs_test 10/10, o0fs_journal_test 5/5) and
  `make test-sanitize` pass locally.
- **Evidence type:** local build + host tests only. Not CI-observed, not
  Director-observed, not physical.
- **Not covered:** physical controllers (never), data journalling for sub-block
  files (deferred, ADR 0018 §6).

## Wave 4.6 — VFS and file syscalls (2026-11-26)

- **Commit:** `64679eb` on branch `kilo/late-alpaca-bx7`, not pushed to `main`.
- **ADR:** [0019](decisions/0019-vfs-layer-file-syscalls.md) — `Accepted`.
- **CI:** not yet observed; `make test` (vfs_test 10/10 in addition to existing
  suites) and `make test-sanitize` pass locally.
- **Evidence type:** local build + host tests only. Not CI-observed, not
  Director-observed, not physical.
- **Not covered:** recursive path resolution, directory listing for
  subdirectories, `O0_O_CREAT`/`O0_O_DIRECTORY` semantics, a second filesystem
  type, user process read/write (deferred).

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
