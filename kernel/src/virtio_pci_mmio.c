/* Modern virtio PCI transport: BAR mapping, common configuration and the
 * feature-negotiation handshake.
 *
 * Ordering rules implemented here:
 *   - queue and request memory is only released after o0_virtio_reset() has
 *     observed device_status == 0, so the device cannot still be DMAing into a
 *     buffer the driver has freed;
 *   - driver features are programmed only after the device's offer has been
 *     read and checked, and FEATURES_OK is re-read before it is trusted;
 *   - every MMIO access is width and alignment correct and bounds checked
 *     against the mapped region.
 */

#include "ozero/virtio_pci_mmio.h"

#include <string.h>

#include "ozero/types.h"

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Ozero modern virtio MMIO accessors require a little-endian target"
#endif

static bool mmio_range(volatile void *base, size_t length, size_t offset,
                       size_t width, size_t alignment) {
    return base != NULL && alignment != 0u && offset % alignment == 0u &&
           offset <= length && width <= length - offset;
}

bool o0_virtio_mmio_read8(volatile void *base, size_t length, size_t offset,
                          uint8_t *out) {
    if (out == NULL || !mmio_range(base, length, offset, 1u, 1u)) return false;
    *out = *(volatile uint8_t *)((volatile uint8_t *)base + offset);
    return true;
}

bool o0_virtio_mmio_read16(volatile void *base, size_t length, size_t offset,
                           uint16_t *out) {
    if (out == NULL || !mmio_range(base, length, offset, 2u, 2u)) return false;
    *out = *(volatile uint16_t *)((volatile uint8_t *)base + offset);
    return true;
}

bool o0_virtio_mmio_read32(volatile void *base, size_t length, size_t offset,
                           uint32_t *out) {
    if (out == NULL || !mmio_range(base, length, offset, 4u, 4u)) return false;
    *out = *(volatile uint32_t *)((volatile uint8_t *)base + offset);
    return true;
}

bool o0_virtio_mmio_read64(volatile void *base, size_t length, size_t offset,
                           uint64_t *out) {
    if (out == NULL || !mmio_range(base, length, offset, 8u, 8u)) return false;
    *out = *(volatile uint64_t *)((volatile uint8_t *)base + offset);
    return true;
}

bool o0_virtio_mmio_write8(volatile void *base, size_t length, size_t offset,
                           uint8_t value) {
    if (!mmio_range(base, length, offset, 1u, 1u)) return false;
    *(volatile uint8_t *)((volatile uint8_t *)base + offset) = value;
    return true;
}

bool o0_virtio_mmio_write16(volatile void *base, size_t length, size_t offset,
                            uint16_t value) {
    if (!mmio_range(base, length, offset, 2u, 2u)) return false;
    *(volatile uint16_t *)((volatile uint8_t *)base + offset) = value;
    return true;
}

bool o0_virtio_mmio_write32(volatile void *base, size_t length, size_t offset,
                            uint32_t value) {
    if (!mmio_range(base, length, offset, 4u, 4u)) return false;
    *(volatile uint32_t *)((volatile uint8_t *)base + offset) = value;
    return true;
}

bool o0_virtio_mmio_write64(volatile void *base, size_t length, size_t offset,
                            uint64_t value) {
    if (!mmio_range(base, length, offset, 8u, 8u)) return false;
    *(volatile uint64_t *)((volatile uint8_t *)base + offset) = value;
    return true;
}

/* x86-64 orders stores and loads, so these are compiler barriers with release
 * and acquire semantics. They are the only ordering primitives the transport
 * relies on; nothing here assumes a weaker memory model. */
void o0_virtio_mmio_write_barrier(void) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

