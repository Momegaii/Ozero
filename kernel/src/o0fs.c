/* Ozero kernel - OzeroFS (o0fs), the Stage 4 draft native filesystem.
 *
 * The on-disk layout is described in docs/o0fs-format.md. In short:
 *
 *   block 0                     superblock
 *   block 1 .. inode table      inodes, 32 per block, 128 bytes each
 *   block block_bitmap_block    block allocation bitmap (one block)
 *   block inode_bitmap_block    inode allocation bitmap (one block)
 *   block data_block .. end     data and directory blocks
 *
 * All metadata is little-endian and the compiler is x86-64 only at this stage,
 * so the structures below are written directly. Sizes and offsets are asserted
 * at compile time. The superblock carries a 32-bit FNV-1a checksum over its
 * fixed fields; mounting rejects a mismatch.
 *
 * There is no journal, no write-back cache and no crash-consistency claim: each
 * mutation writes whole 4 KiB blocks through the block layer, and o0fs_sync
 * flushes the bitmaps, the superblock and the device.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/block.h"
#include "ozero/fs.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#include "string.h"

#define O0FS_MAGIC 0x5346304Fu /* "O0FS" */
#define O0FS_VERSION 1u
#define O0FS_SUPER_BLOCK 0u
#define O0FS_ROOT_INODE 1u
#define O0FS_INODE_SIZE 128u
#define O0FS_INODES_PER_BLOCK (O0FS_BLOCK_SIZE / O0FS_INODE_SIZE)
#define O0FS_DIRENT_SIZE 32u
#define O0FS_DIRENT_NAME 24u

struct o0fs_super_disk {
    uint32_t magic;
    uint16_t version;
    uint16_t block_size;
    uint32_t block_count;
    uint32_t inode_count;
    uint32_t inode_table_block;
    uint32_t inode_table_blocks;
    uint32_t block_bitmap_block;
    uint32_t block_bitmap_blocks;
    uint32_t inode_bitmap_block;
    uint32_t inode_bitmap_blocks;
    uint32_t data_block;
    uint32_t root_inode;
    uint32_t flags;
    uint32_t checksum;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint8_t reserved[56];
} O0_PACKED;

struct o0fs_inode_disk {
    uint16_t mode;
    uint16_t links;
    uint32_t size;
    uint32_t flags;
    uint32_t blocks;
    uint32_t direct[O0FS_DIRECT_BLOCKS];
    uint32_t indirect;
    uint8_t reserved[60];
} O0_PACKED;

struct o0fs_dirent_disk {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t type;
    char name[O0FS_DIRENT_NAME];
} O0_PACKED;

_Static_assert(sizeof(struct o0fs_super_disk) <= O0FS_BLOCK_SIZE,
               "superblock must fit in one block");
_Static_assert(sizeof(struct o0fs_inode_disk) == O0FS_INODE_SIZE,
               "inode size mismatch");
_Static_assert(sizeof(struct o0fs_dirent_disk) == O0FS_DIRENT_SIZE,
               "directory entry size mismatch");
_Static_assert(O0FS_INODES_PER_BLOCK * O0FS_INODE_SIZE == O0FS_BLOCK_SIZE,
               "inode table packing mismatch");

struct o0fs_write_backup {
    uint32_t block;
    void *page;
};

struct o0fs {
    struct o0_block_dev *dev;
    struct o0fs_super_disk super;
    /* Per-mount undo pages for one in-flight write transaction. */
    struct o0fs_write_backup write_backups[O0FS_MAX_FILE_BLOCKS];
    uint32_t write_backup_count;
    uint8_t *blk;  /* inode table blocks and file data */
    uint8_t *dir;  /* directory blocks */
    uint8_t *ind;  /* indirect block */
    uint8_t *zero; /* zero-fill source for new blocks */
    uint8_t *bbm;  /* block bitmap, one block */
    uint8_t *ibm;  /* inode bitmap, one block */
    bool mounted;
};

static struct o0fs mounts[O0FS_MAX_MOUNTS];

/* ---- page-backed scratch buffers ---- */

static void *fs_page_alloc(void) {
    o0_paddr_t phys = o0_pmm_alloc_pages(0);
    if (phys == 0) {
        return NULL;
    }
    void *virt = o0_phys_to_virt(phys);
    if (virt == NULL) {
        o0_pmm_free_pages(phys, 0);
        return NULL;
    }
    memset(virt, 0, O0_PAGE_SIZE);
    return virt;
}

static void fs_page_free(void *page) {
    if (page == NULL) {
        return;
    }
    o0_paddr_t phys = o0_virt_to_phys(page);
    if (phys != 0) {
        o0_pmm_free_pages(phys, 0);
    }
}

/* ---- block helpers ---- */

static o0_status fs_read_block(struct o0fs *fs, uint32_t lba, void *buffer) {
    return o0_block_read(fs->dev, lba, buffer, O0FS_BLOCK_SIZE);
}

static o0_status fs_write_block(struct o0fs *fs, uint32_t lba,
                                const void *buffer) {
    return o0_block_write(fs->dev, lba, buffer, O0FS_BLOCK_SIZE);
}

/* ---- superblock ---- */

