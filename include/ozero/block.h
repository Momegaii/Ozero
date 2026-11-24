/*
 * Ozero (o0) device-independent block I/O contract.
 *
 * The block layer owns request validation, queue selection, merging and
 * partition/media policy.  A protocol driver owns command translation,
 * transport queues, DMA mapping and completion.  Media type (rotational or
 * not) is a property of the device, never of the protocol.
 */
#ifndef O0_BLOCK_H
#define O0_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ozero/platform.h>

#ifdef __cplusplus
extern "C" {
#endif

#define O0_BLOCK_NAME_MAX 32
#define O0_BLOCK_MAX_QUEUES 64

typedef enum o0_block_op {
    O0_BLOCK_READ = 0,
    O0_BLOCK_WRITE,
    O0_BLOCK_FLUSH,
    O0_BLOCK_DISCARD,
    O0_BLOCK_WRITE_ZEROES,
    O0_BLOCK_ZONE_OPEN,
    O0_BLOCK_ZONE_CLOSE,
    O0_BLOCK_ZONE_FINISH,
    O0_BLOCK_ZONE_RESET,
    O0_BLOCK_ZONE_APPEND
} o0_block_op;

typedef enum o0_block_status {
    O0_BLOCK_OK = 0,
    O0_BLOCK_ERR_IO,
    O0_BLOCK_ERR_TIMEOUT,
    O0_BLOCK_ERR_MEDIUM,
    O0_BLOCK_ERR_TRANSPORT,
    O0_BLOCK_ERR_RESET,
    O0_BLOCK_ERR_INVAL,
    O0_BLOCK_ERR_NOTSUP,
    O0_BLOCK_ERR_AGAIN
} o0_block_status;

#define O0_BLOCK_F_FLUSH        (1ull << 0)
#define O0_BLOCK_F_FUA          (1ull << 1)
#define O0_BLOCK_F_DISCARD      (1ull << 2)
#define O0_BLOCK_F_WRITE_ZEROES (1ull << 3)
#define O0_BLOCK_F_NCQ          (1ull << 4)
#define O0_BLOCK_F_ZONED        (1ull << 5)
#define O0_BLOCK_F_REMOVABLE    (1ull << 6)
#define O0_BLOCK_F_ROTATIONAL   (1ull << 7)
#define O0_BLOCK_F_POLL         (1ull << 8)
#define O0_BLOCK_F_DMA_COHERENT (1ull << 9)
#define O0_BLOCK_F_METADATA     (1ull << 10)

#define O0_BIO_F_FUA     (1u << 0)
#define O0_BIO_F_SYNC    (1u << 1)
#define O0_BIO_F_META    (1u << 2)
#define O0_BIO_F_NOMERGE (1u << 3)

struct o0_block_dev;
struct o0_block_queue;
struct o0_bio;

typedef void (*o0_bio_end_t)(struct o0_bio *bio,
                             o0_block_status status,
                             size_t bytes_done);

struct o0_bio_vec {
    void *page;
    size_t offset;
    size_t len;
};

/*
 * A bio may span more than one DMA segment.  A1's generic request carries one
 * contiguous buffer and has no SG pointer, so the protocol driver splits a
 * large transfer into multiple o0_dma_buffer objects and multiple
 * o0_io_request objects.  o0_bio_vec describes logical pages; it is not a
 * hardware SG entry.
 */
struct o0_bio {
    /* One A1 generic request object.  io.complete is the only A1 completion
     * callback; the protocol driver's trampoline calls end_io. */
    struct o0_io_request io;
    o0_block_op op;
    uint16_t flags;
    uint16_t queue_id;
    uint32_t tag;
    uint32_t nr_vecs;
    uint64_t lba;
    struct o0_bio_vec *vecs;
    o0_bio_end_t end_io;
    void *private;
};

struct o0_block_geometry {
    uint32_t logical_block_size;
    uint32_t physical_block_size;
    uint64_t nr_sectors;
    uint32_t max_hw_sectors;
    uint16_t max_segments;
    uint32_t max_segment_size;
    uint32_t dma_alignment;
    uint32_t queue_depth;
    uint64_t features;
};

struct o0_block_queue_ops {
    o0_status (*queue_rq)(struct o0_block_queue *queue, struct o0_bio *bio);
    o0_status (*poll)(struct o0_block_queue *queue, int budget);
    o0_status (*timeout)(struct o0_block_queue *queue, struct o0_bio *bio);
};

struct o0_block_queue {
    uint16_t id;
    uint32_t depth;
    const struct o0_block_queue_ops *ops;
    void *driver_data;
};

struct o0_block_ops {
    /* Block-level queue reset policy.  A1 owns o0_device_dma_quiesce and
     * o0_device_reset; this callback only resets A3 protocol state. */
    o0_status (*reset)(struct o0_block_dev *dev);
    o0_status (*flush)(struct o0_block_dev *dev);
    o0_status (*discard)(struct o0_block_dev *dev,
                         uint64_t lba,
                         uint64_t nr_sectors);
    o0_status (*write_zeroes)(struct o0_block_dev *dev,
                              uint64_t lba,
                              uint64_t nr_sectors,
                              bool no_unmap);
    o0_status (*open)(struct o0_block_dev *dev, bool exclusive);
    void (*close)(struct o0_block_dev *dev);
    bool (*media_changed)(struct o0_block_dev *dev);
    o0_status (*get_geometry)(struct o0_block_dev *dev,
                              struct o0_block_geometry *out);
    o0_status (*set_features)(struct o0_block_dev *dev, uint64_t features);
};

struct o0_block_dev {
    char name[O0_BLOCK_NAME_MAX];
    struct o0_block_geometry geo;
    unsigned int nr_queues;
    struct o0_block_queue *queues;
    const struct o0_block_ops *ops;
    void *driver_data;
};

/* Registration is valid only after the device is identified and quiescent. */
o0_status o0_block_register(struct o0_block_dev *dev);
void o0_block_unregister(struct o0_block_dev *dev);

/* Submit a bio.  The caller must not touch it until end_io runs. */
o0_status o0_block_submit_bio(struct o0_block_dev *dev, struct o0_bio *bio);

/* Called by the protocol driver from IRQ, poll or process context. Exactly one
 * completion is delivered per submitted bio; a second call is ignored. */
void o0_block_complete_bio(struct o0_bio *bio,
                           o0_block_status status,
                           size_t bytes_done);

/* ---- Synchronous single-segment I/O (Stage 4) ----
 *
 * Builds and submits one bio over one contiguous buffer, then waits for the
 * completion. `bytes` must be a non-zero multiple of the logical block size and
 * at most one page, and the transfer must stay inside the device. The driver
 * must either complete the bio before queue_rq returns or provide a poll
 * callback; otherwise the wait ends with O0_ETIMEDOUT. The caller's buffer must
 * remain valid until the call returns. The implementation uses an internal
 * page-sized bounce buffer, so a driver never retains the caller's buffer.
 * If the wait times out, the request and bounce buffer remain quarantined
 * until late completion. A queue timeout callback may release them early only
 * by returning O0_OK after cancellation, DMA quiescence, and completion-callback
 * quiescence are all guaranteed. A timeout callback returning any other status
 * does not transfer ownership back to the caller.
 *
 * This is the Stage 4 interface: it exists so the block layer and the
 * filesystem can be exercised without an asynchronous I/O scheduler. It is not
 * the future native I/O ABI. */
O0_NODISCARD o0_status o0_block_read(struct o0_block_dev *dev, uint64_t lba,
                                     void *buffer, size_t bytes);
O0_NODISCARD o0_status o0_block_write(struct o0_block_dev *dev, uint64_t lba,
                                      const void *buffer, size_t bytes);
O0_NODISCARD o0_status o0_block_flush(struct o0_block_dev *dev);

/* ---- Registry inspection ---- */

/* Registered devices, in registration order. Used for diagnostics and tests;
 * registration itself is a single-threaded boot-time operation. */
size_t o0_block_device_count(void);
struct o0_block_dev *o0_block_device_by_index(size_t index);

static inline uint64_t o0_block_lba_to_bytes(const struct o0_block_dev *dev,
                                              uint64_t lba)
{
    return lba * (uint64_t)dev->geo.logical_block_size;
}

#ifdef __cplusplus
}
#endif

#endif /* O0_BLOCK_H */
