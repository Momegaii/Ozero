/* Host-test device model for the modern virtio common configuration.
 *
 * This is deliberately not a byte array. It implements the register semantics
 * the transport depends on:
 *   - two device feature banks selected by device_feature_select;
 *   - two driver feature banks selected by driver_feature_select, where only
 *     offered bits are latched;
 *   - device_status latching, including a device that clears FEATURES_OK when it
 *     refuses the negotiated feature set, and one that never completes reset;
 *   - per-queue state behind queue_select, with queue_size read-only and
 *     queue_enable read-only after the device has latched it;
 *   - fixed register widths: a wrong-width access is refused, so a driver that
 *     reads a 32-bit word from an 8-bit register fails the test.
 */
#ifndef O0_TESTS_VIRTIO_MOCK_H
#define O0_TESTS_VIRTIO_MOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ozero/virtio_pci_mmio.h"

#define VMOCK_QUEUES 4u

struct vmock_queue {
    uint16_t size;
    uint16_t enable;
    uint16_t notify_off;
    uint16_t msix_vector;
    uint64_t desc;
    uint64_t driver;
    uint64_t device;
};

struct vmock {
    uint32_t features[2];
    uint32_t driver_features[2];
    uint32_t device_feature_select;
    uint32_t driver_feature_select;
    uint8_t status;
    uint8_t config_generation;
    uint16_t num_queues;
    uint16_t queue_select;
    struct vmock_queue queue[VMOCK_QUEUES];
    bool reject_features_ok;
    bool stuck_reset;
    bool refuse_enable;
    bool use_forced_notify_off;
    uint32_t forced_notify_off;
    uint32_t accesses;
};

static inline struct vmock *vmock_of(volatile void *ctx) {
    return (struct vmock *)(uintptr_t)(void *)ctx;
}

static inline size_t vmock_width(size_t offset) {
    switch (offset) {
        case O0_VIRTIO_COMMON_DEVICE_FEATURE_SELECT:
        case O0_VIRTIO_COMMON_DEVICE_FEATURE:
        case O0_VIRTIO_COMMON_DRIVER_FEATURE_SELECT:
        case O0_VIRTIO_COMMON_DRIVER_FEATURE:
            return 4u;
        case O0_VIRTIO_COMMON_MSIX_CONFIG:
        case O0_VIRTIO_COMMON_NUM_QUEUES:
        case O0_VIRTIO_COMMON_QUEUE_SELECT:
        case O0_VIRTIO_COMMON_QUEUE_SIZE:
        case O0_VIRTIO_COMMON_QUEUE_MSIX_VECTOR:
        case O0_VIRTIO_COMMON_QUEUE_ENABLE:
        case O0_VIRTIO_COMMON_QUEUE_NOTIFY_OFF:
            return 2u;
        case O0_VIRTIO_COMMON_DEVICE_STATUS:
        case O0_VIRTIO_COMMON_CONFIG_GENERATION:
            return 1u;
        case O0_VIRTIO_COMMON_QUEUE_DESC:
        case O0_VIRTIO_COMMON_QUEUE_DRIVER:
        case O0_VIRTIO_COMMON_QUEUE_DEVICE:
            return 8u;
        default:
            return 0u;
    }
}

static inline struct vmock_queue *vmock_selected(struct vmock *m) {
    if (m->queue_select >= m->num_queues || m->queue_select >= VMOCK_QUEUES) return NULL;
    return &m->queue[m->queue_select];
}

static inline bool vmock_access_ok(struct vmock *m, size_t length, size_t offset,
                                   size_t width) {
    if (m == NULL || width == 0u || vmock_width(offset) != width) return false;
    if (offset % width != 0u) return false;
    if (offset > length || width > length - offset) return false;
    m->accesses++;
    return true;
}

