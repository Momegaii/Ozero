# OzeroFS on-disk format (draft)

Status: **Proposal — implemented for Stage 4 review, not approved as Ozero's permanent
filesystem.** See `docs/decisions/0003-stage4-storage.md`.

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
| `data_block` … `block_count - 1` | File data and directory blocks |

`o0fs_format` chooses `inode_table_blocks = clamp(block_count / 16, 1, 16)`, so a
volume has between 64 and 512 inodes. Both bitmaps fit in one block because a volume is
limited to 4096 blocks.

## 2. Superblock (block 0)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `magic` = 0x5346304F ("O0FS") |
| 4 | 2 | `version` = 1 |
| 6 | 2 | `block_size` = 4096 |
| 8 | 4 | `block_count` |
| 12 | 4 | `inode_count` |
| 16 | 4 | `inode_table_block` = 1 |
| 20 | 4 | `inode_table_blocks` |
| 24 | 4 | `block_bitmap_block` |
| 28 | 4 | `block_bitmap_blocks` = 1 |
| 32 | 4 | `inode_bitmap_block` |
| 36 | 4 | `inode_bitmap_blocks` = 1 |
| 40 | 4 | `data_block` |
| 44 | 4 | `root_inode` = 1 |
| 48 | 4 | `flags` (0; reserved) |
| 52 | 4 | `checksum` |
| 56 | 4 | `free_blocks` |
| 60 | 4 | `free_inodes` |
| 64 | 56 | Reserved, zero |

`checksum` is a 32-bit FNV-1a hash over bytes `[0, 52)`. Mounting rejects a wrong
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

## 7. Consistency limits

There is no journal, no copy-on-write transaction protocol and no write-back cache.
The live write path uses temporary page-backed undo images for existing data blocks;
these are released after success or rollback and are not persistent recovery records.
Each mutation writes whole 4 KiB blocks. Consequences:

- A power loss between block writes can leave the volume needing repair; runtime undo
  images do not survive power loss and there is no `fsck` yet.
- If the device also fails during rollback, the write returns `EIO`; the filesystem
  cannot promise that the previous bytes or metadata were fully restored.
- A metadata I/O error during truncate can leave freed blocks recorded as holes; the
  inode is written to match the bitmap, but the operation is not rolled back.
- Unlink removes the directory entry before releasing the inode; an I/O error in
  between leaks the inode and its blocks.
- Remount always reloads from the device, so an unmount/`o0fs_sync` is required before
  assuming durability.

These limits are deliberate for Stage 4 and are listed as follow-up work.

## 8. Open questions for the Project Director

1. Is a bitmap/inode design the right native format, or should Ozero choose a
   log-structured or copy-on-write design with checksums and snapshots?
2. Should the native volume format be extent-based and 64-bit from the start?
3. What metadata does Ozero require (ownership, ACLs, capabilities, timestamps,
   alternate streams) and how do they map to the native object model?
4. How should Windows compatibility expose NTFS volumes without making NTFS the
   native format?
