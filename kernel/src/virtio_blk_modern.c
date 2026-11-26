/*
 * Ozero kernel - virtio-blk driver, modern virtio PCI transport (Wave 4.2).
 *
 * Transport: a single memory BAR contains the device regions, located through
 * PCI vendor-specific capabilities (cap ID 0x09). Queue setup and doorbell
 * notification use MMIO registers instead of legacy I/O ports. MSI-X is used
 * for interrupts when available; the submitting call also polls the used ring,
 * so a missing or misrouted interrupt surfaces as a timeout instead of a hang.
 *
 * Ownership: the ring and the request header come from the DMA allocator and
 * are pinned for the device lifetime. The data buffer belongs to the block
 * layer (its bounce buffer), so the driver only translates it and never
 * retains it after completing the bio. One request is in flight per device.
 *
 * Evidence limits: QEMU evidence only. No physical controller is claimed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ozero/block.h"
#include "ozero/irq.h"
#include "ozero/lapic.h"
#include "ozero/mm.h"
#include "ozero/pci.h"
#include "ozero/status.h"
#include "ozero/types.h"
#include "ozero/virtio_blk.h"
#include "ozero/virtio_ring.h"

#include "serial.h"
#include "string.h"

#define VBLK_MODERN_MAX_DEVICES 4u
#define VBLK_MODERN_TIMEOUT_SPINS 20000000u

/* PCI capability IDs. */
#define PCI_CAP_ID_VNDR 0x09u

/* Virtio PCI capability config types. */
#define VIRTIO_PCI_CAP_COMMON_CFG 1u
#define VIRTIO_PCI_CAP_ISR_CFG 2u
#define VIRTIO_PCI_CAP_DEVICE_CFG 3u
#define VIRTIO_PCI_CAP_PCI_CFG 4u
#define VIRTIO_PCI_CAP_NOTIFY_CFG 5u

/* Device status flags. */
#define VIRTIO_STATUS_ACK 0x01u
#define VIRTIO_STATUS_DRIVER 0x02u
#define VIRTIO_STATUS_DRIVER_OK 0x04u
#define VIRTIO_STATUS_FEATURES_OK 0x08u
#define VIRTIO_STATUS_FAILED 0x80u

/* Virtqueue descriptor flags. */
#define VRING_DESC_F_NEXT 1u
#define VRING_DESC_F_WRITE 2u

/* Virtio-blk command types. */
#define VBLK_T_IN 0u
#define VBLK_T_OUT 1u
#define VBLK_T_FLUSH 4u

/* Virtio-blk device status. */
#define VBLK_S_OK 0u
#define VBLK_S_IOERR 1u
#define VBLK_S_UNSUPP 2u

/* Modern virtio PCI capability structure (cap ID 0x09). */
struct O0_PACKED virtio_pci_cap {
    uint8_t cap_vendor;
    uint8_t cap_next;
    uint8_t cap_len;
    uint8_t cfg_type;
    uint8_t bar;
    uint8_t padding[3];
    uint32_t offset;
    uint32_t length;
};

/* Register offsets within the common config space. */
#define VIRTIO_COMMON_DEVICE_FEATURE_SELECT 0x00u
#define VIRTIO_COMMON_DEVICE_FEATURE 0x04u
#define VIRTIO_COMMON_DRIVER_FEATURE_SELECT 0x08u
#define VIRTIO_COMMON_DRIVER_FEATURE 0x0Cu
#define VIRTIO_COMMON_QUEUE_SELECT 0x10u
#define VIRTIO_COMMON_QUEUE_MAX_SIZE 0x14u
#define VIRTIO_COMMON_QUEUE_MSIX_VECTOR 0x18u
#define VIRTIO_COMMON_QUEUE_ENABLE 0x1Cu
#define VIRTIO_COMMON_QUEUE_NOTIFY_OFFSET 0x20u
#define VIRTIO_COMMON_QUEUE_DESC_LO 0x24u
#define VIRTIO_COMMON_QUEUE_DESC_HI 0x28u
#define VIRTIO_COMMON_QUEUE_AVAIL_LO 0x2Cu
#define VIRTIO_COMMON_QUEUE_AVAIL_HI 0x30u
#define VIRTIO_COMMON_QUEUE_USED_LO 0x34u
#define VIRTIO_COMMON_QUEUE_USED_HI 0x38u
#define VIRTIO_COMMON_CONFIG_GENERATION 0x3Cu
#define VIRTIO_COMMON_DEVICE_STATUS 0x7Cu

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
};

