/* Ozero kernel - OzeroFS (o0fs), with redo journal (Wave 4.5).
 *
 * On-disk layout (version 2):
 *
 *   block 0                     superblock
 *   block 1 .. inode table      inodes, 32 per block, 128 bytes each
 *   block block_bitmap_block    block allocation bitmap (one block)
 *   block inode_bitmap_block    inode allocation bitmap (one block)
 *   block journal_block ..      journal region (journal_blocks blocks)
 *   block data_block .. end     data and directory blocks
 *
 * All metadata is little-endian and the compiler is x86-64 only at this stage,
 * so the structures below are written directly. Sizes and offsets are asserted
 * at compile time. The superblock carries a 32-bit FNV-1a checksum over its
 * fixed fields; mounting rejects a mismatch.
 *
 * Crash consistency: a redo journal holds checksummed, sequence-numbered
 * transaction records for metadata blocks. Data blocks are written and flushed
 * before the transaction that references them is committed (ordered data
 * writes). On mount, committed transactions are replayed and uncommitted ones
 * are discarded. The superblock's free_blocks/free_inodes are derived from the
 * bitmaps during recovery rather than trusted from the last write.
 *
 * Evidence limits: QEMU/host-test evidence only. No physical device is claimed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ozero/block.h"
#include "ozero/fs.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#include "string.h"

#define O0FS_MAGIC 0x5346304Fu /* "O0FS" */
#define O0FS_VERSION 2u
#define O0FS_VERSION_1 1u
#define O0FS_SUPER_BLOCK 0u
#define O0FS_ROOT_INODE 1u
#define O0FS_INODE_SIZE 128u
#define O0FS_INODES_PER_BLOCK (O0FS_BLOCK_SIZE / O0FS_INODE_SIZE)
#define O0FS_DIRENT_SIZE 32u
#define O0FS_DIRENT_NAME 24u

#define O0FS_JOURNAL_MAGIC 0x4A55524Eu /* "JURN" */
#define O0FS_JOURNAL_BLOCKS 16u
#define O0FS_MAX_TXN_BLOCKS 8u

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
    uint32_t journal_block;
    uint32_t journal_blocks;
    uint32_t data_block;
    uint32_t root_inode;
    uint32_t flags;
    uint32_t checksum;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint8_t reserved[48];
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

/* One journal record: target block number plus up to 4092 bytes of block data.
 * A full 4 KiB block image spans two records: bytes [0,4092) in the first and
 * bytes [4092,4096) in the second, both carrying the same target block. */
struct o0fs_journal_record {
    uint32_t target_block;   /* 0xFFFFFFFF = unused slot */
    uint8_t data[4092];
} O0_PACKED;

/* Journal transaction header block. Written last (with valid checksum) to
 * commit the transaction; recovery only applies transactions whose header
 * passes validation. */
struct o0fs_journal_header {
    uint32_t magic;          /* O0FS_JOURNAL_MAGIC */
    uint64_t sequence;       /* monotonically increasing transaction id */
    uint32_t record_count;   /* number of records following this header */
    uint32_t checksum;       /* FNV-1a over the record blocks */
    uint8_t reserved[4076];
} O0_PACKED;

_Static_assert(sizeof(struct o0fs_super_disk) <= O0FS_BLOCK_SIZE,
               "superblock must fit in one block");
_Static_assert(sizeof(struct o0fs_inode_disk) == O0FS_INODE_SIZE,
               "inode size mismatch");
_Static_assert(sizeof(struct o0fs_dirent_disk) == O0FS_DIRENT_SIZE,
               "directory entry size mismatch");
_Static_assert(O0FS_INODES_PER_BLOCK * O0FS_INODE_SIZE == O0FS_BLOCK_SIZE,
               "inode table packing mismatch");
_Static_assert(sizeof(struct o0fs_journal_record) == O0FS_BLOCK_SIZE,
               "journal record must fill one block");
_Static_assert(sizeof(struct o0fs_journal_header) == O0FS_BLOCK_SIZE,
               "journal header must fill one block");

struct o0fs_write_backup {
    uint32_t block;
    void *page;
};

