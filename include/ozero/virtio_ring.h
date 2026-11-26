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
#endif
