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


## Director-attested local QEMU validation (2026-11-26)

- **Director report:** TCG and HAXM QEMU runs have been performed locally and are working so far.
- **Evidence type:** Director-attested local QEMU validation. This is recorded as the Director's report, not independently inspected CI evidence.
- **Scope limit:** This does not constitute physical-hardware validation and does not by itself close any Wave 4 driver, filesystem, or crash-recovery acceptance gate.
- **Follow-up:** Preserve the local serial logs and exact QEMU command lines in the handoff when available, so later reviews can correlate the attestation with specific commits and test cells.

## Wave 4.2 — modern virtio PCI transport (2026-11-26, branch `feat/wave4-modern-virtio-transport`)

- **Branch:** `feat/wave4-modern-virtio-transport`, PR #8 (draft). Not merged.
- **Implemented** (design record: [ADR 0016](decisions/0016-modern-virtio-pci-transport.md)):
  - `include/ozero/virtio_pci_mmio.h` — register interface, common-configuration offsets,
    status/feature bits, common/notify/ISR/device region accessors, reset, feature read and
    negotiation, `DRIVER_OK`.
  - `kernel/src/virtio_pci_mmio.c` — bounded little-endian MMIO accessors, reset with a
    bounded poll, feature-bank reads, `VERSION_1` + `FEATURES_OK` negotiation that fails
    closed.
  - `kernel/src/virtio_pci_map.c` — capability-BAR mapping through `o0_pci_probe_bar` and
    `o0_as_map` into a per-device uncached window.
  - `include/ozero/virtio_queue.h`, `kernel/src/virtio_queue.c` — queue size selection,
    notification offset arithmetic, DMA address validation, ordered queue programming,
    descriptor free list, available-ring publication, used-ring completion validation,
    reset/re-init bookkeeping.
  - `kernel/src/virtio_blk_modern.c` — the modern block backend: capability discovery,
    queue 0 configuration, header/data/status descriptor chains, doorbell, bounded poll,
    injected-error path, and a reset-based quiesce path used on timeout or malformed
    completion.
  - `kernel/src/virtio_blk.c` — legacy driver kept intact and renamed to
    `o0_virtio_blk_legacy_probe`; the public entry points now dispatch between transports.
- **Tests (host, run in this workspace):**
  - `make test` — all suites pass, including
    `virtio_ring_test`, `virtio_pci_cap_test`, `virtio_pci_mmio_test`, `virtio_queue_test`.
  - `make test-sanitize` — the same suites under ASan/UBSan with leak detection, exit 0.
  - `make kernel` — freestanding ELF64 kernel builds with `-Werror`.
  - The virtio tests use `tests/virtio_mock.h`, a device model with two feature banks,
    status latching, `FEATURES_OK` rejection, reset stalling, per-queue state and fixed
    register widths. A wrong-width or out-of-region access is refused by the model, so the
    tests cannot pass against a flat byte array.
- **Not yet verified:**
  - No QEMU run was possible in this workspace (no `qemu-system-x86_64`, no `xorriso`, no
    root to install them). The two storage cells are therefore unobserved at the time of
    writing; the CI run identifiers and conclusions are appended only after they exist.
  - No physical controller, no asynchronous submission, no performance claim.

## Open items

- CI was inspected on the Wave 4.0 HEAD `3b48810`: 14/14 jobs `success` in run
  `45700432451`. No CI-built ISO artifact has been published.
- User threads remain BSP-pinned until per-CPU TSS/IST exists. [ADR 0015](decisions/0015-per-cpu-tss-ist.md)
  is the design note for that work; it is a blocking dependency before Stage 6 uses SMP
  beyond the BSP. Ring 3 and SMAP on non-boot CPUs are untested.
- No physical-hardware evidence is claimed. The Director separately attests successful local QEMU TCG and HAXM runs (see above).
- Stage 7–10 not started. Wave 4.0 is closed: ADRs 0012–0014 were approved by the
  Director on 2026-11-25 and the O0FS format is still a proposal. Wave 4.1 is
  implemented and CI-green on `24b8d23` (run `45708532478`). Wave 4.2 and 4.3 are
  implemented and build-verified on `775e333`; their three storage cells have not
  been observed green, so they are not evidence until a run is recorded here.

## Rules that stay in force

- Evidence claims cite a commit on `main`.
- Host, QEMU/CI, Director-observed and physical evidence are reported separately.
- Anything unverifiable stays refused or quarantined, never assumed.