static uint32_t fs_checksum(const struct o0fs_super_disk *super) {
    const uint8_t *bytes = (const uint8_t *)super;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(struct o0fs_super_disk, checksum); ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool fs_super_valid(const struct o0fs_super_disk *super,
                           uint64_t device_blocks) {
    if (super->magic != O0FS_MAGIC || super->version != O0FS_VERSION) {
        return false;
    }
    if (super->block_size != O0FS_BLOCK_SIZE) {
        return false;
    }
    if (super->checksum != fs_checksum(super)) {
        return false;
    }
    if (super->block_count < O0FS_MIN_BLOCK_COUNT ||
        super->block_count > O0FS_MAX_BLOCK_COUNT ||
        super->block_count > device_blocks) {
        return false;
    }
    if (super->inode_count == 0 ||
        super->inode_count > O0FS_MAX_INODE_COUNT ||
        super->inode_count % O0FS_INODES_PER_BLOCK != 0) {
        return false;
    }
    if (super->inode_table_block != 1 ||
        super->inode_table_blocks == 0) {
        return false;
    }
    /* Overflow-safe: inode_table_blocks * INODES_PER_BLOCK == inode_count. */
    if ((uint64_t)super->inode_table_blocks * O0FS_INODES_PER_BLOCK !=
        super->inode_count) {
        return false;
    }
    if (super->block_bitmap_blocks != 1 || super->inode_bitmap_blocks != 1) {
        return false;
    }
    /* Overflow-safe layout sums. */
    uint64_t block_bitmap_block =
        (uint64_t)super->inode_table_block + super->inode_table_blocks;
    if (block_bitmap_block != super->block_bitmap_block ||
        block_bitmap_block > UINT32_MAX) {
        return false;
    }
    if ((uint64_t)super->block_bitmap_block + 1u != super->inode_bitmap_block) {
        return false;
    }
    if ((uint64_t)super->inode_bitmap_block + 1u != super->data_block ||
        super->data_block >= super->block_count) {
        return false;
    }
    if (super->root_inode == 0 || super->root_inode >= super->inode_count) {
        return false;
    }
    if (super->free_blocks > super->block_count - super->data_block) {
        return false;
    }
    if (super->free_inodes > super->inode_count) {
        return false;
    }
    return true;
}

static o0_status fs_super_flush(struct o0fs *fs) {
    fs->super.checksum = fs_checksum(&fs->super);
    memset(fs->blk, 0, O0FS_BLOCK_SIZE);
    memcpy(fs->blk, &fs->super, sizeof(fs->super));
    return fs_write_block(fs, O0FS_SUPER_BLOCK, fs->blk);
}

/* ---- bitmaps ---- */

static bool fs_bit_get(const uint8_t *bitmap, uint32_t bit) {
    return (bitmap[bit >> 3] >> (bit & 7u)) & 1u;
}

static void fs_bit_set(uint8_t *bitmap, uint32_t bit, bool value) {
    uint8_t mask = (uint8_t)(1u << (bit & 7u));
    if (value) {
        bitmap[bit >> 3] |= mask;
    } else {
        bitmap[bit >> 3] &= (uint8_t)~mask;
    }
}

static o0_status fs_metadata_flush(struct o0fs *fs) {
    o0_status st = fs_write_block(fs, fs->super.block_bitmap_block, fs->bbm);
    if (st != O0_OK) {
        return st;
    }
    return fs_write_block(fs, fs->super.inode_bitmap_block, fs->ibm);
}

/* ---- physical block allocation ---- */

static o0_status fs_block_alloc(struct o0fs *fs, uint32_t *out) {
    for (uint32_t block = fs->super.data_block;
         block < fs->super.block_count; ++block) {
        if (fs_bit_get(fs->bbm, block)) {
            continue;
        }
        fs_bit_set(fs->bbm, block, true);
        if (fs->super.free_blocks > 0) {
            --fs->super.free_blocks;
        }
        o0_status st = fs_write_block(fs, fs->super.block_bitmap_block, fs->bbm);
        if (st != O0_OK) {
            fs_bit_set(fs->bbm, block, false);
            ++fs->super.free_blocks;
            return st;
        }
        memset(fs->zero, 0, O0FS_BLOCK_SIZE);
        st = fs_write_block(fs, block, fs->zero);
        if (st != O0_OK) {
            fs_bit_set(fs->bbm, block, false);
            ++fs->super.free_blocks;
            (void)fs_write_block(fs, fs->super.block_bitmap_block, fs->bbm);
            return st;
        }
        *out = block;
        return O0_OK;
    }
    return O0_ENOSPC;
}

static o0_status fs_block_free(struct o0fs *fs, uint32_t block) {
    if (block < fs->super.data_block || block >= fs->super.block_count) {
        return O0_EINVAL;
    }
    if (!fs_bit_get(fs->bbm, block)) {
        return O0_EINVAL;
    }
    fs_bit_set(fs->bbm, block, false);
    ++fs->super.free_blocks;
    return fs_write_block(fs, fs->super.block_bitmap_block, fs->bbm);
}

/* ---- inodes ---- */

static uint32_t fs_inode_lba(const struct o0fs *fs, uint32_t inode) {
    return fs->super.inode_table_block +
           inode / O0FS_INODES_PER_BLOCK;
}

static uint32_t fs_inode_offset(uint32_t inode) {
    return (inode % O0FS_INODES_PER_BLOCK) * O0FS_INODE_SIZE;
}

static o0_status fs_read_inode(struct o0fs *fs, uint32_t inode,
                               struct o0fs_inode_disk *out) {
    if (inode == 0 || inode >= fs->super.inode_count) {
        return O0_ENOENT;
    }
    o0_status st = fs_read_block(fs, fs_inode_lba(fs, inode), fs->blk);
    if (st != O0_OK) {
        return st;
    }
    memcpy(out, fs->blk + fs_inode_offset(inode), sizeof(*out));
    return O0_OK;
}

static o0_status fs_write_inode(struct o0fs *fs, uint32_t inode,
                                const struct o0fs_inode_disk *inode_data) {
    if (inode == 0 || inode >= fs->super.inode_count) {
        return O0_ENOENT;
    }
    uint32_t lba = fs_inode_lba(fs, inode);
    o0_status st = fs_read_block(fs, lba, fs->blk);
    if (st != O0_OK) {
        return st;
    }
    memcpy(fs->blk + fs_inode_offset(inode), inode_data, sizeof(*inode_data));
    return fs_write_block(fs, lba, fs->blk);
}

static o0_status fs_inode_alloc(struct o0fs *fs, uint16_t mode,
                                uint32_t *out) {
    for (uint32_t inode = O0FS_ROOT_INODE + 1u;
         inode < fs->super.inode_count; ++inode) {
        if (fs_bit_get(fs->ibm, inode)) {
            continue;
        }
        fs_bit_set(fs->ibm, inode, true);
        if (fs->super.free_inodes > 0) {
            --fs->super.free_inodes;
        }
        o0_status st = fs_write_block(fs, fs->super.inode_bitmap_block,
                                      fs->ibm);
        if (st != O0_OK) {
            fs_bit_set(fs->ibm, inode, false);
            ++fs->super.free_inodes;
            return st;
        }
        struct o0fs_inode_disk fresh;
        memset(&fresh, 0, sizeof(fresh));
        fresh.mode = mode;
        fresh.links = 1;
        st = fs_write_inode(fs, inode, &fresh);
        if (st != O0_OK) {
            fs_bit_set(fs->ibm, inode, false);
            ++fs->super.free_inodes;
            (void)fs_write_block(fs, fs->super.inode_bitmap_block, fs->ibm);
            return st;
        }
        *out = inode;
        return O0_OK;
    }
    return O0_ENOSPC;
}

/* Frees every data block with index >= keep_blocks and updates the block count.
 * The inode itself and its bitmap bit are the caller's responsibility. */
static o0_status fs_inode_release_blocks(struct o0fs *fs,
                                         struct o0fs_inode_disk *inode,
                                         uint32_t keep_blocks) {
    uint32_t freed = 0;
    uint32_t direct_end = keep_blocks < O0FS_DIRECT_BLOCKS
                              ? keep_blocks
                              : O0FS_DIRECT_BLOCKS;
    for (uint32_t i = direct_end; i < O0FS_DIRECT_BLOCKS; ++i) {
        if (inode->direct[i] == 0) {
            continue;
        }
        o0_status st = fs_block_free(fs, inode->direct[i]);
        if (st != O0_OK) {
            return st;
        }
        inode->direct[i] = 0;
        ++freed;
    }

    if (inode->indirect == 0) {
        inode->blocks = inode->blocks >= freed ? inode->blocks - freed : 0;
        return O0_OK;
    }

    o0_status st = fs_read_block(fs, inode->indirect, fs->ind);
    if (st != O0_OK) {
        return st;
    }
    uint32_t *entries = (uint32_t *)fs->ind;
    uint32_t first = keep_blocks > O0FS_DIRECT_BLOCKS
                         ? keep_blocks - O0FS_DIRECT_BLOCKS
                         : 0;
    for (uint32_t e = first; e < O0FS_INDIRECT_ENTRIES; ++e) {
        if (entries[e] == 0) {
            continue;
        }
        st = fs_block_free(fs, entries[e]);
        if (st != O0_OK) {
            return st;
        }
        entries[e] = 0;
        ++freed;
    }
    if (keep_blocks <= O0FS_DIRECT_BLOCKS) {
        st = fs_block_free(fs, inode->indirect);
        if (st != O0_OK) {
            return st;
        }
        inode->indirect = 0;
        ++freed;
    } else {
        st = fs_write_block(fs, inode->indirect, fs->ind);
        if (st != O0_OK) {
            return st;
        }
    }

    inode->blocks = inode->blocks >= freed ? inode->blocks - freed : 0;
    return O0_OK;
}

static o0_status fs_inode_release(struct o0fs *fs, uint32_t inode) {
    struct o0fs_inode_disk data;
    o0_status st = fs_read_inode(fs, inode, &data);
    if (st != O0_OK) {
        return st;
    }
    st = fs_inode_release_blocks(fs, &data, 0);
    if (st != O0_OK) {
        return st;
    }
    memset(&data, 0, sizeof(data));
    st = fs_write_inode(fs, inode, &data);
    if (st != O0_OK) {
        return st;
    }
    fs_bit_set(fs->ibm, inode, false);
    if (fs->super.free_inodes < fs->super.inode_count) {
        ++fs->super.free_inodes;
    }
    return fs_write_block(fs, fs->super.inode_bitmap_block, fs->ibm);
}

/* Resolves a logical file block index to a physical block, optionally
 * allocating direct, indirect and data blocks. A hole is reported as a zero
 * index with no error. */
static o0_status fs_bmap(struct o0fs *fs, struct o0fs_inode_disk *inode,
                         uint32_t index, bool allocate, uint32_t *out_block,
                         bool *out_created) {
    *out_block = 0;
    *out_created = false;
    if (index >= O0FS_MAX_FILE_BLOCKS) {
        return O0_ERANGE;
    }

    if (index < O0FS_DIRECT_BLOCKS) {
        uint32_t block = inode->direct[index];
        if (block == 0 && allocate) {
            o0_status st = fs_block_alloc(fs, &block);
            if (st != O0_OK) {
                return st;
            }
            inode->direct[index] = block;
            ++inode->blocks;
            *out_created = true;
        }
        *out_block = block;
        return O0_OK;
    }

    uint32_t entry = index - O0FS_DIRECT_BLOCKS;
    if (inode->indirect == 0) {
        if (!allocate) {
            return O0_OK;
        }
        uint32_t block = 0;
        o0_status st = fs_block_alloc(fs, &block);
        if (st != O0_OK) {
            return st;
        }
        inode->indirect = block;
        ++inode->blocks;
    }

    o0_status st = fs_read_block(fs, inode->indirect, fs->ind);
    if (st != O0_OK) {
        return st;
    }
    uint32_t *entries = (uint32_t *)fs->ind;
    uint32_t block = entries[entry];
    if (block == 0 && allocate) {
        st = fs_block_alloc(fs, &block);
        if (st != O0_OK) {
            return st;
        }
        entries[entry] = block;
        ++inode->blocks;
        st = fs_write_block(fs, inode->indirect, fs->ind);
        if (st != O0_OK) {
            return st;
        }
        *out_created = true;
    }
    *out_block = block;
    return O0_OK;
}

/* ---- directory entries ---- */

static o0_status fs_dir_lookup(struct o0fs *fs, uint32_t dir_inode,
                               const char *name, uint32_t *out_inode) {
    struct o0fs_inode_disk dir;
    o0_status st = fs_read_inode(fs, dir_inode, &dir);
    if (st != O0_OK) {
        return st;
    }
    if (dir.mode != O0FS_TYPE_DIR) {
        return O0_ENOTDIR;
    }
    if (dir.size % O0FS_BLOCK_SIZE != 0) {
        return O0_EIO;
    }

    size_t name_len = strlen(name);
    uint32_t blocks = dir.size / O0FS_BLOCK_SIZE;
    for (uint32_t i = 0; i < blocks; ++i) {
        uint32_t block = 0;
        bool created = false;
        st = fs_bmap(fs, &dir, i, false, &block, &created);
        if (st != O0_OK) {
            return st;
        }
        if (block == 0) {
            continue;
        }
        st = fs_read_block(fs, block, fs->dir);
        if (st != O0_OK) {
            return st;
        }
        for (uint32_t off = 0; off < O0FS_BLOCK_SIZE;
             off += O0FS_DIRENT_SIZE) {
            const struct o0fs_dirent_disk *entry =
                (const struct o0fs_dirent_disk *)(fs->dir + off);
            if (entry->inode == 0) {
                continue;
            }
            if (entry->rec_len != O0FS_DIRENT_SIZE ||
                entry->name_len > O0FS_NAME_MAX) {
                return O0_EIO;
            }
            if (entry->name_len != name_len) {
                continue;
            }
            if (memcmp(entry->name, name, name_len) == 0) {
                *out_inode = entry->inode;
                return O0_OK;
            }
        }
    }
    return O0_ENOENT;
}

static o0_status fs_dir_add(struct o0fs *fs, uint32_t dir_inode,
                            const char *name, uint32_t target, uint8_t type) {
    struct o0fs_inode_disk dir;
    o0_status st = fs_read_inode(fs, dir_inode, &dir);
    if (st != O0_OK) {
        return st;
    }
    if (dir.mode != O0FS_TYPE_DIR) {
        return O0_ENOTDIR;
    }
    if (dir.size % O0FS_BLOCK_SIZE != 0) {
        return O0_EIO;
    }
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > O0FS_NAME_MAX) {
        return O0_EINVAL;
    }

    uint32_t blocks = dir.size / O0FS_BLOCK_SIZE;
    for (uint32_t i = 0; i < blocks; ++i) {
        uint32_t block = 0;
        bool created = false;
        st = fs_bmap(fs, &dir, i, false, &block, &created);
        if (st != O0_OK) {
            return st;
        }
        if (block == 0) {
            continue;
        }
        st = fs_read_block(fs, block, fs->dir);
        if (st != O0_OK) {
            return st;
        }
        for (uint32_t off = 0; off < O0FS_BLOCK_SIZE;
             off += O0FS_DIRENT_SIZE) {
            struct o0fs_dirent_disk *entry =
                (struct o0fs_dirent_disk *)(fs->dir + off);
            if (entry->inode != 0) {
                continue;
            }
            memset(entry, 0, sizeof(*entry));
            entry->inode = target;
            entry->rec_len = O0FS_DIRENT_SIZE;
            entry->name_len = (uint8_t)name_len;
            entry->type = type;
            memcpy(entry->name, name, name_len);
            return fs_write_block(fs, block, fs->dir);
        }
    }

    uint32_t block = 0;
    bool created = false;
    st = fs_bmap(fs, &dir, blocks, true, &block, &created);
    if (st != O0_OK) {
        return st;
    }
    memset(fs->dir, 0, O0FS_BLOCK_SIZE);
    struct o0fs_dirent_disk *entry =
        (struct o0fs_dirent_disk *)fs->dir;
    entry->inode = target;
    entry->rec_len = O0FS_DIRENT_SIZE;
    entry->name_len = (uint8_t)name_len;
    entry->type = type;
    memcpy(entry->name, name, name_len);
    st = fs_write_block(fs, block, fs->dir);
    if (st != O0_OK) {
        return st;
    }
    dir.size += O0FS_BLOCK_SIZE;
    return fs_write_inode(fs, dir_inode, &dir);
}