struct o0fs {
    struct o0_block_dev *dev;
    struct o0fs_super_disk super;
    /* Current transaction: metadata block images staged for commit. */
    struct o0fs_txn {
        bool active;
        uint64_t sequence;
        uint32_t blocks[O0FS_MAX_TXN_BLOCKS];
        uint8_t data[O0FS_MAX_TXN_BLOCKS][O0FS_BLOCK_SIZE];
        uint32_t count;
        /* Saved block/inode bitmap for rollback when a transaction aborts.
         * fs_block_alloc and fs_ialloc modify the live bitmaps directly; if
         * the transaction never commits, the live bitmaps must be restored
         * so that o0fs_sync on unmount does not persist uncommitted
         * allocations to disk. */
        uint8_t bbm_saved[O0FS_BLOCK_SIZE];
        uint8_t ibm_saved[O0FS_BLOCK_SIZE];
    } txn;
    uint64_t journal_sequence; /* last committed sequence on disk */
    uint8_t *blk;  /* inode table blocks and file data */
    uint8_t *dir;  /* directory blocks */
    uint8_t *ind;  /* indirect block */
    uint8_t *zero; /* zero-fill source for new blocks */
    uint8_t *bbm;  /* block bitmap, one block */
    uint8_t *ibm;  /* inode bitmap, one block */
    bool mounted;
    bool version2;
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

static uint16_t fs_version_major(uint16_t version) {
    return (uint16_t)(version >> 8);
}

static uint16_t fs_version_minor(uint16_t version) {
    return (uint16_t)(version & 0xFFu);
}

static bool fs_super_valid(const struct o0fs_super_disk *super,
                           uint64_t device_blocks) {
    if (super->magic != O0FS_MAGIC) {
        return false;
    }
    if (fs_version_major(super->version) > fs_version_major(O0FS_VERSION)) {
        /* Newer major: refuse, never attempt to read. */
        return false;
    }
    if (super->block_size != O0FS_BLOCK_SIZE) {
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
    if ((uint64_t)super->inode_table_blocks * O0FS_INODES_PER_BLOCK !=
        super->inode_count) {
        return false;
    }
    if (super->block_bitmap_blocks != 1 || super->inode_bitmap_blocks != 1) {
        return false;
    }
    uint64_t block_bitmap_block =
        (uint64_t)super->inode_table_block + super->inode_table_blocks;
    if (block_bitmap_block != super->block_bitmap_block ||
        block_bitmap_block > UINT32_MAX) {
        return false;
    }
    if ((uint64_t)super->block_bitmap_block + 1u != super->inode_bitmap_block) {
        return false;
    }
    if (fs_version_major(super->version) == 0 &&
        fs_version_minor(super->version) >= 2) {
        /* Version 2+ layout: journal block/blocks, then data_block. */
        if (super->journal_block == 0 || super->journal_blocks == 0 ||
            super->journal_blocks > O0FS_JOURNAL_BLOCKS) {
            return false;
        }
        uint64_t journal_end =
            (uint64_t)super->journal_block + super->journal_blocks;
        if (journal_end != super->data_block ||
            super->data_block >= super->block_count) {
            return false;
        }
        /* Reserved bytes must be zero (ADR 0013). */
        for (size_t i = 0; i < sizeof(super->reserved); ++i) {
            if (super->reserved[i] != 0) {
                return false;
            }
        }
    } else {
        /* Version 1 layout (pre-journal): inode_bitmap_block + 1 = data_block. */
        if ((uint64_t)super->inode_bitmap_block + 1u != super->data_block ||
            super->data_block >= super->block_count) {
            return false;
        }
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

/* ---- transaction / redo journal ---- */

/* Returns the journal LBA for a given journal block index. */
static uint32_t fs_journal_lba(const struct o0fs *fs, uint32_t journal_index) {
    return fs->super.journal_block + journal_index;
}

/* Finds a block in the current transaction. Returns its slot index or -1. */
static int fs_txn_find(struct o0fs *fs, uint32_t block) {
    if (!fs->txn.active) {
        return -1;
    }
    for (uint32_t i = 0; i < fs->txn.count; ++i) {
        if (fs->txn.blocks[i] == block) {
            return (int)i;
        }
    }
    return -1;
}

/* Reads a block, preferring the in-transaction image if present. */
static o0_status fs_meta_read(struct o0fs *fs, uint32_t block, void *out) {
    int slot = fs_txn_find(fs, block);
    if (slot >= 0) {
        memcpy(out, fs->txn.data[slot], O0FS_BLOCK_SIZE);
        return O0_OK;
    }
    return fs_read_block(fs, block, out);
}

/* Stages a metadata block image for the current transaction. The block is not
 * written to its live location until the transaction commits. When no
 * transaction is active the write goes straight to disk. */
static o0_status fs_meta_write(struct o0fs *fs, uint32_t block,
                               const void *data) {
    int slot = fs_txn_find(fs, block);
    if (slot >= 0) {
        memcpy(fs->txn.data[slot], data, O0FS_BLOCK_SIZE);
        return O0_OK;
    }
    if (!fs->txn.active) {
        return fs_write_block(fs, block, data);
    }
    if (fs->txn.count >= O0FS_MAX_TXN_BLOCKS) {
        return O0_ENOMEM;
    }
    slot = fs->txn.count++;
    fs->txn.blocks[slot] = block;
    memcpy(fs->txn.data[slot], data, O0FS_BLOCK_SIZE);
    return O0_OK;
}

/* Begins a new transaction. Fails if one is already active. */
static o0_status fs_txn_begin(struct o0fs *fs) {
    if (fs->txn.active) {
        return O0_EBUSY;
    }
    fs->txn.active = true;
    fs->txn.sequence = fs->journal_sequence + 1u;
    fs->txn.count = 0;
    /* Save live bitmaps so they can be restored if the transaction aborts. */
    memcpy(fs->txn.bbm_saved, fs->bbm, O0FS_BLOCK_SIZE);
    memcpy(fs->txn.ibm_saved, fs->ibm, O0FS_BLOCK_SIZE);
    return O0_OK;
}

/* Aborts the current transaction and restores the live bitmaps to their
 * pre-transaction state. Any block or inode allocations staged during the
 * transaction are discarded; the on-disk bitmaps will remain consistent. */
static void fs_txn_abort(struct o0fs *fs) {
    memcpy(fs->bbm, fs->txn.bbm_saved, O0FS_BLOCK_SIZE);
    memcpy(fs->ibm, fs->txn.ibm_saved, O0FS_BLOCK_SIZE);
    fs->txn.active = false;
    fs->txn.count = 0;
}
/* FNV-1a over journal record blocks 0..n-1 starting at journal block 1. */
static uint32_t fs_journal_checksum(struct o0fs *fs, uint32_t n);

/* Commits the current transaction: writes records to the journal, flushes,
 * writes the commit header, flushes, then applies staged images to live
 * locations and flushes them. Ordered data writes (data blocks flushed before
 * commit) are the caller's responsibility. */
static o0_status fs_txn_commit(struct o0fs *fs) {
    if (!fs->txn.active || fs->txn.count == 0) {
        fs->txn.active = false;
        fs->txn.count = 0;
        return O0_OK;
    }

    uint64_t sequence = fs->txn.sequence;
    uint32_t count = fs->txn.count;

    /* Each metadata block image needs up to 2 journal records (4092 bytes of
     * payload per record). Compute the journal blocks needed. */
    uint32_t journal_blocks_needed = 1u; /* header */
    for (uint32_t i = 0; i < count; ++i) {
        journal_blocks_needed += 2u; /* two records per full block image */
    }
    if (journal_blocks_needed > fs->super.journal_blocks) {
        fs_txn_abort(fs);
        return O0_ENOSPC;
    }

    /* Write data records to the journal. Record 2i carries bytes [0,4092),
     * record 2i+1 carries bytes [4092,4096) of the same target block. */
    uint32_t j = 0u; /* journal block offset from journal_block */
    for (uint32_t i = 0; i < count; ++i) {
        struct o0fs_journal_record rec;
        memset(&rec, 0, sizeof(rec));
        rec.target_block = fs->txn.blocks[i];
        memcpy(rec.data, fs->txn.data[i], 4092u);
        o0_status st = fs_write_block(fs, fs_journal_lba(fs, 1u + j), &rec);
        if (st != O0_OK) {
            fs_txn_abort(fs);
            return st;
        }
        ++j;

        struct o0fs_journal_record rec2;
        memset(&rec2, 0, sizeof(rec2));
        rec2.target_block = fs->txn.blocks[i];
        memcpy(rec2.data, fs->txn.data[i] + 4092u, 4u);
        st = fs_write_block(fs, fs_journal_lba(fs, 1u + j), &rec2);
        if (st != O0_OK) {
            fs_txn_abort(fs);
            return st;
        }
        ++j;
    }

    /* Flush journal data records before the commit header. */
    o0_status st = o0_block_flush(fs->dev);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }

    /* Write the commit header. Checksum covers all record blocks. */
    struct o0fs_journal_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = O0FS_JOURNAL_MAGIC;
    hdr.sequence = sequence;
    hdr.record_count = count;
    hdr.checksum = fs_journal_checksum(fs, j);
    st = fs_write_block(fs, fs_journal_lba(fs, 0u), &hdr);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }

    /* Flush the commit header so the transaction is durable. */
    st = o0_block_flush(fs->dev);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }

    /* Apply staged images to live locations. */
    for (uint32_t i = 0; i < count; ++i) {
        st = fs_write_block(fs, fs->txn.blocks[i], fs->txn.data[i]);
        if (st != O0_OK) {
            fs_txn_abort(fs);
            return st;
        }
    }
    st = o0_block_flush(fs->dev);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }

    fs->journal_sequence = sequence;
    fs->txn.active = false;
    fs->txn.count = 0;
    return O0_OK;
}

/* FNV-1a over journal record blocks 0..n-1 starting at journal block 1. */
static uint32_t fs_journal_checksum(struct o0fs *fs, uint32_t n) {
    uint32_t hash = 2166136261u;
    uint8_t buf[O0FS_BLOCK_SIZE];
    for (uint32_t i = 0; i < n; ++i) {
        o0_status st = fs_read_block(fs, fs_journal_lba(fs, 1u + i), buf);
        if (st != O0_OK) {
            return 0;
        }
        for (size_t k = 0; k < O0FS_BLOCK_SIZE; ++k) {
            hash ^= buf[k];
            hash *= 16777619u;
        }
    }
    return hash;
}

/* ---- physical block allocation (journaled) ---- */

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
        o0_status st = fs_meta_write(fs, fs->super.block_bitmap_block,
                                     fs->bbm);
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
            (void)fs_meta_write(fs, fs->super.block_bitmap_block, fs->bbm);
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
    return fs_meta_write(fs, fs->super.block_bitmap_block, fs->bbm);
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
    o0_status st = fs_meta_read(fs, lba, fs->blk);
    if (st != O0_OK) {
        return st;
    }
    memcpy(fs->blk + fs_inode_offset(inode), inode_data, sizeof(*inode_data));
    return fs_meta_write(fs, lba, fs->blk);
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
        st = fs_meta_write(fs, inode->indirect, fs->ind);
        if (st != O0_OK) {
            return st;
        }
    }

    inode->blocks = inode->blocks >= freed ? inode->blocks - freed : 0;
    return O0_OK;
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
        o0_status st = fs_meta_write(fs, fs->super.inode_bitmap_block,
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
            (void)fs_meta_write(fs, fs->super.inode_bitmap_block, fs->ibm);
            return st;
        }
        *out = inode;
        return O0_OK;
    }
    return O0_ENOSPC;
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
    return fs_meta_write(fs, fs->super.inode_bitmap_block, fs->ibm);
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

    o0_status st = fs_meta_read(fs, inode->indirect, fs->ind);
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
        st = fs_meta_write(fs, inode->indirect, fs->ind);
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
            return fs_meta_write(fs, block, fs->dir);
        }
    }

    uint32_t block = 0;
    bool created = false;
    st = fs_bmap(fs, &dir, blocks, true, &block, &created);
    if (st != O0_OK) {
        return st;
    }
    if (created) {
        dir.size += O0FS_BLOCK_SIZE;
    }
    memset(fs->dir, 0, O0FS_BLOCK_SIZE);
    struct o0fs_dirent_disk *entry =
        (struct o0fs_dirent_disk *)fs->dir;
    entry->inode = target;
    entry->rec_len = O0FS_DIRENT_SIZE;
    entry->name_len = (uint8_t)name_len;
    entry->type = type;
    memcpy(entry->name, name, name_len);
    st = fs_meta_write(fs, block, fs->dir);
    if (st != O0_OK) {
        return st;
    }
    if (created) {
        /* The directory inode grew; stage its inode-table block too. */
        return fs_write_inode(fs, dir_inode, &dir);
    }
    return O0_OK;
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
            return fs_meta_write(fs, block, fs->dir);
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

