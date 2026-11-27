# Wave 4 — Storage and filesystem

Directive from the Project Director, recorded 2026-11-25. This wave starts only after
the Wave 3 sign-off conditions below are met.

## Rule 0 — gate

Wave 4 starts only after Wave 3 is signed off, meaning the SMP CI cells (`-smp 2`,
`-cpu max`, a `LOCK_ORDER=1` boot cell) are green on `main` **and** the Director has
signed off. This wave closes the open Stage 4 exit criteria: real storage driver,
partition discovery, approved filesystem format, crash-consistency design.

Evidence stays split into host, QEMU/CI, Director-observed and physical. Physical
results stay with the Director and are never a gate.

## Wave 4.0 — Gate and design

1. Confirm the Wave 3 sign-off in STATUS, citing the `main` commit and CI run.
2. Write ADRs, each with alternatives, marked `Accepted-provisional` for end-of-run
   Director review (completed 2026-11-25: approved as `Accepted`):
   - Crash consistency: journal versus copy-on-write for O0FS.
   - On-disk format revision and versioning.
   - Block-layer async completion and DMA ownership rules.
3. Update the O0FS format doc. The format stays a proposal until the Director approves it.

Status 2026-11-25: **Wave 4.0 is closed.** Item 1 is recorded in
`docs/PROJECT_STATUS.md` and `docs/handoff.md` (run `45697656276` on `e066168`); the
Director confirmed the Wave 3 sign-off. Item 2 is ADRs
[0012](decisions/0012-o0fs-crash-consistency.md),
[0013](decisions/0013-o0fs-format-versioning.md) and
[0014](decisions/0014-block-async-completion-dma-ownership.md), approved by the Director
on 2026-11-25 and now `Accepted`; ADR 0012 carries the condition that its crash-injection
matrix covers every write point. Item 3 is recorded in `docs/o0fs-format.md` §8, and the
format remains a **proposal**: it is not approved.

ADR [0015](decisions/0015-per-cpu-tss-ist.md) is the per-CPU TSS/IST design note the
Director required before Stage 6 uses SMP. It is design only and changes no current
behaviour.

## Wave 4.1 status

1. PCI: the header type is masked to its layout bits (the multi-function bit no longer
   zeroes the BAR count), the capability list is walked from one config-space snapshot
   (status bit 4 required, bounded walk, self-referential list rejected), and BAR size is
   decoded from the all-ones mask. MSI and MSI-X can be programmed for one vector with
   fixed delivery and a physical destination, and device vectors are dispatched through
   the LAPIC vector path; the I/O APIC stays the route for legacy lines.
2. DMA: `kernel/src/dma.c` returns physically contiguous, alignment-aware PMM runs,
   checks the device's `addr_mask`, and keeps a per-buffer pin count. `o0_dma_free()`
   returns `O0_EBUSY` and frees nothing while a pin is held. Bounce buffers remain the
   block layer's default; no driver uses the allocator yet.
3. Host tests: `tests/pci_test.c` covers BAR sizing, capability parsing over constructed
   config-space images and MSI/MSI-X encoding; `tests/dma_test.c` covers page rounding,
   alignment, constraints, the pinned-free refusal, nested pins and double free.
4. Boot marker `OZERO: pci caps msi=<n> msix=<n>` is emitted after enumeration and
   required by `tools/qemu-smoke.sh`. Commit hash and CI run are recorded under Wave 4.1
   evidence below once the run is green. Nothing in 4.1 is claimed as verified before
   that.

## Wave 4.1 — PCI and DMA foundations

*Depends on: Wave 3 (IOAPIC routing, buddy PMM).*

1. Finish PCI enumeration: BAR sizing, capability list walking, MSI and MSI-X, routed
   through the IOAPIC/LAPIC code from Wave 3.
2. A DMA allocator that returns physically contiguous, correctly aligned PMM pages with
   explicit ownership. Use bounce buffers by default, since no IOMMU is configured.