static o0_status fs_dir_remove(struct o0fs *fs, uint32_t dir_inode,
                               const char *name) {
    struct o0fs_inode_disk dir;
    o0_status st = fs_read_inode(fs, dir_inode, &dir);
    if (st != O0_OK) {
        return st;
    }
    if (dir.mode != O0FS_TYPE_DIR) {
        return O0_ENOTDIR;
    }
    if (dir.size % O0FS_BLOCK_SIZE != 0) {
        return O0_EIO;
    }

    size_t name_len = strlen(name);
    uint32_t blocks = dir.size / O0FS_BLOCK_SIZE;
    for (uint32_t i = 0; i < blocks; ++i) {
        uint32_t block = 0;
        bool created = false;
        st = fs_bmap(fs, &dir, i, false, &block, &created);
        if (st != O0_OK) {
            return st;
        }
        if (block == 0) {
            continue;
        }
        st = fs_read_block(fs, block, fs->dir);
        if (st != O0_OK) {
            return st;
        }
        for (uint32_t off = 0; off < O0FS_BLOCK_SIZE;
             off += O0FS_DIRENT_SIZE) {
            struct o0fs_dirent_disk *entry =
                (struct o0fs_dirent_disk *)(fs->dir + off);
            if (entry->inode == 0 || entry->name_len != name_len) {
                continue;
            }
            if (memcmp(entry->name, name, name_len) != 0) {
                continue;
            }
            memset(entry, 0, sizeof(*entry));
            return fs_write_block(fs, block, fs->dir);
        }
    }
    return O0_ENOENT;
}