void o0_virtio_mmio_read_barrier(void) {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

const struct o0_virtio_reg_iface o0_virtio_mmio_reg_iface = {
    .read8 = o0_virtio_mmio_read8,
    .read16 = o0_virtio_mmio_read16,
    .read32 = o0_virtio_mmio_read32,
    .read64 = o0_virtio_mmio_read64,
    .write8 = o0_virtio_mmio_write8,
    .write16 = o0_virtio_mmio_write16,
    .write32 = o0_virtio_mmio_write32,
    .write64 = o0_virtio_mmio_write64,
    .write_barrier = o0_virtio_mmio_write_barrier,
    .read_barrier = o0_virtio_mmio_read_barrier
};

static bool region_regs(const struct o0_virtio_pci_region *region,
                        const struct o0_virtio_pci_mapping *mapping,
                        size_t min_length, struct o0_virtio_regs *out) {
    if (region == NULL || mapping == NULL || out == NULL || !region->present ||
        region->bar >= O0_VIRTIO_PCI_CFG_MAX_BARS ||
        mapping->bar_virt[region->bar] == NULL ||
        region->length < min_length ||
        (uint64_t)region->offset > mapping->bar_length[region->bar] ||
        (uint64_t)region->length > mapping->bar_length[region->bar] -
                                     (uint64_t)region->offset) {
        return false;
    }
    out->iface = &o0_virtio_mmio_reg_iface;
    out->base = (volatile uint8_t *)mapping->bar_virt[region->bar] + region->offset;
    out->length = region->length;
    return true;
}

bool o0_virtio_pci_common_regs(const struct o0_virtio_pci_caps *caps,
                               const struct o0_virtio_pci_mapping *mapping,
                               struct o0_virtio_regs *out) {
    if (caps == NULL) return false;
    return region_regs(&caps->common, mapping, O0_VIRTIO_COMMON_MIN_LENGTH, out);
}

bool o0_virtio_pci_region_regs(const struct o0_virtio_pci_region *region,
                               const struct o0_virtio_pci_mapping *mapping,
                               struct o0_virtio_regs *out) {
    return region_regs(region, mapping, 1u, out);
}

/* ---- Device lifecycle ---- */

o0_status o0_virtio_reset(const struct o0_virtio_regs *regs) {
    if (regs == NULL || regs->iface == NULL) return O0_EINVAL;
    if (!o0_virtio_reg_write8(regs, O0_VIRTIO_COMMON_DEVICE_STATUS, 0u)) return O0_EIO;
    for (uint32_t spin = 0; spin < O0_VIRTIO_RESET_POLLS; ++spin) {
        uint8_t status = 0xFFu;
        if (!o0_virtio_reg_read8(regs, O0_VIRTIO_COMMON_DEVICE_STATUS, &status)) {
            return O0_EIO;
        }
        if (status == 0u) return O0_OK;
    }
    return O0_ETIMEDOUT;
}

static o0_status virtio_fail(const struct o0_virtio_regs *regs, o0_status reason) {
    (void)o0_virtio_reg_write8(regs, O0_VIRTIO_COMMON_DEVICE_STATUS,
                               O0_VIRTIO_STATUS_FAILED);
    return reason;
}

o0_status o0_virtio_features(const struct o0_virtio_regs *regs,
                             uint32_t *device_low, uint32_t *device_high) {
    if (regs == NULL || regs->iface == NULL || device_low == NULL ||
        device_high == NULL) {
        return O0_EINVAL;
    }
    if (!o0_virtio_reg_write8(regs, O0_VIRTIO_COMMON_DEVICE_STATUS,
                              O0_VIRTIO_STATUS_ACKNOWLEDGE) ||
        !o0_virtio_reg_write8(regs, O0_VIRTIO_COMMON_DEVICE_STATUS,
                              O0_VIRTIO_STATUS_ACKNOWLEDGE | O0_VIRTIO_STATUS_DRIVER)) {
        return virtio_fail(regs, O0_EIO);
    }
    uint32_t low = 0, high = 0;
    if (!o0_virtio_reg_write32(regs, O0_VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 0u) ||
        !o0_virtio_reg_read32(regs, O0_VIRTIO_COMMON_DEVICE_FEATURE, &low) ||
        !o0_virtio_reg_write32(regs, O0_VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 1u) ||
        !o0_virtio_reg_read32(regs, O0_VIRTIO_COMMON_DEVICE_FEATURE, &high)) {
        return virtio_fail(regs, O0_EIO);
    }
    *device_low = low;
    *device_high = high;
    return O0_OK;
}

o0_status o0_virtio_negotiate(const struct o0_virtio_regs *regs,
                              uint32_t accept_low, uint32_t accept_high,
                              uint32_t *device_low, uint32_t *device_high) {
    if (regs == NULL || regs->iface == NULL || device_low == NULL ||
        device_high == NULL) {
        return O0_EINVAL;
    }
    /* A modern transport driver must accept VERSION_1; asking for anything else
     * would silently fall back to legacy register semantics. */
    const uint32_t version1 = 1u << (O0_VIRTIO_F_VERSION_1 - 32u);
    if ((accept_high & version1) == 0u) return O0_EINVAL;

    uint32_t low = 0, high = 0;
    o0_status st = o0_virtio_features(regs, &low, &high);
    if (st != O0_OK) return st;
    if ((high & version1) == 0u) return virtio_fail(regs, O0_ENOTSUP);
    if ((accept_low & ~low) != 0u || (accept_high & ~high) != 0u) {
        return virtio_fail(regs, O0_ENOTSUP);
    }

    if (!o0_virtio_reg_write32(regs, O0_VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 0u) ||
        !o0_virtio_reg_write32(regs, O0_VIRTIO_COMMON_DRIVER_FEATURE, accept_low) ||
        !o0_virtio_reg_write32(regs, O0_VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 1u) ||
        !o0_virtio_reg_write32(regs, O0_VIRTIO_COMMON_DRIVER_FEATURE, accept_high)) {
        return virtio_fail(regs, O0_EIO);
    }

    uint8_t status = O0_VIRTIO_STATUS_ACKNOWLEDGE | O0_VIRTIO_STATUS_DRIVER |
                     O0_VIRTIO_STATUS_FEATURES_OK;
    if (!o0_virtio_reg_write8(regs, O0_VIRTIO_COMMON_DEVICE_STATUS, status)) {
        return virtio_fail(regs, O0_EIO);
    }
    uint8_t observed = 0;
    if (!o0_virtio_reg_read8(regs, O0_VIRTIO_COMMON_DEVICE_STATUS, &observed)) {
        return virtio_fail(regs, O0_EIO);
    }
    if ((observed & O0_VIRTIO_STATUS_FEATURES_OK) == 0u) {
        /* The device rejected the feature set. Its queue state is undefined, so
         * the caller must reset before retrying; nothing else is programmed. */
        return O0_ENOTSUP;
    }
    *device_low = low;
    *device_high = high;
    return O0_OK;
}

o0_status o0_virtio_driver_ok(const struct o0_virtio_regs *regs) {
    if (regs == NULL || regs->iface == NULL) return O0_EINVAL;
    uint8_t status = 0;
    if (!o0_virtio_reg_read8(regs, O0_VIRTIO_COMMON_DEVICE_STATUS, &status)) {
        return O0_EIO;
    }
    if ((status & O0_VIRTIO_STATUS_FEATURES_OK) == 0u) return O0_EINVAL;
    if ((status & O0_VIRTIO_STATUS_FAILED) != 0u) return O0_EIO;
    status = (uint8_t)(status | O0_VIRTIO_STATUS_DRIVER_OK);
    if (!o0_virtio_reg_write8(regs, O0_VIRTIO_COMMON_DEVICE_STATUS, status)) {
        return O0_EIO;
    }
    uint8_t observed = 0;
    if (!o0_virtio_reg_read8(regs, O0_VIRTIO_COMMON_DEVICE_STATUS, &observed) ||
        (observed & O0_VIRTIO_STATUS_DRIVER_OK) == 0u) {
        return O0_EIO;
    }
    return O0_OK;
}
