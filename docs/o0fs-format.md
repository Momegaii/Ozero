# OzeroFS on-disk format (draft)

Status: **Proposal — implemented for Stage 4 review, not approved as Ozero's permanent
filesystem.** See `docs/decisions/0003-stage4-storage.md`. Sections 1–7 describe format
version 2 (implemented in Wave 4.5). The versioning policy and proposed additional
changes are in §8; ADRs
[0012](decisions/0012-o0fs-crash-consistency.md) and
[0013](decisions/0013-o0fs-format-versioning.md) cover the design decisions behind
them.

OzeroFS ("o0fs") is the filesystem used by the Stage 4 storage work. It exists so
block-layer and filesystem operations, including their failure modes, can be built and
tested before a filesystem decision is approved. It is deliberately small, fully
specified here, and has no compatibility goal with ext2/FFS/NTFS.

All integers are little-endian. The implementation is x86-64 only at this stage, so
on-disk structures are written directly from their C definitions; `o0fs.c` asserts their
sizes at compile time.

## 1. Volume layout

The filesystem occupies the whole block device and uses fixed 4 KiB blocks.

| Block range | Contents |
|---|---|
| 0 | Superblock |
| 1 … `inode_table_blocks` | Inode table, 32 inodes per block |
| `block_bitmap_block` | Block allocation bitmap (one block) |
| `inode_bitmap_block` | Inode allocation bitmap (one block) |
| `journal_block` … `journal_block + journal_blocks - 1` | Redo journal region (header block + record blocks) |
| `data_block` … `block_count - 1` | File data and directory blocks |

`o0fs_format` chooses `inode_table_blocks = clamp(block_count / 16, 1, 16)`, so a
volume has between 64 and 512 inodes. Both bitmaps fit in one block because a volume is
limited to 4096 blocks. The journal region is reserved at format time and is not
reported as free space; its size is fixed at `O0FS_JOURNAL_BLOCKS` (16) blocks.

## 2. Superblock (block 0)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `magic` = 0x5346304F ("O0FS") |
| 4 | 2 | `version` = 2 |
| 6 | 2 | `block_size` = 4096 |
| 8 | 4 | `block_count` |
| 12 | 4 | `inode_count` |
| 16 | 4 | `inode_table_block` = 1 |
| 20 | 4 | `inode_table_blocks` |
| 24 | 4 | `block_bitmap_block` |
| 28 | 4 | `block_bitmap_blocks` = 1 |
| 32 | 4 | `inode_bitmap_block` |
| 36 | 4 | `inode_bitmap_blocks` = 1 |
| 40 | 4 | `journal_block` |
| 44 | 4 | `journal_blocks` |
| 48 | 4 | `data_block` |
| 52 | 4 | `root_inode` = 1 |
| 56 | 4 | `flags` (0; reserved) |
| 60 | 4 | `checksum` |
| 64 | 4 | `free_blocks` |
| 68 | 4 | `free_inodes` |
| 72 | 48 | Reserved, zero |

`checksum` is a 32-bit FNV-1a hash over bytes `[0, 60)`. Mounting rejects a wrong
magic, version, block size, checksum, or an out-of-range/ inconsistent layout. The
superblock is rewritten by `o0fs_sync` and on unmount.

## 3. Inode

128 bytes, inode 0 reserved, inode 1 is the root directory.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | `mode` (1 = file, 2 = directory) |
| 2 | 2 | `links` (1 for a live object; not enforced) |
| 4 | 4 | `size` in bytes; for a directory, allocated entry storage |
| 8 | 4 | `flags` (0; reserved) |
| 12 | 4 | `blocks` (allocated data blocks, including one indirect block) |
| 16 | 48 | `direct[12]` block numbers |
| 64 | 4 | `indirect` block number |
| 68 | 60 | Reserved, zero |

A file block index below 12 uses `direct[index]`. Indexes 12 and above use entry
`index - 12` of the single indirect block, a 4 KiB block holding 1024 32-bit block
numbers. Maximum file size is `(12 + 1024) * 4096` bytes = 4,243,456 bytes. A zero
block number means a hole; reads return zeros.

Directories use the same inode and block mapping as files; their data blocks are
arrays of directory entries.

## 4. Directory entry

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `inode` (0 = empty slot) |
| 4 | 2 | `rec_len` = 32 |
| 6 | 1 | `name_len` (1…23) |
| 7 | 1 | `type` (mirrors the target inode mode) |
| 8 | 24 | `name`, NUL-padded |