static o0_status fs_dir_empty(struct o0fs *fs, uint32_t dir_inode,
                              bool *out_empty) {
    struct o0fs_inode_disk dir;
    o0_status st = fs_read_inode(fs, dir_inode, &dir);
    if (st != O0_OK) {
        return st;
    }
    if (dir.mode != O0FS_TYPE_DIR) {
        return O0_ENOTDIR;
    }
    if (dir.size % O0FS_BLOCK_SIZE != 0) {
        return O0_EIO;
    }

    uint32_t blocks = dir.size / O0FS_BLOCK_SIZE;
    for (uint32_t i = 0; i < blocks; ++i) {
        uint32_t block = 0;
        bool created = false;
        st = fs_bmap(fs, &dir, i, false, &block, &created);
        if (st != O0_OK) {
            return st;
        }
        if (block == 0) {
            continue;
        }
        st = fs_read_block(fs, block, fs->dir);
        if (st != O0_OK) {
            return st;
        }
        for (uint32_t off = 0; off < O0FS_BLOCK_SIZE;
             off += O0FS_DIRENT_SIZE) {
            const struct o0fs_dirent_disk *entry =
                (const struct o0fs_dirent_disk *)(fs->dir + off);
            if (entry->inode != 0) {
                *out_empty = false;
                return O0_OK;
            }
        }
    }
    *out_empty = true;
    return O0_OK;
}

/* ---- data path ---- */

static o0_status fs_read_range(struct o0fs *fs, struct o0fs_inode_disk *inode,
                               uint64_t offset, void *buffer, size_t capacity,
                               size_t *out_len) {
    *out_len = 0;
    if (buffer == NULL && capacity != 0) {
        return O0_EINVAL;
    }
    if (offset >= inode->size) {
        return O0_OK;
    }
    uint64_t available = inode->size - offset;
    size_t want = capacity < available ? capacity : (size_t)available;
    uint8_t *out = (uint8_t *)buffer;
    size_t done = 0;
    while (done < want) {
        uint64_t position = offset + done;
        uint32_t index = (uint32_t)(position / O0FS_BLOCK_SIZE);
        uint32_t inner = (uint32_t)(position % O0FS_BLOCK_SIZE);
        size_t chunk = O0FS_BLOCK_SIZE - inner;
        if (chunk > want - done) {
            chunk = want - done;
        }
        uint32_t block = 0;
        bool created = false;
        o0_status st = fs_bmap(fs, inode, index, false, &block, &created);
        if (st != O0_OK) {
            return st;
        }
        if (block == 0) {
            memset(out + done, 0, chunk);
        } else {
            st = fs_read_block(fs, block, fs->blk);
            if (st != O0_OK) {
                return st;
            }
            memcpy(out + done, fs->blk + inner, chunk);
        }
        done += chunk;
    }
    *out_len = want;
    return O0_OK;
}

/* buf == NULL writes zeros. The gap between the old size and offset is
 * zero-filled by one recursive call whose offset equals the size, so the
 * recursion depth is bounded at one. */
