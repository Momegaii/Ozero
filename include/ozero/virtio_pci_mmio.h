/* Modern virtio PCI BAR mappings, register interface and common configuration.
 *
 * The transport talks to the device through `struct o0_virtio_regs`, a small
 * register interface whose default implementation is uncached MMIO. Host tests
 * substitute a device model for that interface, so feature and queue logic is
 * exercised against registers that behave like a device (feature banks, status
 * latching, queue enable) instead of a flat byte array.
 *
 * Offsets and status bits come from the virtio 1.1 specification, used here as a
 * device contract rather than as an architecture.
 */
#ifndef O0_VIRTIO_PCI_MMIO_H
#define O0_VIRTIO_PCI_MMIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/pci.h"
#include "ozero/status.h"
#include "ozero/virtio_pci_cap.h"

/* ---- Common configuration layout (virtio 1.1 §4.1.4.3) ---- */

#define O0_VIRTIO_COMMON_DEVICE_FEATURE_SELECT 0x00u
#define O0_VIRTIO_COMMON_DEVICE_FEATURE        0x04u
#define O0_VIRTIO_COMMON_DRIVER_FEATURE_SELECT 0x08u
#define O0_VIRTIO_COMMON_DRIVER_FEATURE        0x0cu
#define O0_VIRTIO_COMMON_MSIX_CONFIG           0x10u
#define O0_VIRTIO_COMMON_NUM_QUEUES            0x12u
#define O0_VIRTIO_COMMON_DEVICE_STATUS         0x14u
#define O0_VIRTIO_COMMON_CONFIG_GENERATION     0x15u
#define O0_VIRTIO_COMMON_QUEUE_SELECT          0x16u
#define O0_VIRTIO_COMMON_QUEUE_SIZE            0x18u
#define O0_VIRTIO_COMMON_QUEUE_MSIX_VECTOR     0x1au
#define O0_VIRTIO_COMMON_QUEUE_ENABLE          0x1cu
#define O0_VIRTIO_COMMON_QUEUE_NOTIFY_OFF      0x1eu
#define O0_VIRTIO_COMMON_QUEUE_DESC            0x20u
#define O0_VIRTIO_COMMON_QUEUE_DRIVER          0x28u
#define O0_VIRTIO_COMMON_QUEUE_DEVICE          0x30u
#define O0_VIRTIO_COMMON_MIN_LENGTH            0x38u

/* ---- Device status bits ---- */

#define O0_VIRTIO_STATUS_ACKNOWLEDGE 0x01u
#define O0_VIRTIO_STATUS_DRIVER      0x02u
#define O0_VIRTIO_STATUS_DRIVER_OK   0x04u
#define O0_VIRTIO_STATUS_FEATURES_OK 0x08u
#define O0_VIRTIO_STATUS_NEEDS_RESET 0x40u
#define O0_VIRTIO_STATUS_FAILED      0x80u

/* ---- Feature bit numbers (not masks) ---- */

#define O0_VIRTIO_F_VERSION_1 32u

/* Bounded reset poll budget: a device that never clears device_status is
 * reported as timed out rather than hanging the boot. */
#define O0_VIRTIO_RESET_POLLS 100000u

/* ---- Register interface ---- */

/* Every accessor is bounds checked and width/alignment checked by the backend.
 * A false return means the access was refused and no bus cycle happened. */
struct o0_virtio_reg_iface {
    bool (*read8)(volatile void *ctx, size_t length, size_t offset, uint8_t *out);
    bool (*read16)(volatile void *ctx, size_t length, size_t offset, uint16_t *out);
    bool (*read32)(volatile void *ctx, size_t length, size_t offset, uint32_t *out);
    bool (*read64)(volatile void *ctx, size_t length, size_t offset, uint64_t *out);
    bool (*write8)(volatile void *ctx, size_t length, size_t offset, uint8_t value);
    bool (*write16)(volatile void *ctx, size_t length, size_t offset, uint16_t value);
    bool (*write32)(volatile void *ctx, size_t length, size_t offset, uint32_t value);
    bool (*write64)(volatile void *ctx, size_t length, size_t offset, uint64_t value);
    void (*write_barrier)(void);
    void (*read_barrier)(void);
};

