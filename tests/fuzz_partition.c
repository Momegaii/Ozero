/* Ozero host test - MBR/GPT partition parser coverage-guided fuzz target.
 *
 * Builds a mock block device backed by an in-memory byte buffer and drives
 * o0_partition_discover with mutated disk images. Every iteration must return
 * a status without faulting. Under ASan/UBSan any out-of-bounds read is
 * caught.
 *
 * Build:
 *   gcc -Iinclude -Ikernel/src -o build/fuzz_partition \
 *       tests/fuzz_partition.c tests/fuzz.c \
 *       kernel/src/partition.c kernel/src/block.c
 *
 * Run it with `make test` (it is registered in the Makefile test target).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fuzz.h"
#include "ozero/block.h"
#include "ozero/partition.h"
#include "ozero/status.h"
#include "ozero/types.h"

static struct o0_fuzz g_fuzz;

/* Mock block device backed by an in-memory image. The partition parser reads
 * 512-byte sectors; with logical_block_size=512 the LBA mapping is direct. */
#define MOCK_DISK_MAX (64u * 1024u) /* 64 KiB image */
static uint8_t mock_disk[MOCK_DISK_MAX];
static size_t mock_disk_len;

/* Geometry must satisfy block_geometry_ok() and block_sync_transfer() checks. */
static struct o0_block_geometry mock_geo = {
    .logical_block_size = 512u,
    .physical_block_size = 512u,
    .nr_sectors = MOCK_DISK_MAX / 512u,
    .max_hw_sectors = 0u,
    .max_segments = 1u,
    .max_segment_size = O0_PAGE_SIZE,
    .dma_alignment = 0u,
    .queue_depth = 4u,
    .features = 0u,
};

static struct o0_block_queue mock_queue;
static struct o0_block_dev mock_dev;

/* queue_rq: synchronous read from the in-memory image. */
static o0_status mock_queue_rq(struct o0_block_queue *queue, struct o0_bio *bio) {
    (void)queue;
    if (bio->nr_vecs != 1 || bio->vecs == NULL) {
        return O0_EINVAL;
    }
    const struct o0_bio_vec *vec = &bio->vecs[0];
    if (vec->page == NULL || vec->len == 0 ||
        vec->len % mock_geo.logical_block_size != 0) {
        return O0_EINVAL;
    }
    uint64_t sectors = vec->len / mock_geo.logical_block_size;
    if (bio->lba > mock_geo.nr_sectors ||
        sectors > mock_geo.nr_sectors - bio->lba) {
        return O0_ERANGE;
    }
    uint64_t offset = bio->lba * mock_geo.logical_block_size;
    if (offset + vec->len > mock_disk_len) {
        return O0_ERANGE;
    }
    memcpy(vec->page, mock_disk + offset, vec->len);
    bio->end_io(bio, O0_BLOCK_OK, vec->len);
    return O0_OK;
}

static const struct o0_block_queue_ops mock_queue_ops = {
    .queue_rq = mock_queue_rq,
    .poll = NULL,
    .timeout = NULL,
};

static const struct o0_block_ops mock_block_ops = {
    .reset = NULL,
    .flush = NULL,
    .discard = NULL,
    .write_zeroes = NULL,
    .open = NULL,
    .close = NULL,
    .media_changed = NULL,
    .get_geometry = NULL,
    .set_features = NULL,
};

static int target(const uint8_t *data, size_t len) {
    if (len > MOCK_DISK_MAX) {
        return 0;
    }
    /* Re-initialise the mock device for each input. */
    memset(mock_disk, 0, sizeof(mock_disk));
    memcpy(mock_disk, data, len);
    mock_disk_len = len;

    memset(&mock_dev, 0, sizeof(mock_dev));
    snprintf(mock_dev.name, sizeof(mock_dev.name), "fuzz%d", (int)(len % 1000u));
    mock_dev.geo = mock_geo;
    mock_dev.nr_queues = 1u;
    mock_dev.queues = &mock_queue;
    mock_dev.ops = &mock_block_ops;

    memset(&mock_queue, 0, sizeof(mock_queue));
    mock_queue.id = 0u;
    mock_queue.depth = 4u;
    mock_queue.ops = &mock_queue_ops;

    struct o0_partition_table table;
    o0_status st = o0_partition_discover(&mock_dev, &table);
    /* Any status is acceptable; we only require the parser not to fault. */
    (void)st;
    o0_block_unregister(&mock_dev);
    return 0;
}