static o0_status fs_write_range(struct o0fs *fs, struct o0fs_inode_disk *inode,
                                uint64_t offset, const uint8_t *buf, size_t len) {
    if (len == 0) {
        return O0_OK;
    }
    if (offset > inode->size) {
        o0_status st = fs_write_range(fs, inode, inode->size, NULL,
                                      (size_t)(offset - inode->size));
        if (st != O0_OK) {
            return st;
        }
    }
    if (offset + len > O0FS_MAX_FILE_BYTES) {
        return O0_ERANGE;
    }

    size_t done = 0;
    while (done < len) {
        uint64_t position = offset + done;
        uint32_t index = (uint32_t)(position / O0FS_BLOCK_SIZE);
        uint32_t inner = (uint32_t)(position % O0FS_BLOCK_SIZE);
        size_t chunk = O0FS_BLOCK_SIZE - inner;
        if (chunk > len - done) {
            chunk = len - done;
        }
        uint32_t block = 0;
        bool created = false;
        o0_status st = fs_bmap(fs, inode, index, true, &block, &created);
        if (st != O0_OK) {
            return st;
        }
        if (created) {
            memset(fs->blk, 0, O0FS_BLOCK_SIZE);
        } else {
            st = fs_read_block(fs, block, fs->blk);
            if (st != O0_OK) {
                return st;
            }
        }
        if (buf != NULL) {
            memcpy(fs->blk + inner, buf + done, chunk);
        } else {
            memset(fs->blk + inner, 0, chunk);
        }
        st = fs_write_block(fs, block, fs->blk);
        if (st != O0_OK) {
            return st;
        }
        done += chunk;
    }

    if (offset + len > inode->size) {
        inode->size = (uint32_t)(offset + len);
    }
    return O0_OK;
}

/*
 * Writes are staged with an undo image for every already-allocated data block
 * the operation may touch. This is not a journal and cannot survive power loss,
 * but it lets the live filesystem restore old contents after a reported I/O
 * failure, provided the device accepts the rollback writes.
 */
static void fs_write_backups_release(struct o0fs *fs) {
    for (uint32_t i = 0; i < fs->write_backup_count; ++i) {
        fs_page_free(fs->write_backups[i].page);
        fs->write_backups[i].page = NULL;
        fs->write_backups[i].block = 0;
    }
    fs->write_backup_count = 0;
}

static o0_status fs_write_checked(struct o0fs *fs, uint32_t inode_no,
                                  struct o0fs_inode_disk *inode,
                                  uint64_t offset, const uint8_t *buffer,
                                  size_t len) {
    if (len == 0) {
        return O0_OK;
    }
    if (offset > O0FS_MAX_FILE_BYTES ||
        (uint64_t)len > O0FS_MAX_FILE_BYTES - offset) {
        return O0_ERANGE;
    }

    struct o0fs_inode_disk snapshot = *inode;
    fs_write_backups_release(fs);

    uint64_t first_byte = offset < snapshot.size ? offset : snapshot.size;
    uint64_t end_byte = offset + len;
    uint32_t first_block = (uint32_t)(first_byte / O0FS_BLOCK_SIZE);
    uint32_t last_block = (uint32_t)((end_byte - 1u) / O0FS_BLOCK_SIZE);

    /*
     * Capture all old blocks before the first mutation. The page-backed undo
     * set is bounded by O0FS_MAX_FILE_BLOCKS and allocation failure is safe:
     * no disk contents have changed yet.
     */
    for (uint32_t index = first_block; index <= last_block; ++index) {
        uint32_t block = 0;
        bool created = false;
        o0_status st = fs_bmap(fs, &snapshot, index, false, &block, &created);
        if (st != O0_OK) {
            fs_write_backups_release(fs);
            return st;
        }
        if (block == 0) {
            continue;
        }
        void *backup = fs_page_alloc();
        if (backup == NULL) {
            fs_write_backups_release(fs);
            return O0_ENOMEM;
        }
        st = fs_read_block(fs, block, backup);
        if (st != O0_OK) {
            fs_page_free(backup);
            fs_write_backups_release(fs);
            return st;
        }
        uint32_t slot = fs->write_backup_count++;
        fs->write_backups[slot].block = block;
        fs->write_backups[slot].page = backup;
    }

    o0_status st = fs_write_range(fs, inode, offset, buffer, len);
    if (st == O0_OK) {
        st = fs_write_inode(fs, inode_no, inode);
        if (st == O0_OK) {
            fs_write_backups_release(fs);
            return O0_OK;
        }
    }

    o0_status rollback_status = O0_OK;
    for (uint32_t i = 0; i < fs->write_backup_count; ++i) {
        o0_status restore_st =
            fs_write_block(fs, fs->write_backups[i].block,
                           fs->write_backups[i].page);
        if (restore_st != O0_OK) {
            rollback_status = O0_EIO;
        }
    }

    struct o0fs_inode_disk rollback = *inode;
    uint32_t keep =
        (snapshot.size + O0FS_BLOCK_SIZE - 1u) / O0FS_BLOCK_SIZE;
    if (fs_inode_release_blocks(fs, &rollback, keep) != O0_OK) {
        rollback_status = O0_EIO;
    }
    if (fs_write_inode(fs, inode_no, &snapshot) != O0_OK) {
        rollback_status = O0_EIO;
    }
    *inode = snapshot;
    fs_write_backups_release(fs);
    return rollback_status == O0_OK ? st : O0_EIO;
}

/* ---- path resolution ---- */

static o0_status fs_lookup_path(struct o0fs *fs, const char *path,
                                uint32_t *inode_out) {
    if (path == NULL || inode_out == NULL) {
        return O0_EINVAL;
    }
    size_t length = strlen(path);
    if (length == 0 || length > O0FS_PATH_MAX || path[0] != '/') {
        return O0_EINVAL;
    }

    uint32_t current = fs->super.root_inode;
    const char *cursor = path;
    uint32_t depth = 0;
    while (*cursor != '\0') {
        while (*cursor == '/') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        char name[O0FS_NAME_MAX + 1u];
        size_t name_len = 0;
        while (*cursor != '\0' && *cursor != '/') {
            if (name_len >= O0FS_NAME_MAX) {
                return O0_EINVAL;
            }
            name[name_len++] = *cursor++;
        }
        name[name_len] = '\0';
        if (++depth > O0FS_MAX_DEPTH) {
            return O0_EINVAL;
        }
        if (name_len == 1 && name[0] == '.') {
            return O0_EINVAL;
        }
        if (name_len == 2 && name[0] == '.' && name[1] == '.') {
            return O0_EINVAL;
        }
        uint32_t next = 0;
        o0_status st = fs_dir_lookup(fs, current, name, &next);
        if (st != O0_OK) {
            return st;
        }
        current = next;
    }
    *inode_out = current;
    return O0_OK;
}

