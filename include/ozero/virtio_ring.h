/* Pure legacy virtio PCI split-ring layout arithmetic. */
#ifndef O0_VIRTIO_RING_H
#define O0_VIRTIO_RING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define O0_VIRTIO_LEGACY_PAGE_SIZE 4096u
static inline bool o0_virtio_legacy_ring_layout(uint16_t n, size_t *used_offset,
                                                size_t *total_bytes) {
    if (n == 0u || used_offset == NULL || total_bytes == NULL) return false;
    size_t desc = (size_t)n * 16u;
    size_t avail = desc + 2u * (3u + (size_t)n);
    size_t used = (avail + O0_VIRTIO_LEGACY_PAGE_SIZE - 1u) &
                  ~(size_t)(O0_VIRTIO_LEGACY_PAGE_SIZE - 1u);
    size_t used_bytes = 2u * (3u + 4u * (size_t)n);
    if (used > SIZE_MAX - used_bytes) return false;
    *used_offset = used;
    *total_bytes = used + used_bytes;
    return true;
}

/* Modern split virtqueue wire layout: descriptor table (16-byte aligned),
 * available ring (2-byte aligned), then used ring (4-byte aligned). Unlike the
 * legacy PFN layout, the used ring is not forced to a page boundary. */
static inline bool o0_virtio_split_ring_layout(uint16_t n, size_t *avail_offset,
                                               size_t *used_offset, size_t *total_bytes) {
    if (n == 0u || avail_offset == NULL || used_offset == NULL || total_bytes == NULL)
        return false;
    size_t desc_bytes = (size_t)n * 16u;
    size_t avail = (desc_bytes + 1u) & ~(size_t)1u;
    size_t avail_bytes = 4u + (size_t)n * 2u;
    if (avail > SIZE_MAX - avail_bytes) return false;
    size_t used = (avail + avail_bytes + 3u) & ~(size_t)3u;
    size_t used_bytes = 4u + (size_t)n * 8u;
    if (used > SIZE_MAX - used_bytes) return false;
    *avail_offset = avail;
    *used_offset = used;
    *total_bytes = used + used_bytes;
    return true;
}

/* ---- Wire structures (virtio 1.1 §2.7) ---- */

#define O0_VRING_DESC_F_NEXT  1u
#define O0_VRING_DESC_F_WRITE 2u

struct o0_vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct o0_vring_avail {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile uint16_t ring[];
};

struct o0_vring_used_elem {
    uint32_t id;
    uint32_t len;
};

struct o0_vring_used {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile struct o0_vring_used_elem ring[];
};

_Static_assert(sizeof(struct o0_vring_desc) == 16u, "descriptor must be 16 bytes");
_Static_assert(sizeof(struct o0_vring_used_elem) == 8u, "used element must be 8 bytes");

#endif
