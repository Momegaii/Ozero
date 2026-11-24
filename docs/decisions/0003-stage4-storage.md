# ADR 0003: Stage 4 storage substrate

- Status: **Proposed** — implementation landed for review; Project Director approval
  pending, in particular for the draft filesystem format
- Date proposed: 2026-11-23
- Scope: block layer behavior, the first storage driver, and the draft filesystem used
  to exercise storage operations and their failure paths.

## Context

`docs/ROADMAP.md` defines Stage 4 as "storage, filesystem and drivers: block layer,
initial storage drivers, filesystem operations and fault tests". Before Stage 4 the
repository contained interface sketches (`include/ozero/block.h`,
`include/ozero/driver.h`) and no implementation. There is no PCI enumeration, no ATA,
AHCI or NVMe driver, no DMA engine use and no user mode, so a hardware storage driver
cannot be validated here.

The mandatory architecture rule (`AGENTS.md`) forbids making Linux conventions
foundational. `docs/ARCHITECTURE_BASELINE.md` §7 still lists "filesystem selection and
on-disk format" as an unresolved decision.

## Decision

### Block layer

- `kernel/src/block.c` implements the contract declared in `include/ozero/block.h`:
  registration with validated geometry, a unique name and one or more queues;
  single-bio submission with bounds, alignment, flag and feature validation; and one
  completion per bio.
- A new synchronous single-segment API (`o0_block_read`, `o0_block_write`,
  `o0_block_flush`) submits one bio and waits for completion. A driver must complete
  before `queue_rq` returns or provide a poll callback, otherwise the call ends with
  `O0_ETIMEDOUT`.
- Request merging, queue-depth accounting, an asynchronous I/O scheduler, discard and
  zone operations are explicitly not implemented.

### First storage driver

- `kernel/src/ramdisk.c` registers a real `o0_block_dev` backed by PMM pages reached
  through the direct map. It is the reference driver and the fault-injection target
  for block and filesystem tests (`o0_ramdisk_inject_fault`).
- It is limited to two devices of at most 4 MiB, 4 KiB logical blocks, and holds no
  data across a reboot.

### DRAFT filesystem

- `kernel/src/o0fs.c` implements the draft OzeroFS format specified in
  `docs/o0fs-format.md`: 4 KiB blocks, a superblock with a checksum, bitmap allocation,
  128-byte inodes with 12 direct blocks and one single-indirect block, and fixed
  32-byte directory entries.
- Operations: format, mount, sync, unmount, stat, lookup, create file, mkdir, read,
  write, truncate, unlink, rmdir, list and free-space queries.
- Failure paths are part of the design: bad magic/checksum/layout on mount, path and
  type errors with dedicated status codes, `ENOSPC`, injected I/O errors, and a write
  path that releases blocks allocated by a failed write.

This is a **proposal for the format**, not a final architecture decision. It borrows
only generic filesystem concepts (superblock, inode, bitmaps, indirect blocks). Its
layout is not an ext2, FFS or NTFS layout, and no Linux or Windows on-disk structure is
required by the native path.

## Alternatives considered

| Alternative | Why not now |
|---|---|
| Wait for a real ATA/AHCI driver before implementing the block layer | The block layer and its validation are independent of the transport; a RAM disk proves them now and the driver slots in later. |
| Implement the full asynchronous request model with merging and queue scheduling | Nothing yet produces concurrent I/O; the synchronous path exposes fewer untested edge cases and the async model can be added with a real driver. |
| Make the native filesystem FAT or ext2 for familiarity | Violates the architecture rule and imports conventions Ozero has not chosen; it also hides the format decision behind an external layout. |
| Keep only a VFS in memory and no on-disk format | Stage 4 exit evidence explicitly includes filesystem operations; an in-memory tree does not exercise block allocation, metadata persistence or fault handling. |
| Journal or copy-on-write from the start | Adds recovery design and complexity before the storage substrate and format are approved. Recorded as a follow-up, not silently ignored. |

## Consequences

- The direct-map dependency means the RAM disk cannot exceed the 4 GiB direct-map
  window; its 4 MiB limit keeps it far below that.
- OzeroFS is single-volume, has no permissions, timestamps, hard links, symlinks,
  extended attributes, journal or `fsck`; it is not suitable for user data.
- The synchronous block API is not the future native I/O ABI; it is a Stage 4 client
  path.
- `include/ozero/block.h` gained the synchronous helpers and registry inspection
  functions; the existing A3 contract types and callbacks are unchanged.
- New status codes were required by real filesystem semantics: `EEXIST`, `ENOTDIR`,
  `EISDIR`, `ENOTEMPTY`, `ENOSPC`.

## Follow-up work

1. Real hardware path: PCI enumeration, AHCI and NVMe drivers, DMA constraints and
   IRQ-driven completion, with queue-depth accounting and the asynchronous request
   model.
2. Partition discovery (MBR/GPT) and a partition window on top of the block layer.
3. A native I/O ABI decision for services and applications; the synchronous helper is
   not it.
4. Filesystem decision: approve or replace the draft format, then add recovery
   (`fsck`), crash-consistency design, permissions and metadata.
5. A storage test image and a QEMU-visible virtual disk so the boot self-test exercises
   a driver other than the RAM disk.

## Verification

`make kernel` links cleanly with `-Werror`, `make test` runs the new block and OzeroFS
host suites (4 + 9 tests) plus the existing suites, and the kernel boot self-test
formats, mounts, writes, reads, injects a storage fault and recovers. QEMU itself was
not available in the environment where this change was produced, so the boot markers
are added to `tools/qemu-smoke.sh` but were not executed there.