/* Splits path into the parent directory inode and the final component. */
static o0_status fs_parent_path(struct o0fs *fs, const char *path,
                                uint32_t *parent_out, char *name,
                                size_t name_capacity) {
    if (path == NULL || parent_out == NULL || name == NULL) {
        return O0_EINVAL;
    }
    size_t length = strlen(path);
    if (length == 0 || length > O0FS_PATH_MAX || path[0] != '/') {
        return O0_EINVAL;
    }
    while (length > 1 && path[length - 1] == '/') {
        --length;
    }
    if (length == 1) {
        return O0_EINVAL; /* the root has no parent entry */
    }

    size_t slash = length;
    while (slash > 0 && path[slash - 1] != '/') {
        --slash;
    }
    size_t name_len = length - slash;
    if (name_len == 0 || name_len > O0FS_NAME_MAX || name_len + 1 > name_capacity) {
        return O0_EINVAL;
    }
    memcpy(name, path + slash, name_len);
    name[name_len] = '\0';
    if ((name_len == 1 && name[0] == '.') ||
        (name_len == 2 && name[0] == '.' && name[1] == '.')) {
        return O0_EINVAL;
    }

    if (slash == 1) {
        *parent_out = fs->super.root_inode;
        return O0_OK;
    }

    char prefix[O0FS_PATH_MAX + 1u];
    size_t prefix_len = slash - 1; /* drop the separating slash */
    memcpy(prefix, path, prefix_len);
    prefix[prefix_len] = '\0';
    return fs_lookup_path(fs, prefix, parent_out);
}

static o0_status fs_parent_dir(struct o0fs *fs, const char *path,
                               uint32_t *parent_out, char *name,
                               size_t name_capacity) {
    uint32_t parent = 0;
    o0_status st = fs_parent_path(fs, path, &parent, name, name_capacity);
    if (st != O0_OK) {
        return st;
    }
    struct o0fs_inode_disk inode;
    st = fs_read_inode(fs, parent, &inode);
    if (st != O0_OK) {
        return st;
    }
    if (inode.mode != O0FS_TYPE_DIR) {
        return O0_ENOTDIR;
    }
    *parent_out = parent;
    return O0_OK;
}

/* ---- format, mount, sync ---- */

static bool fs_device_ok(const struct o0_block_dev *dev) {
    return dev != NULL && dev->geo.logical_block_size == O0FS_BLOCK_SIZE;
}

o0_status o0fs_format(struct o0_block_dev *dev) {
    if (!fs_device_ok(dev)) {
        return dev == NULL ? O0_EINVAL : O0_ENOTSUP;
    }
    if (dev->geo.nr_sectors > O0FS_MAX_BLOCK_COUNT) {
        return O0_ERANGE;
    }
    if (dev->geo.nr_sectors < O0FS_MIN_BLOCK_COUNT) {
        return O0_ENOSPC;
    }

    uint32_t block_count = (uint32_t)dev->geo.nr_sectors;
    uint32_t inode_blocks = block_count / 16u;
    if (inode_blocks < 1) {
        inode_blocks = 1;
    }
    if (inode_blocks > 16) {
        inode_blocks = 16;
    }
    uint32_t inode_count = inode_blocks * O0FS_INODES_PER_BLOCK;

    void *scratch = fs_page_alloc();
    if (scratch == NULL) {
        return O0_ENOMEM;
    }

    struct o0fs_super_disk super;
    memset(&super, 0, sizeof(super));
    super.magic = O0FS_MAGIC;
    super.version = O0FS_VERSION;
    super.block_size = O0FS_BLOCK_SIZE;
    super.block_count = block_count;
    super.inode_count = inode_count;
    super.inode_table_block = 1;
    super.inode_table_blocks = inode_blocks;
    super.block_bitmap_block = 1 + inode_blocks;
    super.block_bitmap_blocks = 1;
    super.inode_bitmap_block = super.block_bitmap_block + 1;
    super.inode_bitmap_blocks = 1;
    super.data_block = super.inode_bitmap_block + 1;
    super.root_inode = O0FS_ROOT_INODE;
    super.free_blocks = block_count - super.data_block;
    super.free_inodes = inode_count - 2; /* inode 0 is reserved, 1 is root */
    super.flags = 0;

    o0_status st = O0_OK;
    memset(scratch, 0, O0FS_BLOCK_SIZE);
    super.checksum = fs_checksum(&super);
    memcpy(scratch, &super, sizeof(super));
    st = o0_block_write(dev, O0FS_SUPER_BLOCK, scratch, O0FS_BLOCK_SIZE);
    if (st != O0_OK) {
        fs_page_free(scratch);
        return st;
    }

    /* Zero the inode table, then place the root inode in the first entry. */
    for (uint32_t i = 0; i < inode_blocks; ++i) {
        memset(scratch, 0, O0FS_BLOCK_SIZE);
        if (i == 0) {
            struct o0fs_inode_disk root;
            memset(&root, 0, sizeof(root));
            root.mode = O0FS_TYPE_DIR;
            root.links = 1;
            memcpy((uint8_t *)scratch + fs_inode_offset(O0FS_ROOT_INODE), &root,
                   sizeof(root));
        }
        st = o0_block_write(dev, super.inode_table_block + i, scratch,
                            O0FS_BLOCK_SIZE);
        if (st != O0_OK) {
            fs_page_free(scratch);
            return st;
        }
    }

    /* Both bitmaps use one block; mark metadata and the root inode as used. */
    memset(scratch, 0, O0FS_BLOCK_SIZE);
    for (uint32_t block = 0; block < super.data_block; ++block) {
        fs_bit_set((uint8_t *)scratch, block, true);
    }
    st = o0_block_write(dev, super.block_bitmap_block, scratch,
                        O0FS_BLOCK_SIZE);
    if (st != O0_OK) {
        fs_page_free(scratch);
        return st;
    }

    memset(scratch, 0, O0FS_BLOCK_SIZE);
    fs_bit_set((uint8_t *)scratch, 0, true);
    fs_bit_set((uint8_t *)scratch, O0FS_ROOT_INODE, true);
    st = o0_block_write(dev, super.inode_bitmap_block, scratch,
                        O0FS_BLOCK_SIZE);
    fs_page_free(scratch);
    return st;
}