3. Tests: host tests for BAR and capability parsing against recorded config-space dumps,
   and a negative test that a DMA buffer is never freed while a request is in flight.

### Wave 4.1 evidence

- **Commit:** `24b8d23` (`feat(kernel): add PCI MSI/MSI-X routing and the DMA allocator`)
  on `main`.
- **CI:** [run 45708532478](https://github.com/Momegaii/Ozero/actions/runs/45708532478) —
  `completed` / `success`, 14/14 jobs: 12 boot cells
  (`{bios, uefi} × {qemu64, Haswell-v1, max} × -smp {1, 2}`), `lock-order-boot` and
  `host-sanitizers`.
- **Docs commit:** `bbfbc25` is green in
  [run 45708413626](https://github.com/Momegaii/Ozero/actions/runs/45708413626), also
  14/14 jobs.
- **Evidence type:** CI. Not Director-observed, not physical.
- **Log-level evidence:** the marker lines were read, not inferred from job status. See
  the CI evidence channel below; the logs for the current `main` tree were inspected in
  run 45715078528.
- **Check limit (historical):** for runs before the evidence channel existed, the Actions
  job-log endpoint answered `403` ("Must have admin rights to Repository.") and artifact
  archives answered `401`, so those runs could only be read at job and step granularity.
  `24b8d23` and `bbfbc25` were recorded that way. `make test` and `make test-sanitize` also
  passed in the workspace, which is local-only and not evidence on those commits.
- **Not covered:** no driver uses the DMA allocator or MSI-X yet, so no device path is
  exercised. That is Wave 4.2.

## CI evidence channel (`ci-evidence` branch)

The Actions job-log and artifact endpoints require repository admin rights. A reader who
can clone the repository therefore could not read what a boot cell printed, and a failing
cell could not be diagnosed at all. The `publish-evidence` job in
`.github/workflows/quality.yml` removes that gap: after the matrix and the sanitizer job
finish it republishes every cell's diagnostics on the `ci-evidence` branch under
`evidence/ci/run-<run-id>-<attempt>/`, plus an `INDEX.txt` per run. It runs with
`always()`, so a failing cell's log is captured. Serial logs stay plain text; disassembly
is not published and QEMU debug logs are gzipped to keep the branch small. The branch is
not a build input and nothing on `main` reads it.

Reading it:

```sh
git fetch origin ci-evidence:refs/remotes/origin/ci-evidence
git show origin/ci-evidence:evidence/ci/run-<run-id>-1/INDEX.txt
```

First use: run 45715078528 on `c8d3af6`. All 12 boot cells and the lock-order cell were
read line by line and contain `OZERO: build commit=c8d3af67`, `OZERO: pci devices=6` and
`OZERO: pci caps msi=2 msix=1`; the `-smp 2` cells add `OZERO: smp cpus=2 online=2`,
`OZERO: tlb shootdown OK status=0 ipi_seen=1` and
`OZERO: smp scheduling OK cpus=2 threads=2 iterations=11 migrations=11`; the `max` cells
add `OZERO: smep=on smap=on`; the lock-order cell adds `OZERO: lock order check=on`. The
kernel in `c8d3af6` is the `004c80a` tree, so these are the Wave 4.1 markers at log level.
This is CI evidence, not Director-observed and not physical.

### Wave 4.2 status

Implemented in `kernel/src/virtio_blk.c` (legacy transport, device ID `0x1001`)
and `kernel/src/virtio_blk_modern.c` (modern MMIO transport, device ID
`0x1042`, PCI vendor-specific capability ID `0x09`), both registered through
`o0_block_register`, so the block layer's validation, timeout, quarantine and
cancellation rules apply unchanged.

- **Probe:** `o0_virtio_blk_probe` tries modern first, then falls back to
  legacy. `o0_virtio_blk_irq_count` and `o0_virtio_blk_inject_error` check both
  transports.
- **Modern transport:** BAR mapped uncached (`O0_MAP_READ | O0_MAP_WRITE |
  O0_MAP_UC | O0_MAP_NX`) below NVMe's window at
  `0xffffc00040000000`; config regions discovered by walking PCI cap ID `0x09`
  (the pure `vblk_modern_walk_caps()` is host-tested in
  `tests/virtio_blk_modern_test.c`). Feature handshake accepts
  `VIRTIO_BLK_F_FLUSH`; queue 0 programmed through MMIO; MSI-X entry 0
  opportunistic; used-ring polling bounds completion latency.
- **Queue:** one virtqueue, one request in flight, 16-byte request header plus
  optional data buffer plus 1-byte status. Ring and request header from the DMA
  allocator, pinned for device lifetime; data buffer stays the block layer's
  bounce buffer.
- **Markers:** `OZERO: virtio-blk-modern ready ...` followed by `virtio-blk
  read/write OK irq=<n> sectors=<n>` and `virtio-blk error path OK`. The second
  is the injected-error test: a read past the end of the medium must be refused
  by the device and decoded by the driver.
- **CI cell:** `storage (virtio, smp=2)` in
  `.github/workflows/quality.yml`, with `disable-modern=on` removed so the
  modern transport is exercised. `-smp 2` tests completion on a non-boot CPU.
- **Evidence:** build-verified locally (`make kernel`, `-Werror`); host test
  `tests/virtio_blk_modern_test.c` passes under `make test` and
  `make test-sanitize` (ASan+UBSan+leak detection). The CI cell needs a green
  run with logs read from the `ci-evidence` branch before it is CI evidence.
- **Not covered:** physical controllers (never), MSI-X multi-vector, multiple
  virtqueues, asynchronous submission, performance claims.

#### Correction to Wave 4.2

`o0_pci_init` records identity and capabilities but never probes BARs, and
`virtio_blk` read its legacy I/O BAR from that unprobed record. `775e333` probes
BAR0 inside the probe path, so the Wave 4.2 cell can find its device at all;
that cell still needs a run and a log read before it is evidence.

### Wave 4.3 status

Implemented in `kernel/src/ahci.c` and `kernel/src/nvme.c`, both registered through
`o0_block_register` with one queue of depth 1, so the block layer's validation,
timeout, quarantine and cancellation rules apply unchanged.

- **AHCI:** one port (the first with `PxSTS.DET == 3` and a plain SATA disk
  signature), 32-slot command list, one command table with one PRDT entry, and the
  Identify Device data. The probe walks every AHCI controller because a q35 machine
  already has one and a cell that adds `-device ich9-ahci` gets a second, empty one.
- **NVMe:** BAR0 mapped uncached, one admin queue pair and one I/O queue pair in
  NVM mode, command queues and data buffers from the DMA allocator. MSI-X entry 0
  carries admin completions and entry 1 the I/O completions, both targeting one
  vector; the I/O entry is unmasked only after the function mask is clear.
- **Completion:** both drivers poll with a bounded spin (AHCI `PxCI` clear, NVMe
  completion phase tag). A lost or misrouted interrupt is `O0_BLOCK_ERR_TIMEOUT`, not
  a hang. The interrupt handlers count and acknowledge (AHCI clears `PxIS`) but
  completion does not depend on them.
- **Markers:** `OZERO: ahci read/write OK irq=<n> sectors=<n>`, `OZERO: ahci error
  path OK`, `OZERO: nvme read/write OK irq=<n> sectors=<n>`, `OZERO: nvme error
  path OK`. The error-path markers are injected tests: a read that runs past the
  end of the medium must be refused by the controller and decoded by the driver.
- **CI cells:** `storage (ahci, smp=2)` is enabled with its own `-device` line and
  gates only on the AHCI markers, so it cannot pass on another driver's output. The
  NVMe cell is enabled with `-device nvme` and gates on the NVMe markers; it is
  **not yet observed green** — no run is claimed for it until logs are read from the
  `ci-evidence` branch. The cell attaches `-drive id=nd0,file=build/storage.img,if=none,format=raw -device nvme,drive=nd0,serial=nvme0`.
- **Evidence (AHCI, emulator only):** `storage (ahci, smp=2)` is green in run
  `45854731661` on `0397433`. The serial log shows `ahci ready port=5
  sectors=16384`, `ahci read/write OK irq=0 sectors=16384`, `ahci error path OK`
  and the boot milestone `user entry OK` after it, so the drive really took a
  read and a write rather than merely being discovered. Three driver defects had
  to be fixed first, all recorded in that commit: the PRDT starts at `0x80` in
  the command table, `PxCI` is a slot bitmap and not a port bitmap, and a timed
  out command needs the port quiesced before its buffers are reused.
- **Evidence (NVMe):** build-verified only, plus the host encoder test. The NVMe
  cell is still disabled, so no run is claimed for it.
- **Interrupt plumbing found here:** `o0_irq_alloc_vectors` and
  `o0_pci_msi_enable` were usable in principle but no IDT gate existed for the
  device vector range, so the first MSI raised #GP on an empty gate and halted
  the machine. Device gates now exist on the BSP and on every AP. This is a
  generic fix, not an AHCI one, and it is what allowed the cell to finish.
- No physical controller is claimed, and none of this is evidence for real
  hardware.
- **Not covered:** physical controllers (never), NCQ, multiple queues, PRP lists,
  asynchronous submission, hotplug and any performance claim.

#### Correction to Wave 4.2

`o0_pci_init` records identity and capabilities but never probes BARs, and
`virtio_blk` read its legacy I/O BAR from that unprobed record. `775e333` probes
BAR0 inside the probe path, so the Wave 4.2 cell can find its device at all; that
cell still needs a run and a log read before it is evidence.

## Wave 4.2 — virtio-blk first

1. Legacy and modern virtio PCI transport, one virtqueue, and interrupt-driven completion.
   **Done:** legacy transport in `kernel/src/virtio_blk.c`; modern MMIO transport in
   `kernel/src/virtio_blk_modern.c`; probe tries modern first, falls back to legacy.
   ADR [0017](decisions/0017-modern-virtio-blk-transport.md) — `Accepted`.
2. Register it behind the existing block layer, with the timeout, quarantine and
   cancellation rules already there. **Done.**
3. QEMU marker: `virtio-blk read/write OK`, run with a disk image attached in CI. Add a CI
   cell, and a test that injects an I/O error. **Done:** CI cell
   `storage (virtio, smp=2)` in `.github/workflows/quality.yml` with
   `disable-modern=on` removed; `tests/virtio_blk_modern_test.c` covers the
   capability walk under `make test` and `make test-sanitize`.
4. Run it with `-smp 2` as well, to test completion interrupts on a non-boot CPU.
   **Done:** CI cell runs `-smp 2`.

## Wave 4.3 — AHCI, then NVMe

1. AHCI: port discovery, command lists, FIS handling, NCQ optional, IRQ completion.
   QEMU `-device ich9-ahci`.
2. NVMe: admin and I/O queue pairs, MSI-X, QEMU `-device nvme`.
3. Each driver gets its own marker and CI cell. Report real-device support as unverified
   until a Director-observed run exists.
4. Do not claim support for any physical controller from emulator results.

## Wave 4.4 — Partitions

1. MBR and GPT discovery on top of the block layer, with validation of headers, CRCs,
   overlapping entries and out-of-range LBAs.
2. Fuzz the parsers on the host, with malformed and truncated images.

### Wave 4.4 evidence

- **Commit:** `pending` — `feat(kernel): add MBR/GPT partition discovery (Wave 4.4)`
- **CI:** not yet observed; `make test` and `make test-sanitize` pass locally with
  `tests/partition_test.c` (12 tests: MBR valid/no-sig/overlap/out-of-range/fuzz,
  GPT valid/bad-header-CRC/bad-entry-CRC/overlap/out-of-range/fuzz, GUID string
  formatting).
- **ADR:** [0016](decisions/0016-mbr-gpt-partition-discovery.md) — `Accepted`.
- **Tests:** `tests/partition_test.c` runs under both `make test` and
  `make test-sanitize` (ASan+UBSan, leak detection on).
- **Evidence type:** local build + host tests only. Not CI-observed, not
  Director-observed, not physical.
- **Not covered:** backup GPT header fallback, extended MBR logical partitions,
  sub-512-byte logical blocks, physical devices.

## Wave 4.5 — Crash-consistent O0FS

**Status: Complete.**

1. **Redo journal implemented** in `kernel/src/o0fs.c`: metadata-only redo journal
   with ordered data writes, atomic sequence-numbered commit headers, mount-time
   recovery (`fs_journal_recover`), bitmap save/restore on transaction abort, and
   the `o0fs_check` consistency checker. ADR [0018](decisions/0018-o0fs-redo-journal-implementation.md)
   records the design; ADR 0012's Director condition (crash-injection matrix covering
   every write point, remount + checker after each crash) is met by the journal test
   suite.
2. **Tests:** `tests/o0fs_journal_test.c` (5 crash-recovery tests: normal write+remount,
   crash before commit header, crash at commit header, crash during live application,
   clean checker) and `tests/fs_test.c` updated for journal semantics including remount
   after injected I/O faults. All pass under `make test` and `make test-sanitize`.
3. **Docs:** `docs/o0fs-format.md` §1 (journal region), §7 (crash consistency), §8
   (version 2 implemented); ADR 0018.

### Wave 4.5 evidence

- **Commit:** local only, on branch `kilo/late-alpaca-bx7`, not yet pushed to `main`.
- **CI:** not yet observed; `make test` (all suites green: fs_test 10/10,
  o0fs_journal_test 5/5) and `make test-sanitize` pass locally.
- **Evidence type:** local build + host tests only. Not CI-observed, not
  Director-observed, not physical.
- **Not covered:** physical controllers (never), data journalling for sub-block
  files (deferred, ADR 0018 §6).


## Wave 4.6 — VFS and file capabilities

*Depends on: Wave 2 ABI, capability model.*

1. VFS vnode operations, an eight-entry mount table, backend-identity cycle prevention,
   capability-relative path resolution, vnode references and an eight-page bounded
   write-through cache.
2. Writable bounded tmpfs is the reference backend. O0FS is exposed read-only until
   ADR 0012's crash-injection/recovery gate is approved; mounting O0FS does not imply
   durability or authorize writes.
3. Append-only native syscalls 17–21: `file_open`, `file_read`, `file_write`,
   `file_stat`, `file_readdir`. Existing `handle_close` (5) closes file
   capabilities. Read/write carry explicit 64-bit offsets; no POSIX descriptor
   namespace or implicit file position exists.
4. Every path is relative to an explicit directory capability. The root is granted
   only to a selected bootstrap process; child authority is transferred explicitly.
   `..` is rejected and paths/components are bounded.
5. Host tests and ring-3 QEMU acceptance cover tmpfs/O0FS, rights, metadata,
   enumeration, traversal rejection, invalid pointers, stale handles and lifetime.

**Implementation status:** integration is being rebased onto current `main` under
ADR 0022. Wave 4.6 is not complete until `make check`, `make test-sanitize`,
the QEMU ring-3 acceptance marker and all required CI jobs pass on the same commit.

## Wave 4.7 — Sign-off

1. `make check`, `make test-sanitize` and the analyzer pass.
2. A CI run on the final `main` commit with logs inspected, including the new storage cells.
3. STATUS and `docs/handoff.md` updated by evidence type, with open items listed.
4. Tick the Stage 4 exit criteria only where CI evidence exists. Leave boxes open otherwise.

## Deferred

USB storage (Stage 9), and any claim of physical storage support.

## Standing rules

- Every change needs a test and a doc update, and every architectural choice needs an ADR.
- Small commits, one wave item per commit group.
- Anything unverifiable stays refused or quarantined, never assumed.
