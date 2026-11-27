/* Ozero kernel - in-memory block device driver.
 *
 * One o0_block_dev, one queue, synchronous completion. The backing store is a
 * list of PMM pages reached through the direct map, so a bio is copied between
 * the caller's buffer and page-backed storage without any DMA or interrupt
 * path. That makes it useful as the Stage 4 reference driver and as a fault
 * injection target for block and filesystem tests.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/block.h"
#include "ozero/mm.h"
#include "ozero/ramdisk.h"
#include "ozero/status.h"
#include "ozero/types.h"

#include "string.h"

struct o0_ramdisk {
    struct o0_block_dev dev;
    struct o0_block_queue queue;
    void *pages[O0_RAMDISK_MAX_PAGES];
    uint32_t page_count;
    bool in_use;

    /* Fault injection state. remaining < 0 means "unlimited". */
    o0_block_status fault;
    bool fault_write_only;
    uint64_t fault_lba;
    int64_t remaining;
};

static struct o0_ramdisk ramdisks[O0_RAMDISK_MAX_DEVICES];

static struct o0_ramdisk *ramdisk_from_dev(const struct o0_block_dev *dev) {
    for (size_t i = 0; i < O0_RAMDISK_MAX_DEVICES; ++i) {
        if (&ramdisks[i].dev == dev && ramdisks[i].in_use) {
            return &ramdisks[i];
        }
    }
    return NULL;
}

static void ramdisk_copy(struct o0_ramdisk *rd, uint64_t byte_offset,
                         const uint8_t *src, uint8_t *dst, size_t bytes) {
    /* src != NULL: write to the disk; dst != NULL: read from the disk. */
    size_t done = 0;
    while (done < bytes) {
        uint64_t offset = byte_offset + done;
        uint32_t page = (uint32_t)(offset / O0_PAGE_SIZE);
        size_t inner = (size_t)(offset % O0_PAGE_SIZE);
        size_t chunk = O0_PAGE_SIZE - inner;
        if (chunk > bytes - done) {
            chunk = bytes - done;
        }
        uint8_t *base = (uint8_t *)rd->pages[page];
        if (src != NULL) {
            memcpy(base + inner, src + done, chunk);
        } else {
            memcpy(dst + done, base + inner, chunk);
        }
        done += chunk;
    }
}

static bool ramdisk_fault_matches(struct o0_ramdisk *rd, uint64_t lba,
                                  uint64_t sectors, bool is_write) {
    if (rd->fault == O0_BLOCK_OK || (rd->fault_write_only && !is_write)) {
        return false;
    }
    if (rd->remaining == 0) {
        return false;
    }
    /* Match any transfer range that contains the injected LBA. */
    if (rd->fault_lba < lba || rd->fault_lba >= lba + sectors) {
        return false;
    }
    if (rd->remaining > 0) {
        --rd->remaining;
    }
    return true;
}

static void ramdisk_complete(struct o0_bio *bio, o0_block_status status,
                             size_t bytes) {
    o0_block_complete_bio(bio, status, bytes);
}

static o0_status ramdisk_queue_rq(struct o0_block_queue *queue,
                                  struct o0_bio *bio) {
    if (queue == NULL || bio == NULL) {
        return O0_EINVAL;
    }
    struct o0_ramdisk *rd = (struct o0_ramdisk *)queue->driver_data;
    if (rd == NULL || !rd->in_use) {
        return O0_ENOENT;
    }

    if (bio->op == O0_BLOCK_FLUSH) {
        /* Nothing is cached: the backing store is the media. */
        ramdisk_complete(bio, O0_BLOCK_OK, 0);
        return O0_OK;
    }
    if (bio->op != O0_BLOCK_READ && bio->op != O0_BLOCK_WRITE) {
        return O0_ENOTSUP;
    }

    uint64_t bytes = 0;
    for (uint32_t i = 0; i < bio->nr_vecs; ++i) {
        if (bio->vecs[i].page == NULL || bio->vecs[i].len == 0) {
            return O0_EINVAL;
        }
        bytes += bio->vecs[i].len;
    }

    uint64_t sectors = bytes / rd->dev.geo.logical_block_size;
    if (rd->fault != O0_BLOCK_OK &&
        ramdisk_fault_matches(rd, bio->lba, sectors,
                              bio->op == O0_BLOCK_WRITE)) {
        ramdisk_complete(bio, rd->fault, 0);
        return O0_OK;
    }

    uint64_t offset = bio->lba * (uint64_t)rd->dev.geo.logical_block_size;
    uint64_t cursor = offset;
    for (uint32_t i = 0; i < bio->nr_vecs; ++i) {
        const struct o0_bio_vec *vec = &bio->vecs[i];
        uint8_t *page = (uint8_t *)vec->page + vec->offset;
        if (bio->op == O0_BLOCK_WRITE) {
            ramdisk_copy(rd, cursor, page, NULL, vec->len);
        } else {
            ramdisk_copy(rd, cursor, NULL, page, vec->len);
        }
        cursor += vec->len;
    }

    ramdisk_complete(bio, O0_BLOCK_OK, (size_t)bytes);
    return O0_OK;
}