static inline bool vmock_read(volatile void *ctx, size_t length, size_t offset,
                              size_t width, uint64_t *out) {
    struct vmock *m = vmock_of(ctx);
    if (out == NULL || !vmock_access_ok(m, length, offset, width)) return false;
    struct vmock_queue *q = vmock_selected(m);
    uint64_t value = 0;
    switch (offset) {
        case O0_VIRTIO_COMMON_DEVICE_FEATURE_SELECT:
            value = m->device_feature_select;
            break;
        case O0_VIRTIO_COMMON_DEVICE_FEATURE:
            value = m->features[m->device_feature_select & 1u];
            break;
        case O0_VIRTIO_COMMON_DRIVER_FEATURE_SELECT:
            value = m->driver_feature_select;
            break;
        case O0_VIRTIO_COMMON_DRIVER_FEATURE:
            value = m->driver_features[m->driver_feature_select & 1u];
            break;
        case O0_VIRTIO_COMMON_MSIX_CONFIG:
            value = 0xFFFFu;
            break;
        case O0_VIRTIO_COMMON_NUM_QUEUES:
            value = m->num_queues;
            break;
        case O0_VIRTIO_COMMON_DEVICE_STATUS:
            value = m->status;
            break;
        case O0_VIRTIO_COMMON_CONFIG_GENERATION:
            value = m->config_generation;
            break;
        case O0_VIRTIO_COMMON_QUEUE_SELECT:
            value = m->queue_select;
            break;
        case O0_VIRTIO_COMMON_QUEUE_SIZE:
            value = q != NULL ? q->size : 0u;
            break;
        case O0_VIRTIO_COMMON_QUEUE_MSIX_VECTOR:
            value = q != NULL ? q->msix_vector : 0xFFFFu;
            break;
        case O0_VIRTIO_COMMON_QUEUE_ENABLE:
            value = q != NULL ? q->enable : 0u;
            break;
        case O0_VIRTIO_COMMON_QUEUE_NOTIFY_OFF:
            if (q == NULL) {
                value = 0u;
            } else if (m->use_forced_notify_off) {
                value = m->forced_notify_off;
            } else {
                value = q->notify_off;
            }
            break;
        case O0_VIRTIO_COMMON_QUEUE_DESC:
            value = q != NULL ? q->desc : 0u;
            break;
        case O0_VIRTIO_COMMON_QUEUE_DRIVER:
            value = q != NULL ? q->driver : 0u;
            break;
        case O0_VIRTIO_COMMON_QUEUE_DEVICE:
            value = q != NULL ? q->device : 0u;
            break;
        default:
            return false;
    }
    *out = value;
    return true;
}

static inline bool vmock_write(volatile void *ctx, size_t length, size_t offset,
                               size_t width, uint64_t value) {
    struct vmock *m = vmock_of(ctx);
    if (!vmock_access_ok(m, length, offset, width)) return false;
    struct vmock_queue *q = vmock_selected(m);
    switch (offset) {
        case O0_VIRTIO_COMMON_DEVICE_FEATURE_SELECT:
            m->device_feature_select = (uint32_t)value;
            return true;
        case O0_VIRTIO_COMMON_DRIVER_FEATURE_SELECT:
            m->driver_feature_select = (uint32_t)value;
            return true;
        case O0_VIRTIO_COMMON_DRIVER_FEATURE:
            /* A device latches only the bits it offered. */
            m->driver_features[m->driver_feature_select & 1u] =
                (uint32_t)value & m->features[m->driver_feature_select & 1u];
            return true;
        case O0_VIRTIO_COMMON_DEVICE_STATUS:
            if (value == 0u) {
                if (!m->stuck_reset) m->status = 0u;
                return true;
            }
            m->status = (uint8_t)value;
            if (m->reject_features_ok && (m->status & O0_VIRTIO_STATUS_FEATURES_OK) != 0u) {
                m->status = (uint8_t)(m->status & ~O0_VIRTIO_STATUS_FEATURES_OK);
            }
            return true;
        case O0_VIRTIO_COMMON_QUEUE_SELECT:
            m->queue_select = (uint16_t)value;
            return true;
        case O0_VIRTIO_COMMON_QUEUE_MSIX_VECTOR:
            if (q == NULL) return false;
            q->msix_vector = (uint16_t)value;
            return true;
        case O0_VIRTIO_COMMON_QUEUE_ENABLE:
            if (q == NULL) return false;
            q->enable = m->refuse_enable ? 0u : 1u;
            return true;
        case O0_VIRTIO_COMMON_QUEUE_DESC:
            if (q == NULL) return false;
            q->desc = value;
            return true;
        case O0_VIRTIO_COMMON_QUEUE_DRIVER:
            if (q == NULL) return false;
            q->driver = value;
            return true;
        case O0_VIRTIO_COMMON_QUEUE_DEVICE:
            if (q == NULL) return false;
            q->device = value;
            return true;
        default:
            return false;
    }
}

static inline bool vmock_r8(volatile void *c, size_t l, size_t o, uint8_t *v) {
    uint64_t value = 0;
    if (v == NULL || !vmock_read(c, l, o, 1u, &value)) return false;
    *v = (uint8_t)value;
    return true;
}
static inline bool vmock_r16(volatile void *c, size_t l, size_t o, uint16_t *v) {
    uint64_t value = 0;
    if (v == NULL || !vmock_read(c, l, o, 2u, &value)) return false;
    *v = (uint16_t)value;
    return true;
}
static inline bool vmock_r32(volatile void *c, size_t l, size_t o, uint32_t *v) {
    uint64_t value = 0;
    if (v == NULL || !vmock_read(c, l, o, 4u, &value)) return false;
    *v = (uint32_t)value;
    return true;
}
static inline bool vmock_r64(volatile void *c, size_t l, size_t o, uint64_t *v) {
    uint64_t value = 0;
    if (v == NULL || !vmock_read(c, l, o, 8u, &value)) return false;
    *v = value;
    return true;
}
static inline bool vmock_w8(volatile void *c, size_t l, size_t o, uint8_t v) {
    return vmock_write(c, l, o, 1u, v);
}
static inline bool vmock_w16(volatile void *c, size_t l, size_t o, uint16_t v) {
    return vmock_write(c, l, o, 2u, v);
}
static inline bool vmock_w32(volatile void *c, size_t l, size_t o, uint32_t v) {
    return vmock_write(c, l, o, 4u, v);
}
static inline bool vmock_w64(volatile void *c, size_t l, size_t o, uint64_t v) {
    return vmock_write(c, l, o, 8u, v);
}
static inline void vmock_barrier(void) {}

