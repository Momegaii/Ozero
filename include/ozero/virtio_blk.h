/*
 * Ozero (o0) virtio-blk driver interface (Wave 4.2).
 *
 * The driver registers a real o0_block_dev, so the block layer's validation,
 * timeout, quarantine and cancellation rules apply unchanged. It speaks the
 * legacy virtio PCI transport (I/O BAR0 + PFN queue programming); the modern
 * transport is not implemented yet and probe reports O0_ENOTSUP rather than
 * pretending otherwise.
 *
 * Completion: one request is in flight per device. The MSI vector (or the
 * legacy line, when no MSI capability exists) runs an interrupt handler that
 * reads and acknowledges the device ISR. The submitting call also polls the
 * used ring, so a missing or misrouted interrupt surfaces as a timeout instead
 * of a hang.
 */
#ifndef O0_VIRTIO_BLK_H
#define O0_VIRTIO_BLK_H

#include <stdint.h>

#include "ozero/block.h"
#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Probes PCI for a virtio block device and registers it under `name` when
 * found. Returns O0_ENOENT when no device is present and O0_ENOTSUP when the
 * device only exposes the modern transport. */
O0_NODISCARD o0_status o0_virtio_blk_probe(const char *name,
                                           struct o0_block_dev **out);

/* Wave 4.2 injected-error test: issues one read past the end of the medium
 * directly on the virtqueue and verifies the device reports a failure and the
 * driver decodes it. Returns O0_OK when the error path worked. */
O0_NODISCARD o0_status o0_virtio_blk_inject_error(struct o0_block_dev *dev);

/* Interrupts taken by the driver since probe (diagnostics). */
uint64_t o0_virtio_blk_irq_count(struct o0_block_dev *dev);

#ifdef __cplusplus
}
#endif

#endif /* O0_VIRTIO_BLK_H */
