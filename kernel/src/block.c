/* Ozero kernel - device-independent block layer core.
 *
 * The block layer owns device registration, request validation and the
 * synchronous single-segment path used by Stage 4 storage clients. Protocol
 * drivers own command translation, transport queues, DMA mapping and
 * completion; this file never touches device registers.
 *
 * Stage 4 scope, stated honestly:
 *   - one bio is one contiguous logical transfer, validated against the
 *     device geometry and handed to exactly one hardware queue;
 *   - there is no request merging, no I/O scheduler and no queue depth
 *     accounting yet, so a driver that cannot accept a request right now must
 *     fail it with O0_BLOCK_ERR_AGAIN;
 *   - registration is single-threaded boot-time work and is not locked;
 *   - synchronous submission is the only client path (see block.h).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/block.h"
#include "ozero/status.h"
#include "ozero/types.h"

#include "string.h"

#define O0_BLOCK_MAX_DEVICES 8u
#define O0_BLOCK_SYNC_SPINS 100000u
#define O0_BLOCK_SYNC_POLL_BUDGET 8

static struct o0_block_dev *block_devices[O0_BLOCK_MAX_DEVICES];
static size_t block_device_total;

static bool block_power_of_two(uint32_t value) {
    return value != 0 && (value & (value - 1u)) == 0;
}

static bool block_name_ok(const char *name) {
    if (name == NULL) {
        return false;
    }
    size_t length = strlen(name);
    return length > 0 && length < O0_BLOCK_NAME_MAX;
}

static bool block_geometry_ok(const struct o0_block_geometry *geo) {
    if (geo == NULL) {
        return false;
    }
    if (geo->logical_block_size < 512 || geo->logical_block_size > O0_PAGE_SIZE) {
        return false;
    }
    if (!block_power_of_two(geo->logical_block_size)) {
        return false;
    }
    if (geo->physical_block_size != 0) {
        if (!block_power_of_two(geo->physical_block_size) ||
            geo->physical_block_size < geo->logical_block_size) {
            return false;
        }
    }
    if (geo->nr_sectors == 0) {
        return false;
    }
    if (geo->dma_alignment != 0 && !block_power_of_two(geo->dma_alignment)) {
        return false;
    }
    if (geo->max_segment_size != 0 &&
        geo->max_segment_size < geo->logical_block_size) {
        return false;
    }
    return true;
}

static struct o0_block_dev *block_find(const struct o0_block_dev *dev) {
    for (size_t i = 0; i < block_device_total; ++i) {
        if (block_devices[i] == dev) {
            return block_devices[i];
        }
    }
    return NULL;
}

/* Registration names are NUL-terminated within O0_BLOCK_NAME_MAX bytes; the
 * kernel has no strcmp in its freestanding string subset. */