/* Writes data blocks first (ordered data), then stages all metadata changes in
 * a transaction and commits. The caller holds the inode in memory and updates
 * it after success. */
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

    /* Phase 1: begin transaction so all metadata (bitmap, inode table)
     * changes are staged for journaling. Data blocks are written to their
     * live locations by fs_write_range below (ordered data: on disk before
     * the transaction commits). */
    o0_status st = fs_txn_begin(fs);
    if (st != O0_OK) {
        return st;
    }

    /* Phase 2: write data blocks. fs_write_range calls fs_bmap which
     * allocates blocks via fs_meta_write (staged in txn) and writes data
     * blocks directly to disk. */
    st = fs_write_range(fs, inode, offset, buffer, len);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }

    /* Flush data blocks before the transaction commits. */
    st = o0_block_flush(fs->dev);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }

    /* Phase 3: stage the inode update in the transaction. */
    st = fs_write_inode(fs, inode_no, inode);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }

    /* Phase 4: commit the transaction (journal + live application). */
    st = fs_txn_commit(fs);
    if (st != O0_OK) {
        return st;
    }

    if (offset + len > inode->size) {
        inode->size = (uint32_t)(offset + len);
    }
    return O0_OK;
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

/* ---- journal recovery ---- */

/* Replays committed journal transactions. Scans the journal region for valid
 * headers with increasing sequence numbers, applies their record blocks to
 * live locations, and advances the on-disk committed sequence. */
