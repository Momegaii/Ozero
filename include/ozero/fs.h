/* Ozero kernel - native filesystem interface (draft).
 *
 * The on-disk format implemented here (OzeroFS, "o0fs") is a Stage 4 proposal,
 * documented in docs/o0fs-format.md and docs/decisions/0003-stage4-storage.md.
 * It is not an approved permanent Ozero filesystem and it is not derived from
 * an ext2/FFS on-disk layout. It exists so storage operations and their failure
 * modes can be tested before a filesystem decision is approved.
 *
 * Properties of the draft:
 *   - fixed 4 KiB blocks, one block bitmap, one inode bitmap;
 *   - 128-byte inodes with 12 direct block pointers and one single-indirect
 *     block;
 *   - fixed 32-byte directory entries with 23-byte names;
 *   - a single volume, no hard links, no permissions, no timestamps;
 *   - mount is read-write; there is no journal and no crash-consistency claim.
 *
 * Limits are part of the interface because they are enforced: a whole-device
 * filesystem of at most 16 MiB and at most two simultaneous mounts.
 */
#ifndef O0_FS_H
#define O0_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/block.h"
#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0FS_BLOCK_SIZE 4096u
#define O0FS_NAME_MAX 23u
#define O0FS_PATH_MAX 128u
#define O0FS_MAX_DEPTH 16u
#define O0FS_DIRECT_BLOCKS 12u
#define O0FS_INDIRECT_ENTRIES (O0FS_BLOCK_SIZE / 4u)
#define O0FS_MAX_FILE_BLOCKS (O0FS_DIRECT_BLOCKS + O0FS_INDIRECT_ENTRIES)
#define O0FS_MAX_FILE_BYTES ((uint64_t)O0FS_MAX_FILE_BLOCKS * O0FS_BLOCK_SIZE)
#define O0FS_MAX_BLOCK_COUNT 4096u
#define O0FS_MIN_BLOCK_COUNT 32u
#define O0FS_MAX_MOUNTS 2u

enum o0fs_type {
    O0FS_TYPE_FILE = 1,
    O0FS_TYPE_DIR = 2
};

struct o0fs_stat {
    uint32_t inode;
    uint16_t type;  /* enum o0fs_type */
    uint16_t links;
    uint32_t blocks;
    uint64_t size;  /* bytes; for directories this is the entry storage */
};

struct o0fs;

/* Called for each live directory entry. The name is NUL-terminated and the stat
 * describes the entry's inode. */
typedef void (*o0fs_iter_fn)(void *ctx, const char *name,
                             const struct o0fs_stat *stat);

/* Writes an empty OzeroFS volume over the whole device. The device must report
 * a 4096-byte logical block size and between 32 and 4096 blocks. Existing data
 * is destroyed. */
O0_NODISCARD o0_status o0fs_format(struct o0_block_dev *dev);

O0_NODISCARD o0_status o0fs_mount(struct o0_block_dev *dev, struct o0fs **out);

/* Flushes metadata and the block device, then releases the mount. */
o0_status o0fs_unmount(struct o0fs *fs);

/* Flushes dirty block/inode bitmaps, the superblock and the block device. */
O0_NODISCARD o0_status o0fs_sync(struct o0fs *fs);

O0_NODISCARD o0_status o0fs_stat(struct o0fs *fs, const char *path,
                                 struct o0fs_stat *out);

O0_NODISCARD o0_status o0fs_lookup(struct o0fs *fs, const char *path,
                                   uint32_t *inode_out);

O0_NODISCARD o0_status o0fs_create_file(struct o0fs *fs, const char *path);
O0_NODISCARD o0_status o0fs_mkdir(struct o0fs *fs, const char *path);

/* Reads up to capacity bytes starting at offset. Short reads are normal at end
 * of file; *out_len receives the number of bytes copied. */
O0_NODISCARD o0_status o0fs_read(struct o0fs *fs, const char *path,
                                 uint64_t offset, void *buffer, size_t capacity,
                                 size_t *out_len);

/* Writes len bytes at offset, extending the file and zero-filling any gap. */
O0_NODISCARD o0_status o0fs_write(struct o0fs *fs, const char *path,
                                  uint64_t offset, const void *buffer,
                                  size_t len, size_t *out_len);

/* Grows (zero-filled) or shrinks the file to size bytes. */
O0_NODISCARD o0_status o0fs_truncate(struct o0fs *fs, const char *path,
                                     uint64_t size);

/* Removes a non-directory entry and releases its inode and data blocks. */
O0_NODISCARD o0_status o0fs_unlink(struct o0fs *fs, const char *path);

/* Removes an empty directory. The root directory cannot be removed. */
O0_NODISCARD o0_status o0fs_rmdir(struct o0fs *fs, const char *path);

/* Lists a directory; "." and ".." are not stored and therefore not reported. */
O0_NODISCARD o0_status o0fs_list(struct o0fs *fs, const char *path,
                                 o0fs_iter_fn fn, void *ctx);

uint32_t o0fs_block_count(const struct o0fs *fs);
uint32_t o0fs_free_blocks(const struct o0fs *fs);
uint32_t o0fs_free_inodes(const struct o0fs *fs);

#ifdef __cplusplus
}
#endif

#endif /* O0_FS_H */