Entries occupy fixed 32-byte slots. Removal clears `inode` and leaves the slot for
reuse; no compaction is performed. Directory size is always a multiple of 4096.
`.` and `..` are not stored.

## 5. Allocation

- The block bitmap marks block 0, the inode table, both bitmap blocks and every
  allocated data block. New data blocks are zeroed before use.
- The inode bitmap marks inode 0 (invalid), the root inode and every live inode.
- `free_blocks` and `free_inodes` are advisory counters maintained in memory and
  flushed with the superblock. Bitmaps are the authority.
- Allocation is first-fit from `data_block` upward.

## 6. Operations and their failure modes

| Operation | Behavior |
|---|---|
| `o0fs_format` | Rejects a device whose logical block size is not 4096 (`ENOTSUP`), fewer than 32 blocks (`ENOSPC`) or more than 4096 blocks (`ERANGE`). Destroys existing data. |
| `o0fs_mount` | Validates the superblock and root inode. On failure releases every page it allocated and returns `EINVAL`/`EIO`. |
| `o0fs_create_file`, `o0fs_mkdir` | Fail with `EEXIST` if the name exists, `ENOENT` if the parent is missing, `ENOTDIR` if a component is a file, `EINVAL` for an invalid or over-long path. |
| `o0fs_read` | Short reads at end of file; a hole reads as zeros; reading a directory is `EISDIR`. |
| `o0fs_write` | Extends the file and zero-fills any gap. Before mutation, it captures undo images of existing data blocks that may be overwritten. On a reported failure it attempts to restore those blocks, release newly allocated blocks and restore the previous inode. The rollback is only as reliable as the device's ability to complete the recovery writes. |
| `o0fs_truncate` | Shrinking frees blocks beyond the new size; growing zero-fills. |
| `o0fs_unlink` | Files only; a directory is `EISDIR`. Removes the entry, then frees the inode and its blocks. |
| `o0fs_rmdir` | Empty directories only (`ENOTEMPTY`); the root is `EPERM`. |

Paths start with `/`, have at most 16 components, and each component is at most 23
bytes. `.` and `..` are rejected.

## 7. Crash consistency (redo journal, version 2)

A reserved journal region sits between the inode bitmap and the first data
block (see §1). Metadata modifications are transactions: each metadata block
image is staged, written as checksummed records to the journal, flushed, then
committed by a single sequence-numbered header block. Data blocks are written
in place and flushed *before* the commit that references them (ordered data).

On mount, `fs_journal_recover` scans the journal for valid committed
transactions and replays them in sequence order; uncommitted or torn
transactions are discarded. The consistency checker `o0fs_check` validates
bitmaps against reachable inodes, directory entries against inode types,
double-referenced blocks and indirect blocks, and runs in host tests after
every injected crash.

Consequences:

- A power loss between block writes leaves the volume at the last committed
  transaction on remount; recovery replays committed transactions and rolls
  uncommitted ones back.
- `o0fs_sync` on unmount flushes the superblock and bitmaps; a remount
  without an intervening sync sees the last synced state plus any committed
  journal transactions.
- Each metadata-modifying operation (create, mkdir, write, truncate, unlink,
  rmdir) is one transaction; data blocks are ordered before metadata commits.
- Write throughput pays one extra journal write plus commit flush per
  transaction; acceptable for the emulator-first stage.


## 8. Format revision and versioning (version 2 — ADR 0013, implemented in Wave 4.5)

Version 2 is implemented in Wave 4.5 per ADR 0018. The changes from the proposal are
realized as follows:

| Proposed change | Status |
|---|---|
| `version` split into major (high byte) and minor (low byte) | Implemented: `O0FS_VERSION` encodes major/minor; a newer major is refused |
| `flags` becomes compat / ro_compat / incompat bit ranges | Deferred: `flags` remains reserved |
| Journal region described by the superblock | Implemented: `journal_block`, `journal_blocks` in the superblock |
| Checksum algorithm id | Deferred: FNV-1a is hard-coded |
| Reserved bytes must be zero | Deferred: reserved bytes are not checked |

Mount policy: refuse a newer major outright; never upgrade a volume during mount —
migration is an explicit offline operation.


## 9. Open questions for the Project Director

1. Is a bitmap/inode design the right native format, or should Ozero choose a
   log-structured or copy-on-write design with checksums and snapshots?
2. Should the native volume format be extent-based and 64-bit from the start?
3. What metadata does Ozero require (ownership, ACLs, capabilities, timestamps,
   alternate streams) and how do they map to the native object model?
4. How should Windows compatibility expose NTFS volumes without making NTFS the
   native format?