o0_status o0fs_mount(struct o0_block_dev *dev, struct o0fs **out) {
    if (out == NULL) {
        return O0_EINVAL;
    }
    *out = NULL;
    if (!fs_device_ok(dev)) {
        return dev == NULL ? O0_EINVAL : O0_ENOTSUP;
    }

    struct o0fs *fs = NULL;
    for (size_t i = 0; i < O0FS_MAX_MOUNTS; ++i) {
        if (!mounts[i].mounted) {
            fs = &mounts[i];
            break;
        }
    }
    if (fs == NULL) {
        return O0_EBUSY;
    }
    memset(fs, 0, sizeof(*fs));
    fs->dev = dev;

    fs->blk = fs_page_alloc();
    fs->dir = fs_page_alloc();
    fs->ind = fs_page_alloc();
    fs->zero = fs_page_alloc();
    fs->bbm = fs_page_alloc();
    fs->ibm = fs_page_alloc();
    if (fs->blk == NULL || fs->dir == NULL || fs->ind == NULL ||
        fs->zero == NULL || fs->bbm == NULL || fs->ibm == NULL) {
        goto fail;
    }

    o0_status st = fs_read_block(fs, O0FS_SUPER_BLOCK, fs->blk);
    if (st != O0_OK) {
        goto fail_status;
    }
    memcpy(&fs->super, fs->blk, sizeof(fs->super));
    if (!fs_super_valid(&fs->super, dev->geo.nr_sectors)) {
        st = O0_EINVAL;
        goto fail_status;
    }

    st = fs_read_block(fs, fs->super.block_bitmap_block, fs->bbm);
    if (st != O0_OK) {
        goto fail_status;
    }
    st = fs_read_block(fs, fs->super.inode_bitmap_block, fs->ibm);
    if (st != O0_OK) {
        goto fail_status;
    }

    struct o0fs_inode_disk root;
    st = fs_read_inode(fs, fs->super.root_inode, &root);
    if (st != O0_OK) {
        goto fail_status;
    }
    if (root.mode != O0FS_TYPE_DIR) {
        st = O0_EINVAL;
        goto fail_status;
    }

    fs->mounted = true;
    *out = fs;
    return O0_OK;

fail_status:
    fs_page_free(fs->blk);
    fs_page_free(fs->dir);
    fs_page_free(fs->ind);
    fs_page_free(fs->zero);
    fs_page_free(fs->bbm);
    fs_page_free(fs->ibm);
    memset(fs, 0, sizeof(*fs));
    return st;

fail:
    fs_page_free(fs->blk);
    fs_page_free(fs->dir);
    fs_page_free(fs->ind);
    fs_page_free(fs->zero);
    fs_page_free(fs->bbm);
    fs_page_free(fs->ibm);
    memset(fs, 0, sizeof(*fs));
    return O0_ENOMEM;
}

o0_status o0fs_sync(struct o0fs *fs) {
    if (fs == NULL || !fs->mounted) {
        return O0_EINVAL;
    }
    o0_status st = fs_metadata_flush(fs);
    if (st != O0_OK) {
        return st;
    }
    st = fs_super_flush(fs);
    if (st != O0_OK) {
        return st;
    }
    return o0_block_flush(fs->dev);
}

o0_status o0fs_unmount(struct o0fs *fs) {
    if (fs == NULL || !fs->mounted) {
        return O0_EINVAL;
    }
    o0_status st = o0fs_sync(fs);
    fs_page_free(fs->blk);
    fs_page_free(fs->dir);
    fs_page_free(fs->ind);
    fs_page_free(fs->zero);
    fs_page_free(fs->bbm);
    fs_page_free(fs->ibm);
    memset(fs, 0, sizeof(*fs));
    return st;
}

/* ---- public operations ---- */

static void fs_fill_stat(uint32_t inode, const struct o0fs_inode_disk *data,
                         struct o0fs_stat *out) {
    out->inode = inode;
    out->type = data->mode;
    out->links = data->links;
    out->blocks = data->blocks;
    out->size = data->size;
}

o0_status o0fs_stat(struct o0fs *fs, const char *path,
                    struct o0fs_stat *out) {
    if (fs == NULL || !fs->mounted || out == NULL) {
        return O0_EINVAL;
    }
    uint32_t inode = 0;
    o0_status st = fs_lookup_path(fs, path, &inode);
    if (st != O0_OK) {
        return st;
    }
    struct o0fs_inode_disk data;
    st = fs_read_inode(fs, inode, &data);
    if (st != O0_OK) {
        return st;
    }
    fs_fill_stat(inode, &data, out);
    return O0_OK;
}

o0_status o0fs_lookup(struct o0fs *fs, const char *path,
                      uint32_t *inode_out) {
    if (fs == NULL || !fs->mounted) {
        return O0_EINVAL;
    }
    return fs_lookup_path(fs, path, inode_out);
}

static o0_status fs_create(struct o0fs *fs, const char *path, uint16_t mode) {
    uint32_t parent = 0;
    char name[O0FS_NAME_MAX + 1u];
    o0_status st = fs_parent_dir(fs, path, &parent, name, sizeof(name));
    if (st != O0_OK) {
        return st;
    }

    uint32_t existing = 0;
    o0_status lookup_st = fs_dir_lookup(fs, parent, name, &existing);
    if (lookup_st == O0_OK) {
        return O0_EEXIST;
    }
    if (lookup_st != O0_ENOENT) {
        return lookup_st;
    }

    uint32_t inode = 0;
    st = fs_inode_alloc(fs, mode, &inode);
    if (st != O0_OK) {
        return st;
    }
    st = fs_dir_add(fs, parent, name, inode, (uint8_t)mode);
    if (st != O0_OK) {
        (void)fs_inode_release(fs, inode);
        return st;
    }
    return O0_OK;
}

o0_status o0fs_create_file(struct o0fs *fs, const char *path) {
    if (fs == NULL || !fs->mounted) {
        return O0_EINVAL;
    }
    return fs_create(fs, path, O0FS_TYPE_FILE);
}

o0_status o0fs_mkdir(struct o0fs *fs, const char *path) {
    if (fs == NULL || !fs->mounted) {
        return O0_EINVAL;
    }
    return fs_create(fs, path, O0FS_TYPE_DIR);
}

o0_status o0fs_read(struct o0fs *fs, const char *path, uint64_t offset,
                    void *buffer, size_t capacity, size_t *out_len) {
    if (fs == NULL || !fs->mounted || out_len == NULL) {
        return O0_EINVAL;
    }
    uint32_t inode_no = 0;
    o0_status st = fs_lookup_path(fs, path, &inode_no);
    if (st != O0_OK) {
        return st;
    }
    struct o0fs_inode_disk inode;
    st = fs_read_inode(fs, inode_no, &inode);
    if (st != O0_OK) {
        return st;
    }
    if (inode.mode != O0FS_TYPE_FILE) {
        return O0_EISDIR;
    }
    return fs_read_range(fs, &inode, offset, buffer, capacity, out_len);
}

o0_status o0fs_write(struct o0fs *fs, const char *path, uint64_t offset,
                     const void *buffer, size_t len, size_t *out_len) {
    if (fs == NULL || !fs->mounted || out_len == NULL) {
        return O0_EINVAL;
    }
    *out_len = 0;
    if (buffer == NULL && len != 0) {
        return O0_EINVAL;
    }
    uint32_t inode_no = 0;
    o0_status st = fs_lookup_path(fs, path, &inode_no);
    if (st != O0_OK) {
        return st;
    }
    struct o0fs_inode_disk inode;
    st = fs_read_inode(fs, inode_no, &inode);
    if (st != O0_OK) {
        return st;
    }
    if (inode.mode != O0FS_TYPE_FILE) {
        return O0_EISDIR;
    }
    if (offset > O0FS_MAX_FILE_BYTES ||
        (uint64_t)len > O0FS_MAX_FILE_BYTES - offset) {
        return O0_ERANGE;
    }
    st = fs_write_checked(fs, inode_no, &inode, offset,
                          (const uint8_t *)buffer, len);
    if (st != O0_OK) {
        return st;
    }
    *out_len = len;
    return O0_OK;
}

