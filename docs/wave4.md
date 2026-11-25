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
   Director review:
   - Crash consistency: journal versus copy-on-write for O0FS.
   - On-disk format revision and versioning.
   - Block-layer async completion and DMA ownership rules.
3. Update the O0FS format doc. The format stays a proposal until the Director approves it.

Status 2026-11-25: item 1 is recorded in `docs/PROJECT_STATUS.md` and
`docs/handoff.md` (run `45697656276` on `e066168`); item 2 is drafted as ADRs
[0012](decisions/0012-o0fs-crash-consistency.md),
[0013](decisions/0013-o0fs-format-versioning.md) and
[0014](decisions/0014-block-async-completion-dma-ownership.md), all
`Accepted-provisional`; item 3 is recorded in `docs/o0fs-format.md` §8. No Wave 4 code has
started.

## Wave 4.1 — PCI and DMA foundations

*Depends on: Wave 3 (IOAPIC routing, buddy PMM).*

1. Finish PCI enumeration: BAR sizing, capability list walking, MSI and MSI-X, routed
   through the IOAPIC/LAPIC code from Wave 3.
2. A DMA allocator that returns physically contiguous, correctly aligned PMM pages with
   explicit ownership. Use bounce buffers by default, since no IOMMU is configured.
3. Tests: host tests for BAR and capability parsing against recorded config-space dumps,
   and a negative test that a DMA buffer is never freed while a request is in flight.

## Wave 4.2 — virtio-blk first

1. Legacy and modern virtio PCI transport, one virtqueue, and interrupt-driven completion.
2. Register it behind the existing block layer, with the timeout, quarantine and
   cancellation rules already there.
3. QEMU marker: `virtio-blk read/write OK`, run with a disk image attached in CI. Add a CI
   cell, and a test that injects an I/O error.
4. Run it with `-smp 2` as well, to test completion interrupts on a non-boot CPU.

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

## Wave 4.5 — Crash-consistent O0FS

1. Implement the ADR's choice, replacing rollback-only recovery.
2. Tests: power-loss injection at every write point (each block write, flush and reorder),
   remount, and a consistency checker that must pass after every injected crash.
3. Keep write-ordering explicit, and honor flush and FUA semantics.
4. A format-version field, so old images are refused or migrated and never silently misread.

## Wave 4.6 — VFS and file syscalls

*Depends on: Wave 2 ABI, capability model.*

1. A VFS layer with mount table, vnode abstraction, path resolution and file descriptors,
   plus a page cache.
2. A second filesystem (a simple read-only or tmpfs-style one) to prove the VFS interface
   isn't O0FS-shaped.
3. File syscalls through an ABI ADR revision (`open`, `read`, `write`, `close`, `stat`,
   `readdir`). Numbers are never reused, and every user pointer goes through the copy
   helpers.
4. Tests: negative tests for each syscall (bad fd, bad pointer, path traversal, oversize),
   and a user process that reads and writes a file.

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
