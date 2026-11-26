/* Modern virtio split virtqueue: configuration, notification and ring runtime.
 *
 * The queue is programmed through the common-configuration registers and its
 * rings live in driver-owned, physically contiguous memory. Nothing here
 * assumes legacy PFN programming: descriptor, available and used addresses are
 * written as separate 64-bit values and the queue is only enabled after every
 * field has been accepted.
 *
 * The ring runtime tracks descriptor ownership explicitly. A buffer or ring is
 * never released while the device may still write to it; on a timeout the
 * caller resets the device (which invalidates all device-side queue state) and
 * then calls o0_virtio_vq_reinit().
 */
#ifndef O0_VIRTIO_QUEUE_H
#define O0_VIRTIO_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/status.h"
#include "ozero/virtio_pci_mmio.h"
#include "ozero/virtio_ring.h"

/* A split virtqueue needs at least a header, a data and a status descriptor. */
#define O0_VIRTIO_QUEUE_MIN_SIZE 4u
#define O0_VIRTIO_QUEUE_MAX_SIZE 256u

/* ---- Pure helpers (host tested) ---- */

/* Picks the largest power-of-two size that is <= device_max and <= driver_max.
 * A device maximum of zero or a non-power-of-two maximum is rejected: the
 * transport must not invent a queue size the device did not advertise. */
bool o0_virtio_queue_size_choose(uint16_t device_max, uint16_t driver_max,
                                 uint16_t *out);

/* notification offset = queue_notify_off * multiplier, and the 16-bit queue
 * index written there must lie inside the notification region. */
bool o0_virtio_notify_offset(uint16_t queue_notify_off, uint32_t multiplier,
                             uint32_t region_length, uint32_t *out);

/* A DMA address range is usable when it is non-zero, aligned, entirely inside
 * the device address mask and does not wrap. */
bool o0_virtio_queue_address_ok(uint64_t addr, uint64_t bytes, uint64_t addr_mask,
                                size_t alignment);

/* ---- Queue configuration ---- */

struct o0_virtio_queue_plan {
    uint16_t index;
    uint16_t size;
    uint64_t desc_phys;
    uint64_t avail_phys;
    uint64_t used_phys;
    uint64_t addr_mask;
};

struct o0_virtio_queue_info {
    uint16_t index;
    uint16_t size;
    uint16_t notify_off;
};

/* Selects the queue, validates the device maximum size and the plan, refuses to
 * reconfigure an enabled queue, checks every DMA address against the device
 * mask and alignment, programs the three 64-bit addresses, then enables the
 * queue and verifies it reads back enabled. */
O0_NODISCARD o0_status o0_virtio_queue_configure(const struct o0_virtio_regs *regs,
                                                 const struct o0_virtio_queue_plan *plan,
                                                 struct o0_virtio_queue_info *out);

/* ---- Ring runtime ---- */

struct o0_virtio_vq {
    struct o0_vring_desc *desc;
    struct o0_vring_avail *avail;
    struct o0_vring_used *used;
    uint16_t size;
    uint16_t next_avail; /* driver copy of avail->idx */
    uint16_t last_used;  /* completions consumed */
    uint16_t free_head;
    uint16_t num_free;
    uint16_t free_next[O0_VIRTIO_QUEUE_MAX_SIZE];
};

/* Places the three rings inside `base` using the modern split layout and resets
 * the descriptor free list. `base` must be zeroed by the caller and hold at
 * least the layout total. */
bool o0_virtio_vq_init(struct o0_virtio_vq *vq, void *base, uint16_t size);

/* Re-establishes driver-side bookkeeping after a device reset. Only valid once
 * the device is known quiescent. */
void o0_virtio_vq_reinit(struct o0_virtio_vq *vq);

/* Allocates a chain of `count` descriptors. Returns false when the chain cannot
 * be satisfied, so a caller never silently reuses an in-flight descriptor. */
bool o0_virtio_vq_alloc(struct o0_virtio_vq *vq, uint16_t count, uint16_t *head);

/* Fills one descriptor of an allocated chain. */
void o0_virtio_vq_set(struct o0_virtio_vq *vq, uint16_t idx, uint64_t addr,
                      uint32_t len, uint16_t flags, uint16_t next);

/* Publishes the chain head in the available ring. A release barrier separates
 * the descriptor and ring-entry writes from the index update. */
bool o0_virtio_vq_publish(struct o0_virtio_vq *vq, uint16_t head);

/* Returns a consumed chain to the free list. */
void o0_virtio_vq_release(struct o0_virtio_vq *vq, uint16_t head, uint16_t count);

/* Descriptors the device may still be reading or writing. */
uint16_t o0_virtio_vq_outstanding(const struct o0_virtio_vq *vq);

enum o0_virtio_vq_poll {
    O0_VIRTIO_VQ_IDLE = 0,   /* device has not completed anything yet */
    O0_VIRTIO_VQ_DONE,       /* completion consumed, out_len valid */
    O0_VIRTIO_VQ_BAD_ID,     /* completion named a descriptor we did not submit */
    O0_VIRTIO_VQ_OVERRUN     /* used ring advanced past the queue size */
};

/* Consumes one completion. A completion is only accepted when its id matches
 * the chain that was submitted; anything else is reported, not trusted. */
enum o0_virtio_vq_poll o0_virtio_vq_poll(struct o0_virtio_vq *vq,
                                         uint16_t expected_head, uint32_t *out_len);

/* Writes the 16-bit queue index at the device-provided notification offset.
 * The computed access must lie inside the notification region. */
O0_NODISCARD o0_status o0_virtio_notify(const struct o0_virtio_regs *notify_regs,
                                        uint16_t queue_index, uint16_t notify_off,
                                        uint32_t multiplier);

#endif /* O0_VIRTIO_QUEUE_H */