struct o0_virtio_regs {
    const struct o0_virtio_reg_iface *iface;
    volatile void *base;
    size_t length;
};

extern const struct o0_virtio_reg_iface o0_virtio_mmio_reg_iface;

static inline bool o0_virtio_reg_read8(const struct o0_virtio_regs *regs,
                                       size_t offset, uint8_t *out) {
    return regs != NULL && regs->iface != NULL && regs->iface->read8 != NULL &&
           regs->iface->read8(regs->base, regs->length, offset, out);
}

static inline bool o0_virtio_reg_read16(const struct o0_virtio_regs *regs,
                                        size_t offset, uint16_t *out) {
    return regs != NULL && regs->iface != NULL && regs->iface->read16 != NULL &&
           regs->iface->read16(regs->base, regs->length, offset, out);
}

static inline bool o0_virtio_reg_read32(const struct o0_virtio_regs *regs,
                                        size_t offset, uint32_t *out) {
    return regs != NULL && regs->iface != NULL && regs->iface->read32 != NULL &&
           regs->iface->read32(regs->base, regs->length, offset, out);
}

static inline bool o0_virtio_reg_read64(const struct o0_virtio_regs *regs,
                                        size_t offset, uint64_t *out) {
    return regs != NULL && regs->iface != NULL && regs->iface->read64 != NULL &&
           regs->iface->read64(regs->base, regs->length, offset, out);
}

static inline bool o0_virtio_reg_write8(const struct o0_virtio_regs *regs,
                                        size_t offset, uint8_t value) {
    return regs != NULL && regs->iface != NULL && regs->iface->write8 != NULL &&
           regs->iface->write8(regs->base, regs->length, offset, value);
}

static inline bool o0_virtio_reg_write16(const struct o0_virtio_regs *regs,
                                         size_t offset, uint16_t value) {
    return regs != NULL && regs->iface != NULL && regs->iface->write16 != NULL &&
           regs->iface->write16(regs->base, regs->length, offset, value);
}

static inline bool o0_virtio_reg_write32(const struct o0_virtio_regs *regs,
                                         size_t offset, uint32_t value) {
    return regs != NULL && regs->iface != NULL && regs->iface->write32 != NULL &&
           regs->iface->write32(regs->base, regs->length, offset, value);
}

static inline bool o0_virtio_reg_write64(const struct o0_virtio_regs *regs,
                                         size_t offset, uint64_t value) {
    return regs != NULL && regs->iface != NULL && regs->iface->write64 != NULL &&
           regs->iface->write64(regs->base, regs->length, offset, value);
}

static inline void o0_virtio_reg_write_barrier(const struct o0_virtio_regs *regs) {
    if (regs != NULL && regs->iface != NULL && regs->iface->write_barrier != NULL) {
        regs->iface->write_barrier();
    }
}

static inline void o0_virtio_reg_read_barrier(const struct o0_virtio_regs *regs) {
    if (regs != NULL && regs->iface != NULL && regs->iface->read_barrier != NULL) {
        regs->iface->read_barrier();
    }
}

/* ---- MMIO accessors (default register interface) ---- */

O0_NODISCARD bool o0_virtio_mmio_read8(volatile void *base, size_t length,
                                       size_t offset, uint8_t *out);
O0_NODISCARD bool o0_virtio_mmio_read16(volatile void *base, size_t length,
                                        size_t offset, uint16_t *out);
O0_NODISCARD bool o0_virtio_mmio_read32(volatile void *base, size_t length,
                                        size_t offset, uint32_t *out);
O0_NODISCARD bool o0_virtio_mmio_read64(volatile void *base, size_t length,
                                        size_t offset, uint64_t *out);