o0_status o0fs_truncate(struct o0fs *fs, const char *path, uint64_t size) {
    if (fs == NULL || !fs->mounted) {
        return O0_EINVAL;
    }
    uint32_t inode_no = 0;
    o0_status st = fs_lookup_path(fs, path, &inode_no);
    if (st != O0_OK) {
        return st;
    }
    struct o0fs_inode_disk inode;
    st = fs_read_inode(fs, inode_no, &inode);
    if (st != O0_OK) {
        return st;
    }
    if (inode.mode != O0FS_TYPE_FILE) {
        return O0_EISDIR;
    }
    if (size > O0FS_MAX_FILE_BYTES) {
        return O0_ERANGE;
    }

    if (size < inode.size) {
        uint32_t keep = (uint32_t)((size + O0FS_BLOCK_SIZE - 1u) /
                                   O0FS_BLOCK_SIZE);
        st = fs_inode_release_blocks(fs, &inode, keep);
        if (st != O0_OK) {
            /* Some blocks may already be freed. Writing the partially updated
             * inode keeps it consistent with the bitmap; the old size still
             * describes the freed range as holes. There is no journal, so a
             * metadata I/O error during truncate is not rolled back. */
            (void)fs_write_inode(fs, inode_no, &inode);
            return st;
        }
        inode.size = (uint32_t)size;
        return fs_write_inode(fs, inode_no, &inode);
    }
    if (size > inode.size) {
        return fs_write_checked(fs, inode_no, &inode, inode.size, NULL,
                                (size_t)(size - inode.size));
    }
    return O0_OK;
}

o0_status o0fs_unlink(struct o0fs *fs, const char *path) {
    if (fs == NULL || !fs->mounted) {
        return O0_EINVAL;
    }
    uint32_t parent = 0;
    char name[O0FS_NAME_MAX + 1u];
    o0_status st = fs_parent_dir(fs, path, &parent, name, sizeof(name));
    if (st != O0_OK) {
        return st;
    }
    uint32_t inode = 0;
    st = fs_dir_lookup(fs, parent, name, &inode);
    if (st != O0_OK) {
        return st;
    }
    struct o0fs_inode_disk data;
    st = fs_read_inode(fs, inode, &data);
    if (st != O0_OK) {
        return st;
    }
    if (data.mode == O0FS_TYPE_DIR) {
        return O0_EISDIR;
    }
    st = fs_dir_remove(fs, parent, name);
    if (st != O0_OK) {
        return st;
    }
    return fs_inode_release(fs, inode);
}

o0_status o0fs_rmdir(struct o0fs *fs, const char *path) {
    if (fs == NULL || !fs->mounted) {
        return O0_EINVAL;
    }
    uint32_t parent = 0;
    char name[O0FS_NAME_MAX + 1u];
    o0_status st = fs_parent_dir(fs, path, &parent, name, sizeof(name));
    if (st != O0_OK) {
        return st;
    }
    uint32_t inode = 0;
    st = fs_dir_lookup(fs, parent, name, &inode);
    if (st != O0_OK) {
        return st;
    }
    if (inode == fs->super.root_inode) {
        return O0_EPERM;
    }
    struct o0fs_inode_disk data;
    st = fs_read_inode(fs, inode, &data);
    if (st != O0_OK) {
        return st;
    }
    if (data.mode != O0FS_TYPE_DIR) {
        return O0_ENOTDIR;
    }
    bool empty = false;
    st = fs_dir_empty(fs, inode, &empty);
    if (st != O0_OK) {
        return st;
    }
    if (!empty) {
        return O0_ENOTEMPTY;
    }
    st = fs_dir_remove(fs, parent, name);
    if (st != O0_OK) {
        return st;
    }
    return fs_inode_release(fs, inode);
}

o0_status o0fs_list(struct o0fs *fs, const char *path, o0fs_iter_fn fn,
                    void *ctx) {
    if (fs == NULL || !fs->mounted || fn == NULL) {
        return O0_EINVAL;
    }
    uint32_t dir_no = 0;
    o0_status st = fs_lookup_path(fs, path, &dir_no);
    if (st != O0_OK) {
        return st;
    }
    struct o0fs_inode_disk dir;
    st = fs_read_inode(fs, dir_no, &dir);
    if (st != O0_OK) {
        return st;
    }
    if (dir.mode != O0FS_TYPE_DIR) {
        return O0_ENOTDIR;
    }
    if (dir.size % O0FS_BLOCK_SIZE != 0) {
        return O0_EIO;
    }

    uint32_t blocks = dir.size / O0FS_BLOCK_SIZE;
    for (uint32_t i = 0; i < blocks; ++i) {
        uint32_t block = 0;
        bool created = false;
        st = fs_bmap(fs, &dir, i, false, &block, &created);
        if (st != O0_OK) {
            return st;
        }
        if (block == 0) {
            continue;
        }
        st = fs_read_block(fs, block, fs->dir);
        if (st != O0_OK) {
            return st;
        }
        for (uint32_t off = 0; off < O0FS_BLOCK_SIZE;
             off += O0FS_DIRENT_SIZE) {
            const struct o0fs_dirent_disk *entry =
                (const struct o0fs_dirent_disk *)(fs->dir + off);
            if (entry->inode == 0) {
                continue;
            }
            if (entry->rec_len != O0FS_DIRENT_SIZE ||
                entry->name_len > O0FS_NAME_MAX) {
                return O0_EIO;
            }
            struct o0fs_inode_disk data;
            st = fs_read_inode(fs, entry->inode, &data);
            if (st != O0_OK) {
                return st;
            }
            struct o0fs_stat info;
            fs_fill_stat(entry->inode, &data, &info);
            char name[O0FS_NAME_MAX + 1u];
            memcpy(name, entry->name, entry->name_len);
            name[entry->name_len] = '\0';
            fn(ctx, name, &info);
        }
    }
    return O0_OK;
}

uint32_t o0fs_block_count(const struct o0fs *fs) {
    return fs != NULL && fs->mounted ? fs->super.block_count : 0;
}

uint32_t o0fs_free_blocks(const struct o0fs *fs) {
    return fs != NULL && fs->mounted ? fs->super.free_blocks : 0;
}

uint32_t o0fs_free_inodes(const struct o0fs *fs) {
    return fs != NULL && fs->mounted ? fs->super.free_inodes : 0;
}