/* Header the device reads before the data buffer. */
struct vblk_req_hdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct o0_virtio_blk_modern {
    struct o0_block_dev dev;
    struct o0_block_queue queue;
    struct o0_pci_device pci;
    volatile uint8_t *bar;
    uint64_t bar_size;
    uint64_t capacity;
    bool in_use;
    bool has_flush;

    /* Offsets within the BAR for each config region. */
    uint32_t common_off;
    uint32_t isr_off;
    uint32_t dev_off;
    uint32_t notify_off;

    struct o0_dma_buffer ring;
    struct o0_dma_buffer req;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
    struct vblk_req_hdr *hdr;
    volatile uint8_t *status_byte;
    uint16_t last_used;
    uint16_t queue_size;
    uint32_t device_features;
    volatile uint32_t isr_status;

    o0_vector_t vector;
    bool vector_ok;
    volatile uint64_t irq_count;

    /* MSI-X table mapping, for cleanup. */
    uint64_t msix_window;
    size_t msix_size;
    bool msix_mapped;
};

static struct o0_virtio_blk_modern vblk_modern_devices[VBLK_MODERN_MAX_DEVICES];

static struct o0_virtio_blk_modern *vblk_modern_from_dev(
    const struct o0_block_dev *dev) {
    for (size_t i = 0; i < VBLK_MODERN_MAX_DEVICES; ++i) {
        if (&vblk_modern_devices[i].dev == dev &&
            vblk_modern_devices[i].in_use) {
            return &vblk_modern_devices[i];
        }
    }
    return NULL;
}

static bool vblk_modern_irq(o0_gsi_t gsi, void *ctx) {
    (void)gsi;
    struct o0_virtio_blk_modern *vd = (struct o0_virtio_blk_modern *)ctx;
    if (vd == NULL) {
        return false;
    }
    /* Reading the ISR acknowledges it. The ISR region is within the BAR. */
    vd->isr_status =
        *(volatile uint32_t *)(vd->bar + vd->isr_off);
    vd->irq_count = vd->irq_count + 1u;
    return true;
}

static o0_block_status vblk_modern_map_status(uint8_t status) {
    switch (status) {
        case VBLK_S_OK:
            return O0_BLOCK_OK;
        case VBLK_S_UNSUPP:
            return O0_BLOCK_ERR_NOTSUP;
        case VBLK_S_IOERR:
        default:
            return O0_BLOCK_ERR_IO;
    }
}

static void vblk_modern_complete(struct o0_bio *bio, o0_block_status status,
                                 size_t bytes) {
    o0_block_complete_bio(bio, status, bytes);
}

/* Waits for the used ring to advance past `last_used`. Returns true on
 * completion, false on timeout. */
static bool vblk_modern_wait_used(struct o0_virtio_blk_modern *vd) {
    for (uint32_t spin = 0; spin < VBLK_MODERN_TIMEOUT_SPINS; ++spin) {
        uint16_t idx = vd->used->idx;
        if (idx != vd->last_used) {
            __asm__ volatile ("lfence" : : : "memory");
            return true;
        }
        __asm__ volatile ("" : : : "memory");
    }
    return false;
}

static void vblk_modern_submit_chain(struct o0_virtio_blk_modern *vd) {
    uint16_t slot = (uint16_t)(vd->avail->idx % vd->queue_size);
    vd->avail->ring[slot] = 0;
    __asm__ volatile ("mfence" : : : "memory");
    vd->avail->idx = (uint16_t)(vd->avail->idx + 1u);
    __asm__ volatile ("mfence" : : : "memory");
    /* Doorbell: write the queue index to the notify register. */
    *(volatile uint16_t *)(vd->bar + vd->notify_off) = (uint16_t)0;
}