int main(void) {
    uint64_t generations = 2000u;

    o0_fuzz_init(&g_fuzz);

    /* Seed 1: valid MBR with boot signature and one partition entry. */
    uint8_t mbr[512];
    memset(mbr, 0, sizeof(mbr));
    mbr[510] = 0x55;
    mbr[511] = 0xaa;
    /* Partition entry at offset 446: bootable, type 0x01, LBA 2048, size 4096 sectors */
    mbr[446] = 0x80;
    mbr[450] = 0x01;
    mbr[454] = 0x00; mbr[455] = 0x08; mbr[456] = 0x00; mbr[457] = 0x00; /* LBA 2048 */
    mbr[458] = 0x00; mbr[459] = 0x10; mbr[460] = 0x00; mbr[461] = 0x00; /* size 4096 */
    o0_fuzz_add_seed(&g_fuzz, mbr, sizeof(mbr));

    /* Seed 2: all zeros (no signature, should be O0_ENOENT). */
    uint8_t zeros[512];
    memset(zeros, 0, sizeof(zeros));
    o0_fuzz_add_seed(&g_fuzz, zeros, sizeof(zeros));

    /* Seed 3: GPT protective MBR + GPT header + one entry (minimal valid GPT). */
    uint8_t gpt[4096];
    memset(gpt, 0, sizeof(gpt));
    /* LBA 0: protective MBR */
    gpt[510] = 0x55;
    gpt[511] = 0xaa;
    gpt[450] = 0xee; /* GPT protective type */
    /* LBA 1: GPT header */
    uint8_t *hdr = gpt + 512;
    memcpy(hdr, "EFI PART", 8);        /* signature */
    hdr[8] = 0x00; hdr[9] = 0x01; hdr[10] = 0x00; hdr[11] = 0x00; /* revision */
    hdr[12] = 92; hdr[13] = 0; hdr[14] = 0; hdr[15] = 0;           /* header size 92 */
    hdr[16] = 0; hdr[17] = 0; hdr[18] = 0; hdr[19] = 0;            /* crc32 */
    hdr[24] = 1; hdr[25] = 0; hdr[26] = 0; hdr[27] = 0;            /* my LBA = 1 */
    hdr[32] = 2; hdr[33] = 0; hdr[34] = 0; hdr[35] = 0;            /* entries LBA = 2 */
    hdr[56] = 1; hdr[57] = 0; hdr[58] = 0; hdr[59] = 0;            /* num entries = 1 */
    hdr[60] = 128; hdr[61] = 0; hdr[62] = 0; hdr[63] = 0;          /* entry size */
    /* LBA 2: GPT entry array */
    uint8_t *entry = gpt + 1024;
    entry[0] = 0xc1; entry[1] = 0x2a; entry[2] = 0x73; entry[3] = 0x28;
    entry[4] = 0xf8; entry[5] = 0x1f; entry[6] = 0x11; entry[7] = 0xd2;
    entry[8] = 0xba; entry[9] = 0x4b; entry[10] = 0x00; entry[11] = 0xa0;
    entry[12] = 0xc9; entry[13] = 0x3e; entry[14] = 0xc9; entry[15] = 0x3b;
    entry[32] = 0x02; entry[33] = 0x00; entry[34] = 0x00; entry[35] = 0x00; /* start LBA 2 */
    entry[40] = 0x10; entry[41] = 0x00; entry[42] = 0x00; entry[43] = 0x00; /* size 4096 sectors */
    o0_fuzz_add_seed(&g_fuzz, gpt, sizeof(gpt));

    printf("fuzz_partition: %llu generations over partition parser\n",
           (unsigned long long)generations);
    uint64_t crashes = o0_fuzz_run(&g_fuzz, target, generations);
    o0_fuzz_summary(&g_fuzz);

    if (crashes != 0) {
        printf("fuzz_partition: FAIL (%llu crashes)\n",
               (unsigned long long)crashes);
        return 1;
    }
    printf("fuzz_partition: PASS\n");
    return 0;
}