static o0_status fs_journal_recover(struct o0fs *fs) {
    if (!fs->version2) {
        return O0_OK;
    }

    uint8_t *buf = fs->blk; /* reuse scratch page */
    uint64_t last_committed = 0;

    /* Scan all journal blocks for valid headers. */
    for (uint32_t j = 0; j < fs->super.journal_blocks; ++j) {
        o0_status st = fs_read_block(fs, fs_journal_lba(fs, j), buf);
        if (st != O0_OK) {
            return st;
        }
        const struct o0fs_journal_header *hdr =
            (const struct o0fs_journal_header *)buf;
        if (hdr->magic != O0FS_JOURNAL_MAGIC) {
            continue;
        }
        /* Copy header fields before the checksum loop overwrites buf. */
        struct o0fs_journal_header hdr_copy;
        memcpy(&hdr_copy, buf, sizeof(hdr_copy));
        hdr = &hdr_copy;
        if (hdr->sequence <= last_committed) {
            continue; /* stale or already applied */
        }
        if (hdr->record_count == 0 || hdr->record_count > O0FS_MAX_TXN_BLOCKS) {
            continue;
        }
        /* Verify checksum over the record blocks. Each block image spans 2
         * records. */
        uint32_t hash = 2166136261u;
        bool ok = true;
        uint32_t record_blocks = hdr->record_count * 2u;
        for (uint32_t r = 0; r < record_blocks; ++r) {
            uint32_t rblock = j + 1u + r;
            if (rblock >= fs->super.journal_blocks) {
                ok = false;
                break;
            }
            st = fs_read_block(fs, fs_journal_lba(fs, rblock), buf);
            if (st != O0_OK) {
                ok = false;
                break;
            }
            for (size_t k = 0; k < O0FS_BLOCK_SIZE; ++k) {
                hash ^= buf[k];
                hash *= 16777619u;
            }
        }
        if (!ok || hash != hdr->checksum) {
            continue; /* torn or corrupt transaction */
        }

        /* Collect records for replay. Each block image spans 2 records. */
        uint32_t replay_blocks[O0FS_MAX_TXN_BLOCKS];
        uint8_t replay_data[O0FS_MAX_TXN_BLOCKS][O0FS_BLOCK_SIZE];
        uint32_t replay_count = 0;
        for (uint32_t r = 0; r < hdr->record_count; ++r) {
            uint32_t rec0_idx = j + 1u + r * 2u;
            uint32_t rec1_idx = j + 1u + r * 2u + 1u;
            if (rec1_idx >= fs->super.journal_blocks) {
                ok = false;
                break;
            }
            st = fs_read_block(fs, fs_journal_lba(fs, rec0_idx), buf);
            if (st != O0_OK) {
                ok = false;
                break;
            }
            const struct o0fs_journal_record *rec0 =
                (const struct o0fs_journal_record *)buf;
            uint32_t target = rec0->target_block;
            if (target == 0xFFFFFFFFu) {
                ok = false;
                break;
            }
            /* Copy rec0's data payload (not the target_block field) before buf
             * is overwritten by the rec1 read. */
            uint8_t rec0_data[4092];
            memcpy(rec0_data, rec0->data, 4092u);

            st = fs_read_block(fs, fs_journal_lba(fs, rec1_idx), buf);
            if (st != O0_OK) {
                ok = false;
                break;
            }
            const struct o0fs_journal_record *rec1 =
                (const struct o0fs_journal_record *)buf;
            if (rec1->target_block != target) {
                ok = false;
                break;
            }
            if (replay_count >= O0FS_MAX_TXN_BLOCKS) {
                ok = false;
                break;
            }
            replay_blocks[replay_count] = target;
            memcpy(replay_data[replay_count], rec0_data, 4092u);
            memcpy(replay_data[replay_count] + 4092u, rec1->data, 4u);
            ++replay_count;
        }
        if (!ok) {
            continue;
        }

        /* Apply this transaction's records to live locations. */
        for (uint32_t r = 0; r < replay_count; ++r) {
            st = fs_write_block(fs, replay_blocks[r], replay_data[r]);
            if (st != O0_OK) {
                return st;
            }
        }
        st = o0_block_flush(fs->dev);
        if (st != O0_OK) {
            return st;
        }
        last_committed = hdr->sequence;
    }

    fs->journal_sequence = last_committed;
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

    /* Version 2 layout: journal region between bitmaps and data blocks. */
    uint32_t block_bitmap_block = 1 + inode_blocks;
    uint32_t inode_bitmap_block = block_bitmap_block + 1;
    uint32_t journal_block = inode_bitmap_block + 1;
    uint32_t journal_blocks = O0FS_JOURNAL_BLOCKS;
    uint32_t data_block = journal_block + journal_blocks;

    if (data_block >= block_count) {
        return O0_ENOSPC; /* volume too small for version 2 layout */
    }

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
    super.block_bitmap_block = block_bitmap_block;
    super.block_bitmap_blocks = 1;
    super.inode_bitmap_block = inode_bitmap_block;
    super.inode_bitmap_blocks = 1;
    super.journal_block = journal_block;
    super.journal_blocks = journal_blocks;
    super.data_block = data_block;
    super.root_inode = O0FS_ROOT_INODE;
    super.free_blocks = block_count - data_block;
    super.free_inodes = inode_count - 2; /* inode 0 reserved, 1 is root */
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

    /* Zero the journal blocks. */
    for (uint32_t j = 0; j < journal_blocks; ++j) {
        memset(scratch, 0, O0FS_BLOCK_SIZE);
        st = o0_block_write(dev, journal_block + j, scratch, O0FS_BLOCK_SIZE);
        if (st != O0_OK) {
            fs_page_free(scratch);
            return st;
        }
    }

    /* Zero the inode table, then place the root inode in the first entry. */
    for (uint32_t i = 0; i < inode_blocks; ++i) {
        memset(scratch, 0, O0FS_BLOCK_SIZE);
        if (i == 0) {
            struct o0fs_inode_disk root;
            memset(&root, 0, sizeof(root));
            root.mode = O0FS_TYPE_DIR;
            root.links = 1;
            memcpy((uint8_t *)scratch + fs_inode_offset(O0FS_ROOT_INODE),
                   &root, sizeof(root));
        }
        st = o0_block_write(dev, super.inode_table_block + i, scratch,
                            O0FS_BLOCK_SIZE);
        if (st != O0_OK) {
            fs_page_free(scratch);
            return st;
        }
    }

    /* Both bitmaps use one block; mark metadata and journal blocks as used. */
    memset(scratch, 0, O0FS_BLOCK_SIZE);
    for (uint32_t block = 0; block < data_block; ++block) {
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
    if (fs->super.checksum != fs_checksum(&fs->super)) {
        st = O0_EINVAL;
        goto fail_status;
    }
    if (!fs_super_valid(&fs->super, dev->geo.nr_sectors)) {
        st = O0_EINVAL;
        goto fail_status;
    }
    fs->version2 = (fs->super.version == O0FS_VERSION);

    /* Version 1 volumes are not mountable by the version 2 implementation:
     * the superblock layout changed (journal region inserted). */
    if (!fs->version2) {
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

    /* Journal recovery: replay committed journal transactions. */
    st = fs_journal_recover(fs);
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

/* ---- consistency checker (Wave 4.5) ---- */

/* Walks every allocated inode and verifies that each referenced data block is
 * marked used in the block bitmap, that no block is referenced twice, and that
 * directory entries reference allocated inodes of the matching type. Returns
 * O0_OK on full consistency, O0_EINVAL on any inconsistency. */
static o0_status fs_check(struct o0fs *fs) {
    /* Block usage tracker: 1 = referenced by metadata/inode. */
    static uint8_t used[O0FS_MAX_BLOCK_COUNT / 8u + 1u];
    memset(used, 0, sizeof(used));

    uint32_t block_count = fs->super.block_count;
    uint32_t data_block = fs->super.data_block;

    /* Metadata blocks are always used. */
    for (uint32_t b = 0; b < data_block; ++b) {
        uint32_t byte = b >> 3;
        uint8_t mask = (uint8_t)(1u << (b & 7u));
        used[byte] |= mask;
    }

    /* Walk all inodes. */
    for (uint32_t ino = 1; ino < fs->super.inode_count; ++ino) {
        if (!fs_bit_get(fs->ibm, ino)) {
            continue;
        }
        struct o0fs_inode_disk ino_data;
        o0_status st = fs_read_inode(fs, ino, &ino_data);
        if (st != O0_OK) {
            return st;
        }
        if (ino_data.mode != O0FS_TYPE_FILE && ino_data.mode != O0FS_TYPE_DIR) {
            return O0_EINVAL;
        }

        /* Check direct blocks. */
        uint32_t referenced = 0;
        for (uint32_t i = 0; i < O0FS_DIRECT_BLOCKS; ++i) {
            uint32_t b = ino_data.direct[i];
            if (b == 0) {
                continue;
            }
            if (b < data_block || b >= block_count) {
                return O0_EINVAL;
            }
            uint32_t byte = b >> 3;
            uint8_t mask = (uint8_t)(1u << (b & 7u));
            if (used[byte] & mask) {
                return O0_EINVAL; /* block referenced twice */
            }
            used[byte] |= mask;
            ++referenced;
        }

        /* Check indirect block. */
        if (ino_data.indirect != 0) {
            uint32_t ib = ino_data.indirect;
            if (ib < data_block || ib >= block_count) {
                return O0_EINVAL;
            }
            uint32_t byte = ib >> 3;
            uint8_t mask = (uint8_t)(1u << (ib & 7u));
            if (used[byte] & mask) {
                return O0_EINVAL;
            }
            used[byte] |= mask;
            ++referenced;

            st = fs_read_block(fs, ib, fs->ind);
            if (st != O0_OK) {
                return st;
            }
            uint32_t *entries = (uint32_t *)fs->ind;
            for (uint32_t e = 0; e < O0FS_INDIRECT_ENTRIES; ++e) {
                uint32_t b = entries[e];
                if (b == 0) {
                    continue;
                }
                if (b < data_block || b >= block_count) {
                    return O0_EINVAL;
                }
                byte = b >> 3;
                mask = (uint8_t)(1u << (b & 7u));
                if (used[byte] & mask) {
                    return O0_EINVAL;
                }
                used[byte] |= mask;
                ++referenced;
            }
        }

        if (referenced != ino_data.blocks) {
            return O0_EINVAL;
        }

        /* Directories: verify entries reference allocated inodes. */
        if (ino_data.mode == O0FS_TYPE_DIR) {
            if (ino_data.size % O0FS_BLOCK_SIZE != 0) {
                return O0_EINVAL;
            }
            uint32_t dblocks = ino_data.size / O0FS_BLOCK_SIZE;
            for (uint32_t i = 0; i < dblocks; ++i) {
                uint32_t block = 0;
                bool created = false;
                st = fs_bmap(fs, &ino_data, i, false, &block, &created);
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
                    if (entry->inode >= fs->super.inode_count) {
                        return O0_EINVAL;
                    }
                    if (!fs_bit_get(fs->ibm, entry->inode)) {
                        return O0_EINVAL;
                    }
                    struct o0fs_inode_disk target;
                    st = fs_read_inode(fs, entry->inode, &target);
                    if (st != O0_OK) {
                        return st;
                    }
                    if (target.mode != entry->type) {
                        return O0_EINVAL;
                    }
                }
            }
        }
    }

    /* Block bitmap must match our usage tracker for all data blocks. */
    for (uint32_t b = data_block; b < block_count; ++b) {
        bool bitmap_used = fs_bit_get(fs->bbm, b);
        uint32_t byte = b >> 3;
        uint8_t mask = (uint8_t)(1u << (b & 7u));
        bool tracker_used = (used[byte] & mask) != 0;
        if (bitmap_used != tracker_used) {
            return O0_EINVAL;
        }
    }

    return O0_OK;
}

o0_status o0fs_check(struct o0_block_dev *dev) {
    if (!fs_device_ok(dev)) {
        return dev == NULL ? O0_EINVAL : O0_ENOTSUP;
    }
    struct o0fs *fs = NULL;
    o0_status st = o0fs_mount(dev, &fs);
    if (st != O0_OK) {
        return st;
    }
    st = fs_check(fs);
    (void)o0fs_unmount(fs);
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
    o0_status st = fs_txn_begin(fs);
    if (st != O0_OK) {
        return st;
    }

    uint32_t parent = 0;
    char name[O0FS_NAME_MAX + 1u];
    st = fs_parent_dir(fs, path, &parent, name, sizeof(name));
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }

    uint32_t existing = 0;
    o0_status lookup_st = fs_dir_lookup(fs, parent, name, &existing);
    if (lookup_st == O0_OK) {
        fs_txn_abort(fs);
        return O0_EEXIST;
    }
    if (lookup_st != O0_ENOENT) {
        fs_txn_abort(fs);
        return lookup_st;
    }

    uint32_t inode = 0;
    st = fs_inode_alloc(fs, mode, &inode);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    st = fs_dir_add(fs, parent, name, inode, (uint8_t)mode);
    if (st != O0_OK) {
        (void)fs_inode_release(fs, inode);
        fs_txn_abort(fs);
        return st;
    }
    st = fs_txn_commit(fs);
    return st;
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

    st = fs_txn_begin(fs);
    if (st != O0_OK) {
        return st;
    }
    if (size < inode.size) {
        uint32_t keep = (uint32_t)((size + O0FS_BLOCK_SIZE - 1u) /
                                   O0FS_BLOCK_SIZE);
        st = fs_inode_release_blocks(fs, &inode, keep);
        if (st != O0_OK) {
            fs_txn_abort(fs);
            return st;
        }
        inode.size = (uint32_t)size;
        st = fs_write_inode(fs, inode_no, &inode);
        if (st != O0_OK) {
            fs_txn_abort(fs);
            return st;
        }
    } else if (size > inode.size) {
        st = fs_write_range(fs, &inode, inode.size, NULL,
                            (size_t)(size - inode.size));
        if (st != O0_OK) {
            fs_txn_abort(fs);
            return st;
        }
        st = fs_write_inode(fs, inode_no, &inode);
        if (st != O0_OK) {
            fs_txn_abort(fs);
            return st;
        }
    }
    st = fs_txn_commit(fs);
    return st;
}

o0_status o0fs_unlink(struct o0fs *fs, const char *path) {
    if (fs == NULL || !fs->mounted) {
        return O0_EINVAL;
    }
    o0_status st = fs_txn_begin(fs);
    if (st != O0_OK) {
        return st;
    }

    uint32_t parent = 0;
    char name[O0FS_NAME_MAX + 1u];
    st = fs_parent_dir(fs, path, &parent, name, sizeof(name));
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    uint32_t inode = 0;
    st = fs_dir_lookup(fs, parent, name, &inode);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    struct o0fs_inode_disk data;
    st = fs_read_inode(fs, inode, &data);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    if (data.mode == O0FS_TYPE_DIR) {
        fs_txn_abort(fs);
        return O0_EISDIR;
    }
    st = fs_dir_remove(fs, parent, name);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    st = fs_inode_release(fs, inode);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    st = fs_txn_commit(fs);
    return st;
}

o0_status o0fs_rmdir(struct o0fs *fs, const char *path) {
    if (fs == NULL || !fs->mounted) {
        return O0_EINVAL;
    }
    o0_status st = fs_txn_begin(fs);
    if (st != O0_OK) {
        return st;
    }

    uint32_t parent = 0;
    char name[O0FS_NAME_MAX + 1u];
    st = fs_parent_dir(fs, path, &parent, name, sizeof(name));
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    uint32_t inode = 0;
    st = fs_dir_lookup(fs, parent, name, &inode);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    if (inode == fs->super.root_inode) {
        fs_txn_abort(fs);
        return O0_EPERM;
    }
    struct o0fs_inode_disk data;
    st = fs_read_inode(fs, inode, &data);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    if (data.mode != O0FS_TYPE_DIR) {
        fs_txn_abort(fs);
        return O0_ENOTDIR;
    }
    bool empty = false;
    st = fs_dir_empty(fs, inode, &empty);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    if (!empty) {
        fs_txn_abort(fs);
        return O0_ENOTEMPTY;
    }
    st = fs_dir_remove(fs, parent, name);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    st = fs_inode_release(fs, inode);
    if (st != O0_OK) {
        fs_txn_abort(fs);
        return st;
    }
    st = fs_txn_commit(fs);
    return st;
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