static o0_status ramdisk_reset(struct o0_block_dev *dev) {
    struct o0_ramdisk *rd = ramdisk_from_dev(dev);
    if (rd == NULL) {
        return O0_ENOENT;
    }
    rd->fault = O0_BLOCK_OK;
    rd->fault_write_only = false;
    rd->remaining = 0;
    return O0_OK;
}

static o0_status ramdisk_flush(struct o0_block_dev *dev) {
    return dev != NULL ? O0_OK : O0_EINVAL;
}

static o0_status ramdisk_get_geometry(struct o0_block_dev *dev,
                                      struct o0_block_geometry *out) {
    if (dev == NULL || out == NULL) {
        return O0_EINVAL;
    }
    *out = dev->geo;
    return O0_OK;
}

static bool ramdisk_media_changed(struct o0_block_dev *dev) {
    (void)dev;
    return false;
}

static o0_status ramdisk_open(struct o0_block_dev *dev, bool exclusive) {
    (void)exclusive;
    return dev != NULL ? O0_OK : O0_EINVAL;
}

static void ramdisk_close(struct o0_block_dev *dev) {
    (void)dev;
}

static struct o0_block_ops ramdisk_block_ops = {
    .reset = ramdisk_reset,
    .flush = ramdisk_flush,
    .discard = NULL,
    .write_zeroes = NULL,
    .open = ramdisk_open,
    .close = ramdisk_close,
    .media_changed = ramdisk_media_changed,
    .get_geometry = ramdisk_get_geometry,
    .set_features = NULL
};

static struct o0_block_queue_ops ramdisk_queue_ops = {
    .queue_rq = ramdisk_queue_rq,
    .poll = NULL,
    .timeout = NULL
};