O0_NODISCARD bool o0_virtio_mmio_write8(volatile void *base, size_t length,
                                        size_t offset, uint8_t value);
O0_NODISCARD bool o0_virtio_mmio_write16(volatile void *base, size_t length,
                                         size_t offset, uint16_t value);
O0_NODISCARD bool o0_virtio_mmio_write32(volatile void *base, size_t length,
                                         size_t offset, uint32_t value);
O0_NODISCARD bool o0_virtio_mmio_write64(volatile void *base, size_t length,
                                         size_t offset, uint64_t value);
void o0_virtio_mmio_write_barrier(void);
void o0_virtio_mmio_read_barrier(void);

/* ---- BAR mapping ---- */

/* Each capability BAR is mapped once as a dedicated uncached kernel window at
 * `window_base + bar * window_stride`; a window is never an HHDM alias. BAR
 * metadata comes from o0_pci_probe_bar, which is the implemented PCI service.
 * On failure, mappings already acquired are unwound. The caller must quiesce
 * the device before unmapping. */
struct o0_virtio_pci_mapping {
    struct o0_pci_bdf bdf;
    uint8_t header_type;
    uint64_t window_base;
    uint64_t window_stride;
    void *bar_virt[O0_VIRTIO_PCI_CFG_MAX_BARS];
    size_t bar_length[O0_VIRTIO_PCI_CFG_MAX_BARS];
};

O0_NODISCARD o0_status o0_virtio_pci_map_caps(const struct o0_pci_bdf *bdf,
                                              uint8_t header_type,
                                              const struct o0_virtio_pci_caps *caps,
                                              uint64_t window_base,
                                              uint64_t window_stride,
                                              struct o0_virtio_pci_mapping *mapping);
o0_status o0_virtio_pci_unmap_caps(struct o0_virtio_pci_mapping *mapping);

/* Builds the register interface for the common configuration region. */
O0_NODISCARD bool o0_virtio_pci_common_regs(const struct o0_virtio_pci_caps *caps,
                                            const struct o0_virtio_pci_mapping *mapping,
                                            struct o0_virtio_regs *out);

/* Builds the register interface for a region (notify or ISR). */
O0_NODISCARD bool o0_virtio_pci_region_regs(const struct o0_virtio_pci_region *region,
                                            const struct o0_virtio_pci_mapping *mapping,
                                            struct o0_virtio_regs *out);

/* ---- Device lifecycle ---- */

/* Writes device_status = 0 and waits, bounded, for the device to report 0.
 * Invalidates all device-side queue state; the caller must not free queue or
 * request memory until this returns O0_OK. */
O0_NODISCARD o0_status o0_virtio_reset(const struct o0_virtio_regs *regs);

/* ACKNOWLEDGE then DRIVER, then reads both device feature banks. Leaves the
 * device in the DRIVER state so the caller can choose which offered features to
 * accept before o0_virtio_negotiate(). */
O0_NODISCARD o0_status o0_virtio_features(const struct o0_virtio_regs *regs,
                                          uint32_t *device_low, uint32_t *device_high);

/* Re-establishes ACKNOWLEDGE/DRIVER, re-reads the offer, rejects a device that
 * does not offer VERSION_1, rejects a request for a feature the device did not
 * offer, programs driver features, then sets and re-reads FEATURES_OK. On
 * failure the device is left in the FAILED state and the caller must reset it. */
O0_NODISCARD o0_status o0_virtio_negotiate(const struct o0_virtio_regs *regs,
                                           uint32_t accept_low, uint32_t accept_high,
                                           uint32_t *device_low, uint32_t *device_high);

/* Sets DRIVER_OK. Only valid after a successful negotiation. */
O0_NODISCARD o0_status o0_virtio_driver_ok(const struct o0_virtio_regs *regs);

#endif /* O0_VIRTIO_PCI_MMIO_H */