static const struct o0_virtio_reg_iface vmock_iface = {
    .read8 = vmock_r8,
    .read16 = vmock_r16,
    .read32 = vmock_r32,
    .read64 = vmock_r64,
    .write8 = vmock_w8,
    .write16 = vmock_w16,
    .write32 = vmock_w32,
    .write64 = vmock_w64,
    .write_barrier = vmock_barrier,
    .read_barrier = vmock_barrier
};

/* Notification region: a byte window that records 16-bit writes. */
struct vmock_notify {
    uint8_t bytes[64];
    size_t length;
    size_t writes;
};

static inline struct vmock_notify *vmock_notify_of(volatile void *ctx) {
    return (struct vmock_notify *)(uintptr_t)(void *)ctx;
}

static inline bool vmock_notify_ok(struct vmock_notify *n, size_t length,
                                   size_t offset, size_t width) {
    if (n == NULL || width == 0u || offset % width != 0u) return false;
    if (offset > length || width > length - offset) return false;
    if (offset > n->length || width > n->length - offset) return false;
    return true;
}

static inline bool vmock_notify_read(volatile void *ctx, size_t length, size_t offset,
                                     size_t width, uint64_t *out) {
    struct vmock_notify *n = vmock_notify_of(ctx);
    if (out == NULL || !vmock_notify_ok(n, length, offset, width)) return false;
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i) {
        value |= (uint64_t)n->bytes[offset + i] << (8u * i);
    }
    *out = value;
    return true;
}

static inline bool vmock_notify_write(volatile void *ctx, size_t length, size_t offset,
                                      size_t width, uint64_t value) {
    struct vmock_notify *n = vmock_notify_of(ctx);
    if (!vmock_notify_ok(n, length, offset, width)) return false;
    for (size_t i = 0; i < width; ++i) {
        n->bytes[offset + i] = (uint8_t)(value >> (8u * i));
    }
    if (width == 2u) n->writes++;
    return true;
}

static inline bool vmock_notify_r8(volatile void *c, size_t l, size_t o, uint8_t *v) {
    uint64_t value = 0;
    if (v == NULL || !vmock_notify_read(c, l, o, 1u, &value)) return false;
    *v = (uint8_t)value;
    return true;
}
static inline bool vmock_notify_r16(volatile void *c, size_t l, size_t o, uint16_t *v) {
    uint64_t value = 0;
    if (v == NULL || !vmock_notify_read(c, l, o, 2u, &value)) return false;
    *v = (uint16_t)value;
    return true;
}
static inline bool vmock_notify_r32(volatile void *c, size_t l, size_t o, uint32_t *v) {
    uint64_t value = 0;
    if (v == NULL || !vmock_notify_read(c, l, o, 4u, &value)) return false;
    *v = (uint32_t)value;
    return true;
}
static inline bool vmock_notify_r64(volatile void *c, size_t l, size_t o, uint64_t *v) {
    uint64_t value = 0;
    if (v == NULL || !vmock_notify_read(c, l, o, 8u, &value)) return false;
    *v = value;
    return true;
}
static inline bool vmock_notify_w8(volatile void *c, size_t l, size_t o, uint8_t v) {
    return vmock_notify_write(c, l, o, 1u, v);
}
static inline bool vmock_notify_w16(volatile void *c, size_t l, size_t o, uint16_t v) {
    return vmock_notify_write(c, l, o, 2u, v);
}
static inline bool vmock_notify_w32(volatile void *c, size_t l, size_t o, uint32_t v) {
    return vmock_notify_write(c, l, o, 4u, v);
}
static inline bool vmock_notify_w64(volatile void *c, size_t l, size_t o, uint64_t v) {
    return vmock_notify_write(c, l, o, 8u, v);
}

static const struct o0_virtio_reg_iface vmock_notify_iface = {
    .read8 = vmock_notify_r8,
    .read16 = vmock_notify_r16,
    .read32 = vmock_notify_r32,
    .read64 = vmock_notify_r64,
    .write8 = vmock_notify_w8,
    .write16 = vmock_notify_w16,
    .write32 = vmock_notify_w32,
    .write64 = vmock_notify_w64,
    .write_barrier = vmock_barrier,
    .read_barrier = vmock_barrier
};

#endif /* O0_TESTS_VIRTIO_MOCK_H */