static void ramdisk_set_name(char *dst, const char *src) {
    size_t i = 0;
    for (; i + 1 < O0_BLOCK_NAME_MAX && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void ramdisk_release(struct o0_ramdisk *rd) {
    for (uint32_t i = 0; i < rd->page_count; ++i) {
        if (rd->pages[i] == NULL) {
            continue;
        }
        o0_paddr_t phys = o0_virt_to_phys(rd->pages[i]);
        if (phys != 0) {
            o0_pmm_free_pages(phys, 0);
        }
        rd->pages[i] = NULL;
    }
    rd->page_count = 0;
    rd->in_use = false;
    memset(&rd->dev, 0, sizeof(rd->dev));
    memset(&rd->queue, 0, sizeof(rd->queue));
    rd->fault = O0_BLOCK_OK;
    rd->fault_write_only = false;
    rd->remaining = 0;
}

o0_status o0_ramdisk_create(const char *name, size_t bytes,
                            struct o0_block_dev **out) {
    if (name == NULL || out == NULL || name[0] == '\0' ||
        strlen(name) >= O0_BLOCK_NAME_MAX) {
        return O0_EINVAL;
    }
    *out = NULL;

    if (bytes < 64u * 1024u || bytes > O0_RAMDISK_MAX_BYTES) {
        return O0_EINVAL;
    }
    bytes &= ~(size_t)(O0_RAMDISK_BLOCK_SIZE - 1u);
    uint32_t pages = (uint32_t)(bytes / O0_PAGE_SIZE);
    if (pages == 0 || pages > O0_RAMDISK_MAX_PAGES) {
        return O0_EINVAL;
    }

    struct o0_ramdisk *rd = NULL;
    for (size_t i = 0; i < O0_RAMDISK_MAX_DEVICES; ++i) {
        if (!ramdisks[i].in_use) {
            rd = &ramdisks[i];
            break;
        }
    }
    if (rd == NULL) {
        return O0_EBUSY;
    }
    memset(rd, 0, sizeof(*rd));

    for (uint32_t i = 0; i < pages; ++i) {
        o0_paddr_t phys = o0_pmm_alloc_pages(0);
        if (phys == 0) {
            ramdisk_release(rd);
            return O0_ENOMEM;
        }
        void *virt = o0_phys_to_virt(phys);
        if (virt == NULL) {
            o0_pmm_free_pages(phys, 0);
            ramdisk_release(rd);
            return O0_ENOMEM;
        }
        memset(virt, 0, O0_PAGE_SIZE);
        rd->pages[i] = virt;
        rd->page_count = i + 1u;
    }

    rd->in_use = true;
    ramdisk_set_name(rd->dev.name, name);
    rd->dev.geo.logical_block_size = O0_RAMDISK_BLOCK_SIZE;
    rd->dev.geo.physical_block_size = O0_RAMDISK_BLOCK_SIZE;
    rd->dev.geo.nr_sectors = bytes / O0_RAMDISK_BLOCK_SIZE;
    rd->dev.geo.max_hw_sectors = O0_PAGE_SIZE / O0_RAMDISK_BLOCK_SIZE;
    rd->dev.geo.max_segments = 1;
    rd->dev.geo.max_segment_size = O0_PAGE_SIZE;
    rd->dev.geo.dma_alignment = O0_PAGE_SIZE;
    rd->dev.geo.queue_depth = 1;
    rd->dev.geo.features = O0_BLOCK_F_FLUSH | O0_BLOCK_F_DMA_COHERENT;
    rd->dev.nr_queues = 1;
    rd->dev.queues = &rd->queue;
    rd->dev.ops = &ramdisk_block_ops;
    rd->dev.driver_data = rd;

    rd->queue.id = 0;
    rd->queue.depth = 1;
    rd->queue.ops = &ramdisk_queue_ops;
    rd->queue.driver_data = rd;

    rd->fault = O0_BLOCK_OK;
    rd->remaining = 0;

    o0_status st = o0_block_register(&rd->dev);
    if (st != O0_OK) {
        ramdisk_release(rd);
        return st;
    }

    *out = &rd->dev;
    return O0_OK;
}

o0_status o0_ramdisk_destroy(struct o0_block_dev *dev) {
    struct o0_ramdisk *rd = ramdisk_from_dev(dev);
    if (rd == NULL) {
        return O0_ENOENT;
    }
    o0_block_unregister(dev);
    ramdisk_release(rd);
    return O0_OK;
}

o0_status o0_ramdisk_inject_fault(struct o0_block_dev *dev,
                                  o0_block_status status,
                                  uint64_t fail_lba,
                                  int64_t count) {
    struct o0_ramdisk *rd = ramdisk_from_dev(dev);
    if (rd == NULL) {
        return O0_ENOENT;
    }
    if (status > O0_BLOCK_ERR_AGAIN) {
        return O0_EINVAL;
    }
    rd->fault = status;
    rd->fault_write_only = false;
    rd->fault_lba = fail_lba;
    rd->remaining = status == O0_BLOCK_OK ? 0 : count;
    return O0_OK;
}

o0_status o0_ramdisk_inject_write_fault(struct o0_block_dev *dev,
                                        o0_block_status status,
                                        uint64_t fail_lba,
                                        int64_t count) {
    o0_status st = o0_ramdisk_inject_fault(dev, status, fail_lba, count);
    if (st != O0_OK) {
        return st;
    }
    struct o0_ramdisk *rd = ramdisk_from_dev(dev);
    rd->fault_write_only = status != O0_BLOCK_OK;
    return O0_OK;
}