static o0_block_status vblk_modern_run(struct o0_virtio_blk_modern *vd,
                                       uint32_t type, uint64_t sector,
                                       uint64_t data_phys, uint32_t data_len,
                                       bool data_to_device) {
    vd->hdr->type = type;
    vd->hdr->reserved = 0;
    vd->hdr->sector = sector;
    *vd->status_byte = 0xFFu;
    __asm__ volatile ("mfence" : : : "memory");

    vd->desc[0].addr = vd->req.phys;
    vd->desc[0].len = (uint32_t)sizeof(struct vblk_req_hdr);
    vd->desc[0].flags = VRING_DESC_F_NEXT;
    vd->desc[0].next = 1;

    uint16_t status_desc = 1;
    if (data_len != 0u) {
        vd->desc[1].addr = data_phys;
        vd->desc[1].len = data_len;
        vd->desc[1].flags = (uint16_t)(VRING_DESC_F_NEXT |
                                       (data_to_device ? 0u
                                                       : VRING_DESC_F_WRITE));
        vd->desc[1].next = 2;
        status_desc = 2;
    }

    vd->desc[status_desc].addr = vd->req.phys + 32u;
    vd->desc[status_desc].len = 1;
    vd->desc[status_desc].flags = VRING_DESC_F_WRITE;
    vd->desc[status_desc].next = 0;

    vblk_modern_submit_chain(vd);
    if (!vblk_modern_wait_used(vd)) {
        o0_serial_puts("virtio-blk-modern timeout dump: qsize=");
        o0_serial_dec(vd->queue_size);
        o0_serial_puts(" status=");
        o0_serial_hex(*(volatile uint32_t *)(vd->bar +
                                             vd->common_off +
                                             VIRTIO_COMMON_DEVICE_STATUS));
        o0_serial_puts(" features=");
        o0_serial_hex(vd->device_features);
        o0_serial_puts(" capacity=");
        o0_serial_dec(vd->capacity);
        o0_serial_puts(" isr=");
        o0_serial_hex(vd->isr_status);
        o0_serial_puts(" avail=");
        o0_serial_dec(vd->avail->idx);
        o0_serial_puts(" used=");
        o0_serial_dec(vd->used->idx);
        o0_serial_puts("\n");
        return O0_BLOCK_ERR_TIMEOUT;
    }
    vd->last_used = (uint16_t)(vd->last_used + 1u);
    uint8_t status = *vd->status_byte;
    return vblk_modern_map_status(status);
}

static o0_status vblk_modern_queue_rq(struct o0_block_queue *queue,
                                      struct o0_bio *bio) {
    if (queue == NULL || bio == NULL) {
        return O0_EINVAL;
    }
    struct o0_virtio_blk_modern *vd =
        (struct o0_virtio_blk_modern *)queue->driver_data;
    if (vd == NULL || !vd->in_use) {
        return O0_ENOENT;
    }

    if (bio->op == O0_BLOCK_FLUSH) {
        o0_block_status st =
            vblk_modern_run(vd, VBLK_T_FLUSH, 0, 0, 0, false);
        vblk_modern_complete(bio, st, 0);
        return O0_OK;
    }
    if (bio->op != O0_BLOCK_READ && bio->op != O0_BLOCK_WRITE) {
        return O0_ENOTSUP;
    }
    if (bio->nr_vecs != 1u || bio->vecs == NULL ||
        bio->vecs[0].page == NULL) {
        return O0_EINVAL;
    }

    const struct o0_bio_vec *vec = &bio->vecs[0];
    uint8_t *buffer = (uint8_t *)vec->page + vec->offset;
    uint64_t phys = o0_virt_to_phys(buffer);
    if (phys == 0u) {
        return O0_EINVAL;
    }

    o0_block_status st = vblk_modern_run(
        vd, bio->op == O0_BLOCK_WRITE ? VBLK_T_OUT : VBLK_T_IN, bio->lba,
        phys, (uint32_t)vec->len, bio->op == O0_BLOCK_WRITE);
    vblk_modern_complete(bio, st, st == O0_BLOCK_OK ? vec->len : 0u);
    return O0_OK;
}

static o0_status vblk_modern_reset(struct o0_block_dev *dev) {
    return vblk_modern_from_dev(dev) != NULL ? O0_OK : O0_ENOENT;
}

static o0_status vblk_modern_flush(struct o0_block_dev *dev) {
    return dev != NULL ? O0_OK : O0_EINVAL;
}

static o0_status vblk_modern_geometry(struct o0_block_dev *dev,
                                      struct o0_block_geometry *out) {
    if (dev == NULL || out == NULL) {
        return O0_EINVAL;
    }
    *out = dev->geo;
    return O0_OK;
}

static bool vblk_modern_media_changed(struct o0_block_dev *dev) {
    (void)dev;
    return false;
}

static o0_status vblk_modern_open(struct o0_block_dev *dev, bool exclusive) {
    (void)exclusive;
    return dev != NULL ? O0_OK : O0_EINVAL;
}

static void vblk_modern_close(struct o0_block_dev *dev) {
    (void)dev;
}