static bool block_name_same(const char *a, const char *b) {
    for (size_t i = 0; i < O0_BLOCK_NAME_MAX; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
    return false;
}

static bool block_name_registered(const char *name) {
    for (size_t i = 0; i < block_device_total; ++i) {
        if (block_name_same(block_devices[i]->name, name)) {
            return true;
        }
    }
    return false;
}

o0_status o0_block_register(struct o0_block_dev *dev) {
    if (dev == NULL || !block_name_ok(dev->name) || dev->ops == NULL) {
        return O0_EINVAL;
    }
    if (!block_geometry_ok(&dev->geo)) {
        return O0_EINVAL;
    }
    if (dev->queues == NULL || dev->nr_queues == 0 ||
        dev->nr_queues > O0_BLOCK_MAX_QUEUES) {
        return O0_EINVAL;
    }
    if (block_find(dev) != NULL) {
        return O0_EBUSY;
    }
    if (block_name_registered(dev->name)) {
        return O0_EBUSY;
    }
    for (unsigned int i = 0; i < dev->nr_queues; ++i) {
        const struct o0_block_queue *queue = &dev->queues[i];
        if (queue->ops == NULL || queue->ops->queue_rq == NULL) {
            return O0_EINVAL;
        }
        if (queue->id != i) {
            return O0_EINVAL;
        }
        if (queue->depth == 0) {
            return O0_EINVAL;
        }
    }
    if (block_device_total >= O0_BLOCK_MAX_DEVICES) {
        return O0_ENOMEM;
    }

    block_devices[block_device_total++] = dev;
    return O0_OK;
}

void o0_block_unregister(struct o0_block_dev *dev) {
    for (size_t i = 0; i < block_device_total; ++i) {
        if (block_devices[i] != dev) {
            continue;
        }
        for (size_t j = i + 1; j < block_device_total; ++j) {
            block_devices[j - 1] = block_devices[j];
        }
        --block_device_total;
        block_devices[block_device_total] = NULL;
        return;
    }
}

size_t o0_block_device_count(void) {
    return block_device_total;
}

struct o0_block_dev *o0_block_device_by_index(size_t index) {
    if (index >= block_device_total) {
        return NULL;
    }
    return block_devices[index];
}

/* Total bytes described by the bio's logical vectors. */
static o0_status block_bio_bytes(const struct o0_bio *bio, uint64_t *out_bytes) {
    uint64_t total = 0;
    for (uint32_t i = 0; i < bio->nr_vecs; ++i) {
        const struct o0_bio_vec *vec = &bio->vecs[i];
        if (vec->page == NULL) {
            return O0_EINVAL;
        }
        if (vec->len == 0) {
            return O0_EINVAL;
        }
        if (vec->offset >= O0_PAGE_SIZE) {
            return O0_EINVAL;
        }
        if (vec->len > O0_PAGE_SIZE - vec->offset) {
            return O0_EINVAL;
        }
        if (total > UINT64_MAX - vec->len) {
            return O0_EOVERFLOW;
        }
        total += vec->len;
    }
    *out_bytes = total;
    return O0_OK;
}

o0_status o0_block_submit_bio(struct o0_block_dev *dev, struct o0_bio *bio) {
    if (dev == NULL || bio == NULL || bio->end_io == NULL) {
        return O0_EINVAL;
    }
    if (block_find(dev) == NULL) {
        return O0_ENOENT;
    }
    if (bio->queue_id >= dev->nr_queues) {
        return O0_EINVAL;
    }

    struct o0_block_queue *queue = &dev->queues[bio->queue_id];
    if (queue->ops == NULL || queue->ops->queue_rq == NULL) {
        return O0_EINVAL;
    }

    uint64_t bytes = 0;
    switch (bio->op) {
        case O0_BLOCK_READ:
        case O0_BLOCK_WRITE: {
            if (bio->nr_vecs == 0 || bio->vecs == NULL) {
                return O0_EINVAL;
            }
            o0_status st = block_bio_bytes(bio, &bytes);
            if (st != O0_OK) {
                return st;
            }
            if (bytes == 0 || (bytes % dev->geo.logical_block_size) != 0) {
                return O0_EINVAL;
            }
            uint64_t sectors = bytes / dev->geo.logical_block_size;
            if (bio->lba > dev->geo.nr_sectors ||
                sectors > dev->geo.nr_sectors - bio->lba) {
                return O0_ERANGE;
            }
            if ((bio->flags & O0_BIO_F_FUA) != 0 &&
                (dev->geo.features & O0_BLOCK_F_FUA) == 0) {
                return O0_ENOTSUP;
            }
            break;
        }
        case O0_BLOCK_FLUSH:
            if (bio->nr_vecs != 0) {
                return O0_EINVAL;
            }
            if ((dev->geo.features & O0_BLOCK_F_FLUSH) == 0) {
                return O0_ENOTSUP;
            }
            break;
        default:
            /* Discard, write-zeroes and zone operations need a request model
             * this stage does not have yet. */
            return O0_ENOTSUP;
    }

    /* A synchronous driver may complete inside queue_rq; on an error return the
     * bio was not accepted and no completion will follow. */
    return queue->ops->queue_rq(queue, bio);
}

void o0_block_complete_bio(struct o0_bio *bio,
                           o0_block_status status,
                           size_t bytes_done) {
    if (bio == NULL || bio->end_io == NULL) {
        return;
    }
    o0_bio_end_t callback = bio->end_io;
    /* Clear first so a driver that completes twice cannot deliver two
     * completions for one submission. */
    bio->end_io = NULL;
    callback(bio, status, bytes_done);
}

static o0_status block_status_to_status(o0_block_status status) {
    switch (status) {
        case O0_BLOCK_OK: return O0_OK;
        case O0_BLOCK_ERR_INVAL: return O0_EINVAL;
        case O0_BLOCK_ERR_NOTSUP: return O0_ENOTSUP;
        case O0_BLOCK_ERR_AGAIN: return O0_EAGAIN;
        case O0_BLOCK_ERR_TIMEOUT: return O0_ETIMEDOUT;
        case O0_BLOCK_ERR_IO:
        case O0_BLOCK_ERR_MEDIUM:
        case O0_BLOCK_ERR_TRANSPORT:
        case O0_BLOCK_ERR_RESET:
        default:
            return O0_EIO;
    }
}

struct block_sync_state {
    bool done;
    o0_block_status status;
    size_t bytes_done;
};

static void block_sync_end(struct o0_bio *bio, o0_block_status status,
                           size_t bytes_done) {
    struct block_sync_state *state = (struct block_sync_state *)bio->private;
    if (state == NULL) {
        return;
    }
    state->done = true;
    state->status = status;
    state->bytes_done = bytes_done;
}

static o0_status block_sync_transfer(struct o0_block_dev *dev, o0_block_op op,
                                     uint64_t lba, void *buffer, size_t bytes) {
    if (dev == NULL) {
        return O0_EINVAL;
    }
    if (op != O0_BLOCK_FLUSH) {
        if (buffer == NULL || bytes == 0) {
            return O0_EINVAL;
        }
        if (bytes > O0_PAGE_SIZE) {
            return O0_ERANGE;
        }
        if (bytes % dev->geo.logical_block_size != 0) {
            return O0_EINVAL;
        }
    }

    struct block_sync_state state = { false, O0_BLOCK_OK, 0 };
    struct o0_bio_vec vec = { buffer, 0, bytes };
    struct o0_bio bio;
    memset(&bio, 0, sizeof(bio));
    bio.op = op;
    bio.lba = lba;
    bio.nr_vecs = (op == O0_BLOCK_FLUSH) ? 0 : 1;
    bio.vecs = (op == O0_BLOCK_FLUSH) ? NULL : &vec;
    bio.end_io = block_sync_end;
    bio.private = &state;

    o0_status st = o0_block_submit_bio(dev, &bio);
    if (st != O0_OK) {
        return st;
    }

    struct o0_block_queue *queue = &dev->queues[bio.queue_id];
    for (uint32_t spin = 0; spin < O0_BLOCK_SYNC_SPINS && !state.done; ++spin) {
        if (queue->ops->poll != NULL) {
            (void)queue->ops->poll(queue, O0_BLOCK_SYNC_POLL_BUDGET);
        }
    }

    if (!state.done) {
        return O0_ETIMEDOUT;
    }
    if (state.status != O0_BLOCK_OK) {
        return block_status_to_status(state.status);
    }
    if (op != O0_BLOCK_FLUSH && state.bytes_done != bytes) {
        return O0_EIO;
    }
    return O0_OK;
}

o0_status o0_block_read(struct o0_block_dev *dev, uint64_t lba, void *buffer,
                        size_t bytes) {
    return block_sync_transfer(dev, O0_BLOCK_READ, lba, buffer, bytes);
}

o0_status o0_block_write(struct o0_block_dev *dev, uint64_t lba,
                         const void *buffer, size_t bytes) {
    return block_sync_transfer(dev, O0_BLOCK_WRITE, lba, (void *)buffer, bytes);
}

o0_status o0_block_flush(struct o0_block_dev *dev) {
    if (dev == NULL) {
        return O0_EINVAL;
    }
    return block_sync_transfer(dev, O0_BLOCK_FLUSH, 0, NULL, 0);
}
