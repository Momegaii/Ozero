/* Internal interface between the virtio-blk transports.
 *
 * The public API in include/ozero/virtio_blk.h stays transport neutral. The
 * legacy I/O-port driver and the modern MMIO-capability driver each implement a
 * backend and the dispatcher in virtio_blk.c chooses between them after probing
 * PCI. The two transports never share queue programming.
 */
#ifndef O0_VIRTIO_BLK_INTERNAL_H
#define O0_VIRTIO_BLK_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "ozero/block.h"
#include "ozero/status.h"

/* Legacy transport (I/O BAR0, PFN queue programming). */
O0_NODISCARD o0_status o0_virtio_blk_legacy_probe(const char *name,
                                                  struct o0_block_dev **out);
O0_NODISCARD o0_status o0_virtio_blk_legacy_inject_error(struct o0_block_dev *dev);
uint64_t o0_virtio_blk_legacy_irq_count(struct o0_block_dev *dev);

/* Modern transport (PCI vendor capabilities, 64-bit queue addresses). */
O0_NODISCARD o0_status o0_virtio_blk_modern_probe(const char *name,
                                                  struct o0_block_dev **out);
O0_NODISCARD o0_status o0_virtio_blk_modern_inject_error(struct o0_block_dev *dev);
uint64_t o0_virtio_blk_modern_irq_count(struct o0_block_dev *dev);

#endif /* O0_VIRTIO_BLK_INTERNAL_H */