static const struct o0_block_ops vblk_modern_block_ops = {
    .reset = vblk_modern_reset,
    .flush = vblk_modern_flush,
    .discard = NULL,
    .write_zeroes = NULL,
    .open = vblk_modern_open,
    .close = vblk_modern_close,
    .media_changed = vblk_modern_media_changed,
    .get_geometry = vblk_modern_geometry,
    .set_features = NULL
};

static const struct o0_block_queue_ops vblk_modern_queue_ops = {
    .queue_rq = vblk_modern_queue_rq,
    .poll = NULL,
    .timeout = NULL
};

static void vblk_modern_set_name(char *dst, const char *src) {
    size_t i = 0;
    for (; i + 1u < O0_BLOCK_NAME_MAX && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void vblk_modern_ignore(o0_status status) {
    (void)status;
}

/* ---- Modern virtio PCI capability walk ---- */

/* Walks the PCI capability list (vendor-specific cap ID 0x09) from a config
 * space snapshot, looking for virtio capabilities. Fills in the offsets for
 * common, ISR, device and notify config regions. Returns true if all required
 * regions were found. Pure: suitable for host testing. */
static bool vblk_modern_walk_caps(const uint8_t *cfg,
                                  uint8_t *common_bar, uint32_t *common_off,
                                  uint8_t *isr_bar, uint32_t *isr_off,
                                  uint8_t *dev_bar, uint32_t *dev_off,
                                  uint8_t *notify_bar,
                                  uint32_t *notify_off) {
    /* Status bit 4 says the capability list is present. */
    if ((cfg[0x06] & 0x10u) == 0u) {
        return false;
    }

    uint8_t cap_ptr = cfg[0x34];
    bool have_common = false, have_isr = false, have_dev = false,
         have_notify = false;
    for (uint32_t walk = 0; walk < 48u; ++walk) {
        if (cap_ptr < 0x40u) {
            break;
        }
        if ((size_t)cap_ptr + 2u > 256u) {
            return false;
        }
        uint8_t cap_id = cfg[cap_ptr];
        uint8_t next = cfg[cap_ptr + 1u];

        if (cap_id == PCI_CAP_ID_VNDR) {
            /* Virtio PCI capability: cap_len bytes starting at cap_ptr. */
            if ((size_t)cap_ptr + sizeof(struct virtio_pci_cap) > 256u) {
                return false;
            }
            const struct virtio_pci_cap *vc =
                (const struct virtio_pci_cap *)(cfg + cap_ptr);
            if (vc->cfg_type == VIRTIO_PCI_CAP_COMMON_CFG &&
                !have_common) {
                *common_bar = vc->bar;
                *common_off = vc->offset;
                have_common = true;
            } else if (vc->cfg_type == VIRTIO_PCI_CAP_ISR_CFG &&
                       !have_isr) {
                *isr_bar = vc->bar;
                *isr_off = vc->offset;
                have_isr = true;
            } else if (vc->cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG &&
                       !have_dev) {
                *dev_bar = vc->bar;
                *dev_off = vc->offset;
                have_dev = true;
            } else if (vc->cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG &&
                       !have_notify) {
                *notify_bar = vc->bar;
                *notify_off = vc->offset;
                have_notify = true;
            }
        }

        if (next == cap_ptr) {
            return false; /* self-referential list */
        }
        cap_ptr = next;
    }

    return have_common && have_isr && have_dev && have_notify;
}

/* Reads a 32-bit value from MMIO at bar + offset. */
static uint32_t vblk_modern_mmio_read32(const volatile uint8_t *bar,
                                        uint32_t offset) {
    return (uint32_t)bar[offset] |
           ((uint32_t)bar[offset + 1u] << 8) |
           ((uint32_t)bar[offset + 2u] << 16) |
           ((uint32_t)bar[offset + 3u] << 24);
}

/* Writes a 32-bit value to MMIO at bar + offset. */
static void vblk_modern_mmio_write32(volatile uint8_t *bar, uint32_t offset,
                                     uint32_t value) {
    bar[offset] = (uint8_t)(value & 0xFFu);
    bar[offset + 1u] = (uint8_t)((value >> 8) & 0xFFu);
    bar[offset + 2u] = (uint8_t)((value >> 16) & 0xFFu);
    bar[offset + 3u] = (uint8_t)((value >> 24) & 0xFFu);
    __asm__ volatile ("" : : : "memory");
}

o0_status o0_virtio_blk_modern_probe(const char *name,
                                     struct o0_block_dev **out) {
    if (name == NULL || out == NULL || name[0] == '\0') {
        return O0_EINVAL;
    }
    *out = NULL;

    const struct o0_pci_device *found = NULL;
    for (size_t i = 0; i < o0_pci_device_count(); ++i) {
        const struct o0_pci_device *pci = o0_pci_device(i);
        if (pci == NULL) {
            continue;
        }
        if (pci->vendor_id == 0x1AF4u && pci->device_id == 0x1042u) {
            found = pci;
            break;
        }
    }
    if (found == NULL) {
        return O0_ENOENT;
    }

    /* Read PCI config space snapshot, then walk capabilities. */
    uint8_t cfg[256];
    for (uint32_t off = 0; off < sizeof(cfg); off += 4u) {
        uint32_t value = 0;
        if (o0_pci_config_read32(found->bdf, (uint8_t)off, &value) != 0) {
            return O0_EIO;
        }
        cfg[off] = (uint8_t)(value & 0xFFu);
        cfg[off + 1u] = (uint8_t)((value >> 8) & 0xFFu);
        cfg[off + 2u] = (uint8_t)((value >> 16) & 0xFFu);
        cfg[off + 3u] = (uint8_t)((value >> 24) & 0xFFu);
    }
    uint8_t common_bar = 0, isr_bar = 0, dev_bar = 0, notify_bar = 0;
    uint32_t common_off = 0, isr_off = 0, dev_off = 0, notify_off = 0;
    if (!vblk_modern_walk_caps(cfg, &common_bar, &common_off, &isr_bar,
                               &isr_off, &dev_bar, &dev_off, &notify_bar,
                               &notify_off)) {
        o0_serial_puts("virtio-blk-modern: no virtio capabilities found\n");
        return O0_ENOTSUP;
    }

    /* All modern virtio regions should be on the same BAR. */
    if (common_bar != isr_bar || common_bar != dev_bar ||
        common_bar != notify_bar) {
        o0_serial_puts("virtio-blk-modern: capabilities on different BARs\n");
        return O0_ENOTSUP;
    }

    /* Probe the BAR size and base. */
    struct o0_pci_bar bar;
    if (o0_pci_probe_bar(found->bdf, common_bar, found->resources.header_type,
                         &bar) != 0) {
        return O0_EIO;
    }
    if (!bar.valid || !bar.is_memory || bar.base == 0u || bar.size == 0u) {
        return O0_ENOTSUP;
    }

    /* Map the BAR as uncached device memory. */
    uint64_t base = bar.base & ~(uint64_t)(O0_PAGE_SIZE - 1u);
    size_t size = (size_t)((bar.base - base) + bar.size);
    if (size < O0_PAGE_SIZE) {
        size = O0_PAGE_SIZE;
    }
    size = (size + O0_PAGE_SIZE - 1u) & ~(O0_PAGE_SIZE - 1u);
    uint64_t window = 0xffffc00040000000ull; /* MMIO window, below NVMe's */
    o0_status st = o0_as_map(o0_as_kernel(), window, base, size,
                             O0_MAP_READ | O0_MAP_WRITE | O0_MAP_UC |
                                 O0_MAP_NX);
    if (st != O0_OK) {
        return st;
    }
    volatile uint8_t *mapped_bar = (volatile uint8_t *)(uintptr_t)window;

    struct o0_virtio_blk_modern *vd = NULL;
    for (size_t i = 0; i < VBLK_MODERN_MAX_DEVICES; ++i) {
        if (!vblk_modern_devices[i].in_use) {
            vd = &vblk_modern_devices[i];
            break;
        }
    }
    if (vd == NULL) {
        o0_as_unmap(o0_as_kernel(), window, size);
        return O0_EBUSY;
    }
    memset(vd, 0, sizeof(*vd));

    vd->pci = *found;
    vd->bar = mapped_bar;
    vd->bar_size = bar.size;
    vd->common_off = common_off;
    vd->isr_off = isr_off;
    vd->dev_off = dev_off;
    vd->notify_off = notify_off;
    vd->msix_window = 0;
    vd->msix_size = 0;
    vd->msix_mapped = false;

    /* Device reset: write 0 to the device status register. */
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_DEVICE_STATUS,
        0, 0);
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_DEVICE_STATUS,
        0, VIRTIO_STATUS_ACK);
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_DEVICE_STATUS,
        0, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    /* Feature handshake. */
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_DEVICE_FEATURE_SELECT,
        0, 0);
    vd->device_features =
        vblk_modern_mmio_read32((volatile uint8_t *)vd->bar +
                                    vd->common_off +
                                    VIRTIO_COMMON_DEVICE_FEATURE,
                                0);
    uint32_t driver_features = 0;
    if ((vd->device_features & (1u << 9)) != 0u) { /* VIRTIO_BLK_F_FLUSH */
        driver_features |= 1u << 9;
        vd->has_flush = true;
    }
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_DRIVER_FEATURE_SELECT,
        0, 0);
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_DRIVER_FEATURE,
        0, driver_features);
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_DEVICE_STATUS,
        0, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
               VIRTIO_STATUS_FEATURES_OK);

    /* Verify the device accepted our features. */
    uint32_t status = vblk_modern_mmio_read32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_DEVICE_STATUS,
        0);
    if ((status & VIRTIO_STATUS_FAILED) != 0u) {
        o0_serial_puts("virtio-blk-modern: device failed feature handshake\n");
        o0_as_unmap(o0_as_kernel(), window, size);
        return O0_ENOTSUP;
    }

    /* Queue setup: select queue 0. */
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_QUEUE_SELECT,
        0, 0);
    vd->queue_size =
        (uint16_t)vblk_modern_mmio_read32((volatile uint8_t *)vd->bar +
                                              vd->common_off +
                                              VIRTIO_COMMON_QUEUE_MAX_SIZE,
                                          0);
    if (vd->queue_size == 0u ||
        (vd->queue_size & (uint16_t)(vd->queue_size - 1u)) != 0u ||
        vd->queue_size < 3u) {
        o0_serial_puts("virtio-blk-modern: invalid queue size\n");
        o0_as_unmap(o0_as_kernel(), window, size);
        return O0_ENOTSUP;
    }

    /* Read the notify offset from the common config. */
    vd->notify_off =
        vblk_modern_mmio_read32((volatile uint8_t *)vd->bar +
                                    vd->common_off +
                                    VIRTIO_COMMON_QUEUE_NOTIFY_OFFSET,
                                0);

    /* Allocate DMA buffers for the ring and request header. */
    struct o0_dma_constraint constraint;
    memset(&constraint, 0, sizeof(constraint));
    constraint.addr_mask = (UINT64_C(1) << 44) - 1u;
    constraint.alignment = O0_PAGE_SIZE;
    constraint.max_segment = 0;
    constraint.coherent = true;

    /* Use the legacy ring layout arithmetic for the ring size. */
    size_t used_offset = 0, ring_bytes = 0;
    if (!o0_virtio_legacy_ring_layout(vd->queue_size, &used_offset,
                                      &ring_bytes)) {
        o0_serial_puts("virtio-blk-modern: ring layout overflow\n");
        o0_as_unmap(o0_as_kernel(), window, size);
        return O0_ENOTSUP;
    }
    size_t ring_alloc_bytes = (ring_bytes + O0_PAGE_SIZE - 1u) &
                              ~(size_t)(O0_PAGE_SIZE - 1u);
    if (o0_dma_alloc(&constraint, ring_alloc_bytes, &vd->ring) != O0_OK) {
        o0_as_unmap(o0_as_kernel(), window, size);
        return O0_ENOMEM;
    }
    if (o0_dma_alloc(&constraint, O0_PAGE_SIZE, &vd->req) != O0_OK) {
        vblk_modern_ignore(o0_dma_free(&vd->ring));
        o0_as_unmap(o0_as_kernel(), window, size);
        return O0_ENOMEM;
    }
    memset(vd->ring.virt, 0, vd->ring.size);
    memset(vd->req.virt, 0, vd->req.size);

    uint8_t *ring_base = (uint8_t *)vd->ring.virt;
    vd->desc = (struct vring_desc *)ring_base;
    vd->avail = (struct vring_avail *)(base +
                                       sizeof(struct vring_desc) * vd->queue_size);
    vd->used = (struct vring_used *)(base + used_offset);
    vd->hdr = (struct vblk_req_hdr *)vd->req.virt;
    vd->status_byte = (volatile uint8_t *)vd->req.virt + 32u;
    vd->last_used = 0;

    if (vd->ring.size < ring_bytes) {
        vblk_modern_ignore(o0_dma_free(&vd->req));
        vblk_modern_ignore(o0_dma_free(&vd->ring));
        o0_as_unmap(o0_as_kernel(), window, size);
        return O0_ENOMEM;
    }

    if (o0_dma_pin(&vd->ring) != O0_OK || o0_dma_pin(&vd->req) != O0_OK) {
        vblk_modern_ignore(o0_dma_free(&vd->req));
        vblk_modern_ignore(o0_dma_free(&vd->ring));
        o0_as_unmap(o0_as_kernel(), window, size);
        return O0_EBUSY;
    }

    /* Program the queue addresses via MMIO. Physical addresses of the ring
     * sub-structures, computed as ring.phys + offset from ring.virt. */
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_QUEUE_DESC_LO,
        0, (uint32_t)(vd->ring.phys & 0xFFFFFFFFu));
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_QUEUE_DESC_HI,
        0, (uint32_t)(vd->ring.phys >> 32));
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_QUEUE_AVAIL_LO,
        0, (uint32_t)((uintptr_t)vd->avail - (uintptr_t)vd->ring.virt +
                     vd->ring.phys));
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_QUEUE_AVAIL_HI,
        0, 0u);
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_QUEUE_USED_LO,
        0, (uint32_t)((uintptr_t)vd->used - (uintptr_t)vd->ring.virt +
                     vd->ring.phys));
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_QUEUE_USED_HI,
        0, 0u);

    /* Enable the queue. */
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_QUEUE_ENABLE,
        0, 1u);

    /* MSI-X setup if available. */
    if (found->resources.msix_supported &&
        found->resources.msix_table_size >= 1u) {
        uint8_t bir = found->resources.msix_table_bir;
        if (bir < found->resources.bar_count) {
            struct o0_pci_bar msix_bar;
            if (o0_pci_probe_bar(found->bdf, bir,
                                 found->resources.header_type,
                                 &msix_bar) == 0 &&
                msix_bar.valid && msix_bar.is_memory &&
                msix_bar.base != 0u) {
                uint64_t table_phys =
                    msix_bar.base + found->resources.msix_table_offset;
                uint64_t page = table_phys & ~(uint64_t)(O0_PAGE_SIZE - 1u);
                size_t msize =
                    (size_t)((table_phys - page) + 1u * 16u);
                msize = (msize + O0_PAGE_SIZE - 1u) &
                        ~(O0_PAGE_SIZE - 1u);
                uint64_t mwindow =
                    0xffffc00050000000ull; /* separate MMIO window */
                if (o0_as_map(o0_as_kernel(), mwindow, page, msize,
                              O0_MAP_READ | O0_MAP_WRITE | O0_MAP_UC |
                                  O0_MAP_NX) == O0_OK) {
                    volatile void *table =
                        (volatile void *)(uintptr_t)(mwindow +
                                                     (table_phys - page));
                    vd->msix_window = mwindow;
                    vd->msix_size = msize;
                    vd->msix_mapped = true;
                    o0_vector_t vector = O0_VECTOR_NONE;
                    if (o0_irq_alloc_vectors(1u, &vector) == O0_OK &&
                        o0_irq_register_vector(vector, vblk_modern_irq,
                                               vd) == O0_OK &&
                        o0_pci_msix_program(
                            table, found->resources.msix_table_size, 0u,
                            o0_lapic_id(), vector, true) == 0 &&
                        o0_pci_msix_enable(
                            found->bdf, found->resources.msix_cap_offset,
                            table, 0u, o0_lapic_id(), vector) == 0) {
                        vd->vector = vector;
                        vd->vector_ok = true;
                    } else {
                        if (vd->vector != O0_VECTOR_NONE) {
                            (void)o0_irq_unregister_vector(
                                vd->vector, vblk_modern_irq, vd);
                            o0_irq_free_vectors(vd->vector, 1u);
                            vd->vector = O0_VECTOR_NONE;
                        }
                        vd->msix_window = 0;
                        vd->msix_size = 0;
                        vd->msix_mapped = false;
                        o0_as_unmap(o0_as_kernel(), mwindow, msize);
                    }
                }
            }
        }
    }

    /* Read the device capacity from the device config space. */
    uint64_t cap_lo =
        vblk_modern_mmio_read32((volatile uint8_t *)vd->bar + vd->dev_off,
                                0);
    uint64_t cap_hi =
        vblk_modern_mmio_read32((volatile uint8_t *)vd->bar +
                                    vd->dev_off + 4u,
                                0);
    vd->capacity = cap_lo | (cap_hi << 32);
    if (vd->capacity == 0u) {
        o0_serial_puts("virtio-blk-modern: zero capacity\n");
        if (vd->vector_ok) {
            (void)o0_irq_unregister_vector(vd->vector, vblk_modern_irq, vd);
            o0_irq_free_vectors(vd->vector, 1u);
            vd->vector_ok = false;
        }
        if (vd->msix_mapped) {
            o0_as_unmap(o0_as_kernel(), vd->msix_window, vd->msix_size);
        }
        vblk_modern_ignore(o0_dma_unpin(&vd->req));
        vblk_modern_ignore(o0_dma_unpin(&vd->ring));
        vblk_modern_ignore(o0_dma_free(&vd->req));
        vblk_modern_ignore(o0_dma_free(&vd->ring));
        o0_as_unmap(o0_as_kernel(), window, size);
        return O0_ENOTSUP;
    }

    /* Mark the device ready. */
    vblk_modern_mmio_write32(
        (volatile uint8_t *)vd->bar + vd->common_off +
            VIRTIO_COMMON_DEVICE_STATUS,
        0, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
               VIRTIO_STATUS_DRIVER_OK);

    o0_serial_puts("virtio-blk-modern ready bar=");
    o0_serial_hex(bar.base);
    o0_serial_puts(" common_off=");
    o0_serial_hex(common_off);
    o0_serial_puts(" qsize=");
    o0_serial_dec(vd->queue_size);
    o0_serial_puts(" capacity=");
    o0_serial_dec(vd->capacity);
    o0_serial_puts(" irq=");
    o0_serial_dec(vd->vector_ok ? 1 : 0);
    o0_serial_puts("\n");

    vd->in_use = true;
    vblk_modern_set_name(vd->dev.name, name);
    vd->dev.geo.logical_block_size = 512u;
    vd->dev.geo.physical_block_size = 512u;
    vd->dev.geo.nr_sectors = vd->capacity;
    vd->dev.geo.max_hw_sectors = O0_PAGE_SIZE / 512u;
    vd->dev.geo.max_segments = 1;
    vd->dev.geo.max_segment_size = O0_PAGE_SIZE;
    vd->dev.geo.dma_alignment = 512u;
    vd->dev.geo.queue_depth = 1;
    vd->dev.geo.features = O0_BLOCK_F_DMA_COHERENT |
                           (vd->has_flush ? O0_BLOCK_F_FLUSH : 0u);
    vd->dev.nr_queues = 1;
    vd->dev.queues = &vd->queue;
    vd->dev.ops = &vblk_modern_block_ops;
    vd->dev.driver_data = vd;
    vd->queue.id = 0;
    vd->queue.depth = 1;
    vd->queue.ops = &vblk_modern_queue_ops;
    vd->queue.driver_data = vd;

    st = o0_block_register(&vd->dev);
    if (st != O0_OK) {
        vd->in_use = false;
        if (vd->vector_ok) {
            (void)o0_irq_unregister_vector(vd->vector, vblk_modern_irq, vd);
            o0_irq_free_vectors(vd->vector, 1u);
            vd->vector_ok = false;
        }
        if (vd->msix_mapped) {
            o0_as_unmap(o0_as_kernel(), vd->msix_window, vd->msix_size);
        }
        vblk_modern_ignore(o0_dma_unpin(&vd->req));
        vblk_modern_ignore(o0_dma_unpin(&vd->ring));
        vblk_modern_ignore(o0_dma_free(&vd->req));
        vblk_modern_ignore(o0_dma_free(&vd->ring));
        o0_as_unmap(o0_as_kernel(), window, size);
        return st;
    }

    *out = &vd->dev;
    return O0_OK;
}

uint64_t o0_virtio_blk_modern_irq_count(struct o0_block_dev *dev) {
    struct o0_virtio_blk_modern *vd = vblk_modern_from_dev(dev);
    return vd != NULL ? vd->irq_count : 0u;
}

o0_status o0_virtio_blk_modern_inject_error(struct o0_block_dev *dev) {
    struct o0_virtio_blk_modern *vd = vblk_modern_from_dev(dev);
    if (vd == NULL) {
        return O0_ENOENT;
    }
    /* A read starting at the last sector but two sectors long runs off the end
     * of the medium. The device reports the failure; the driver must decode it
     * rather than hang or report success. */
    uint64_t sector = vd->capacity > 1u ? vd->capacity - 1u : 0u;
    uint64_t phys = vd->req.phys + 512u;
    if (vd->req.size < 512u + 1024u) {
        return O0_ENOMEM;
    }
    o0_block_status st = vblk_modern_run(vd, VBLK_T_IN, sector, phys, 1024u,
                                         false);
    if (st == O0_BLOCK_OK) {
        return O0_EIO; /* the device accepted an out-of-range read */
    }
    return O0_OK;
}
