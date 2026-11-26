/* Ozero kernel - virtio-blk driver, legacy virtio PCI transport (Wave 4.2).
 *
 * Transport: BAR0 is an I/O BAR with the legacy register layout. The queue is
 * one physically contiguous page reached by PFN, and the ring layout is the
 * spec layout the device recomputes itself: descriptor table, then avail ring
 * 2-byte aligned, then used ring 4-byte aligned.
 *
 * Ownership: the ring and the request header come from the DMA allocator and
 * are pinned for the device lifetime. The data buffer belongs to the block
 * layer (its bounce buffer), so the driver only translates it and never
 * retains it after completing the bio.
 *
 * Completion: the interrupt handler acknowledges the device ISR and records the
 * interrupt; the submitting call waits on the used ring with a bounded spin and
 * reports O0_BLOCK_ERR_TIMEOUT instead of blocking forever. One request is in
 * flight per device, which is why a single request header is enough.
 *
 * Evidence limits: this is QEMU evidence only. No physical controller is
 * claimed, and the modern (MMIO capability) transport is not implemented.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

#define VBLK_MAX_DEVICES 4u
#define VBLK_TIMEOUT_SPINS 20000000u

/* Legacy virtio PCI register offsets (I/O BAR0). */
#define VBLK_REG_DEVICE_FEATURES 0x00u
#define VBLK_REG_DRIVER_FEATURES 0x04u
#define VBLK_REG_QUEUE_PFN 0x08u
#define VBLK_REG_QUEUE_SIZE 0x0Cu
#define VBLK_REG_QUEUE_SELECT 0x0Eu
#define VBLK_REG_QUEUE_NOTIFY 0x10u
#define VBLK_REG_STATUS 0x12u
#define VBLK_REG_ISR 0x13u
#define VBLK_REG_DEVICE_CFG 0x14u

#define VBLK_STATUS_ACK 0x01u
#define VBLK_STATUS_DRIVER 0x02u
#define VBLK_STATUS_DRIVER_OK 0x04u
#define VBLK_STATUS_FEATURES_OK 0x08u
#define VBLK_STATUS_FAILED 0x80u

#define VBLK_F_FLUSH (1u << 9)

#define VBLK_T_IN 0u
#define VBLK_T_OUT 1u
#define VBLK_T_FLUSH 4u

#define VBLK_S_OK 0u
#define VBLK_S_IOERR 1u
#define VBLK_S_UNSUPP 2u

#define VRING_DESC_F_NEXT 1u
#define VRING_DESC_F_WRITE 2u

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

struct o0_virtio_blk {
    struct o0_block_dev dev;
    struct o0_block_queue queue;
    struct o0_pci_device pci;
    uint16_t io_base;
    uint64_t capacity;
    bool in_use;
    bool has_flush;

    struct o0_dma_buffer ring;
    struct o0_dma_buffer req;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
    struct vblk_req_hdr *hdr;
    volatile uint8_t *status_byte;
    uint16_t last_used;
    uint16_t queue_size;
    uint16_t pci_command;
    uint32_t device_features;
    volatile uint8_t last_isr;

    o0_vector_t vector;
    bool vector_ok;
    volatile uint64_t irq_count;
};

static struct o0_virtio_blk vblk_devices[VBLK_MAX_DEVICES];

/* ---- port I/O ---- */

static inline void vblk_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void vblk_outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void vblk_outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t vblk_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t vblk_inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t vblk_inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Cleanup paths unwind best-effort; the compiler requires the status to be
 * consumed explicitly rather than silently discarded. */
static void vblk_ignore(o0_status status) {
    (void)status;
}

static struct o0_virtio_blk *vblk_from_dev(const struct o0_block_dev *dev) {
    for (size_t i = 0; i < VBLK_MAX_DEVICES; ++i) {
        if (&vblk_devices[i].dev == dev && vblk_devices[i].in_use) {
            return &vblk_devices[i];
        }
    }
    return NULL;
}

static bool vblk_irq(o0_gsi_t gsi, void *ctx) {
    (void)gsi;
    struct o0_virtio_blk *vd = (struct o0_virtio_blk *)ctx;
    if (vd == NULL) {
        return false;
    }
    /* Reading the ISR acknowledges it; the value itself carries no queue
     * information in legacy virtio. */
    vd->last_isr = vblk_inb((uint16_t)(vd->io_base + VBLK_REG_ISR));
    vd->irq_count = vd->irq_count + 1u;
    return true;
}

static o0_block_status vblk_map_status(uint8_t status) {
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

static void vblk_complete(struct o0_bio *bio, o0_block_status status,
                          size_t bytes) {
    o0_block_complete_bio(bio, status, bytes);
}

/* Waits for the used ring to advance past `last_used`. Returns the id the
 * device reported, or false on timeout. */
static bool vblk_wait_used(struct o0_virtio_blk *vd) {
    for (uint32_t spin = 0; spin < VBLK_TIMEOUT_SPINS; ++spin) {
        uint16_t idx = vd->used->idx;
        if (idx != vd->last_used) {
            /* Device completion stores must be visible before consuming status. */
            __asm__ volatile ("lfence" : : : "memory");
            return true;
        }
        __asm__ volatile ("" : : : "memory");
    }
    return false;
}

static void vblk_submit_chain(struct o0_virtio_blk *vd) {
    uint16_t slot = (uint16_t)(vd->avail->idx % vd->queue_size);
    vd->avail->ring[slot] = 0;
    /* Publish descriptor/avail entry before making the new index visible. */
    __asm__ volatile ("mfence" : : : "memory");
    vd->avail->idx = (uint16_t)(vd->avail->idx + 1u);
    __asm__ volatile ("mfence" : : : "memory");
    vblk_outw((uint16_t)(vd->io_base + VBLK_REG_QUEUE_NOTIFY), 0);
}

/* One synchronous request: header, optional data buffer, status byte. */
static o0_block_status vblk_run(struct o0_virtio_blk *vd, uint32_t type,
                                uint64_t sector, uint64_t data_phys,
                                uint32_t data_len, bool data_to_device) {
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
                                       (data_to_device ? 0u : VRING_DESC_F_WRITE));
        vd->desc[1].next = 2;
        status_desc = 2;
    }

    /* The status byte sits at a fixed offset in the request page, after the
     * 16-byte header, so the descriptor address is the request buffer plus 32. */
    vd->desc[status_desc].addr = vd->req.phys + 32u;
    vd->desc[status_desc].len = 1;
    vd->desc[status_desc].flags = VRING_DESC_F_WRITE;
    vd->desc[status_desc].next = 0;

    vblk_submit_chain(vd);
    if (!vblk_wait_used(vd)) {
        o0_serial_puts("virtio-blk timeout dump: qsize=");
        o0_serial_dec(vd->queue_size);
        o0_serial_puts(" pfn="); o0_serial_hex(vd->ring.phys >> 12);
        o0_serial_puts(" status="); o0_serial_hex(vblk_inb(vd->io_base + VBLK_REG_STATUS));
        o0_serial_puts(" features="); o0_serial_hex(vd->device_features);
        o0_serial_puts(" capacity="); o0_serial_dec(vd->capacity);
        o0_serial_puts(" isr="); o0_serial_hex(vd->last_isr);
        o0_serial_puts(" avail="); o0_serial_dec(vd->avail->idx);
        o0_serial_puts(" used="); o0_serial_dec(vd->used->idx);
        o0_serial_puts(" pci_cmd="); o0_serial_hex(vd->pci_command);
        o0_serial_puts(" hdr_type="); o0_serial_hex(vd->hdr->type);
        o0_serial_puts(" hdr_sector="); o0_serial_hex(vd->hdr->sector);
        o0_serial_puts(" hdr_reserved="); o0_serial_hex(vd->hdr->reserved);
        o0_serial_puts(" req_status="); o0_serial_hex(*vd->status_byte);
        o0_serial_puts("\n");
        return O0_BLOCK_ERR_TIMEOUT;
    }
    vd->last_used = (uint16_t)(vd->last_used + 1u);
    uint8_t status = *vd->status_byte;
    return vblk_map_status(status);
}

static o0_status vblk_queue_rq(struct o0_block_queue *queue, struct o0_bio *bio) {
    if (queue == NULL || bio == NULL) {
        return O0_EINVAL;
    }
    struct o0_virtio_blk *vd = (struct o0_virtio_blk *)queue->driver_data;
    if (vd == NULL || !vd->in_use) {
        return O0_ENOENT;
    }

    if (bio->op == O0_BLOCK_FLUSH) {
        o0_block_status st = vblk_run(vd, VBLK_T_FLUSH, 0, 0, 0, false);
        vblk_complete(bio, st, 0);
        return O0_OK;
    }
    if (bio->op != O0_BLOCK_READ && bio->op != O0_BLOCK_WRITE) {
        return O0_ENOTSUP;
    }
    if (bio->nr_vecs != 1u || bio->vecs == NULL || bio->vecs[0].page == NULL) {
        return O0_EINVAL;
    }

    const struct o0_bio_vec *vec = &bio->vecs[0];
    uint8_t *buffer = (uint8_t *)vec->page + vec->offset;
    uint64_t phys = o0_virt_to_phys(buffer);
    if (phys == 0u) {
        return O0_EINVAL;
    }

    o0_block_status st = vblk_run(vd, bio->op == O0_BLOCK_WRITE ? VBLK_T_OUT
                                                                : VBLK_T_IN,
                                  bio->lba, phys, (uint32_t)vec->len,
                                  bio->op == O0_BLOCK_WRITE);
    vblk_complete(bio, st, st == O0_BLOCK_OK ? vec->len : 0u);
    return O0_OK;
}

static o0_status vblk_reset(struct o0_block_dev *dev) {
    return vblk_from_dev(dev) != NULL ? O0_OK : O0_ENOENT;
}

static o0_status vblk_flush(struct o0_block_dev *dev) {
    return dev != NULL ? O0_OK : O0_EINVAL;
}

static o0_status vblk_geometry(struct o0_block_dev *dev,
                               struct o0_block_geometry *out) {
    if (dev == NULL || out == NULL) {
        return O0_EINVAL;
    }
    *out = dev->geo;
    return O0_OK;
}

static bool vblk_media_changed(struct o0_block_dev *dev) {
    (void)dev;
    return false;
}

static o0_status vblk_open(struct o0_block_dev *dev, bool exclusive) {
    (void)exclusive;
    return dev != NULL ? O0_OK : O0_EINVAL;
}

static void vblk_close(struct o0_block_dev *dev) {
    (void)dev;
}

static const struct o0_block_ops vblk_block_ops = {
    .reset = vblk_reset,
    .flush = vblk_flush,
    .discard = NULL,
    .write_zeroes = NULL,
    .open = vblk_open,
    .close = vblk_close,
    .media_changed = vblk_media_changed,
    .get_geometry = vblk_geometry,
    .set_features = NULL
};

static const struct o0_block_queue_ops vblk_queue_ops = {
    .queue_rq = vblk_queue_rq,
    .poll = NULL,
    .timeout = NULL
};

static void vblk_set_name(char *dst, const char *src) {
    size_t i = 0;
    for (; i + 1u < O0_BLOCK_NAME_MAX && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

o0_status o0_virtio_blk_probe(const char *name, struct o0_block_dev **out) {
    if (name == NULL || out == NULL || name[0] == '\0') {
        return O0_EINVAL;
    }
    *out = NULL;
    o0_status st;

    /* Try the modern (MMIO) transport first. */
    st = o0_virtio_blk_modern_probe(name, out);
    if (st == O0_OK && *out != NULL) {
        return O0_OK;
    }
    if (st != O0_ENOENT && st != O0_ENOTSUP) {
        return st;
    }

    /* Fall back to the legacy (I/O port) transport. */
    const struct o0_pci_device *found = NULL;
    for (size_t i = 0; i < o0_pci_device_count(); ++i) {
        const struct o0_pci_device *pci = o0_pci_device(i);
        if (pci == NULL) {
            continue;
        }
        if (pci->vendor_id == 0x1AF4u && pci->device_id == 0x1001u) {
            found = pci;
            break;
        }
    }
    if (found == NULL) {
        return O0_ENOENT;
    }

    struct o0_virtio_blk *vd = NULL;
    for (size_t i = 0; i < VBLK_MAX_DEVICES; ++i) {
        if (!vblk_devices[i].in_use) {
            vd = &vblk_devices[i];
            break;
        }
    }
    if (vd == NULL) {
        return O0_EBUSY;
    }
    memset(vd, 0, sizeof(*vd));

    struct o0_pci_bar bar0;
    if (o0_pci_probe_bar(found->bdf, 0u, found->resources.header_type, &bar0) != 0) {
        return O0_EIO;
    }
    vd->pci = *found;
    vd->io_base = (uint16_t)bar0.base;

    /* Enable I/O decoding and bus mastering before any device access. */
    uint16_t cmd = 0;
    if (o0_pci_config_read16(found->bdf, 0x04u, &cmd) != 0) {
        return O0_EIO;
    }
    cmd = (uint16_t)(cmd | 0x0001u | 0x0004u);
    if (o0_pci_config_write16(found->bdf, 0x04u, cmd) != 0 ||
        o0_pci_config_read16(found->bdf, 0x04u, &vd->pci_command) != 0 ||
        (vd->pci_command & 0x0005u) != 0x0005u) {
        return O0_EIO;
    }

    /* Reset, then the legacy feature handshake. */
    vblk_outb((uint16_t)(vd->io_base + VBLK_REG_STATUS), 0);
    vblk_outb((uint16_t)(vd->io_base + VBLK_REG_STATUS), VBLK_STATUS_ACK);
    vblk_outb((uint16_t)(vd->io_base + VBLK_REG_STATUS),
              VBLK_STATUS_ACK | VBLK_STATUS_DRIVER);

    uint32_t features = vblk_inl((uint16_t)(vd->io_base + VBLK_REG_DEVICE_FEATURES));
    vd->device_features = features;
    uint32_t driver_features = 0;
    if ((features & VBLK_F_FLUSH) != 0u) {
        driver_features |= VBLK_F_FLUSH;
        vd->has_flush = true;
    }
    vblk_outl((uint16_t)(vd->io_base + VBLK_REG_DRIVER_FEATURES), driver_features);
    vblk_outw((uint16_t)(vd->io_base + VBLK_REG_QUEUE_SELECT), 0);
    vd->queue_size = vblk_inw((uint16_t)(vd->io_base + VBLK_REG_QUEUE_SIZE));
    size_t used_offset = 0, ring_bytes = 0;
    if (vd->queue_size == 0u) {
        o0_serial_puts("virtio-blk: queue 0 unavailable (QUEUE_SIZE=0)\\n");
        vblk_outb((uint16_t)(vd->io_base + VBLK_REG_STATUS), VBLK_STATUS_FAILED);
        return O0_ENOTSUP;
    }
    if ((vd->queue_size & (uint16_t)(vd->queue_size - 1u)) != 0u) {
        o0_serial_puts("virtio-blk: invalid non-power-of-two QUEUE_SIZE\\n");
        vblk_outb((uint16_t)(vd->io_base + VBLK_REG_STATUS), VBLK_STATUS_FAILED);
        return O0_ENOTSUP;
    }
    if (vd->queue_size < 3u ||
        !o0_virtio_legacy_ring_layout(vd->queue_size, &used_offset, &ring_bytes)) {
        o0_serial_puts("virtio-blk: queue too small or ring layout overflow\\n");
        vblk_outb((uint16_t)(vd->io_base + VBLK_REG_STATUS), VBLK_STATUS_FAILED);
        return O0_ENOTSUP;
    }
    o0_serial_puts("virtio-blk queue size="); o0_serial_dec(vd->queue_size);
    o0_serial_puts("\n");

    struct o0_dma_constraint constraint;
    memset(&constraint, 0, sizeof(constraint));
    constraint.addr_mask = (UINT64_C(1) << 44) - 1u;
    constraint.alignment = O0_PAGE_SIZE;
    constraint.max_segment = 0;
    constraint.coherent = true;

    size_t ring_alloc_bytes = (ring_bytes + O0_PAGE_SIZE - 1u) &
                              ~(size_t)(O0_PAGE_SIZE - 1u);
    if (o0_dma_alloc(&constraint, ring_alloc_bytes, &vd->ring) != O0_OK) {
        return O0_ENOMEM;
    }
    if (o0_dma_alloc(&constraint, O0_PAGE_SIZE, &vd->req) != O0_OK) {
        vblk_ignore(o0_dma_free(&vd->ring));
        return O0_ENOMEM;
    }
    memset(vd->ring.virt, 0, vd->ring.size);
    memset(vd->req.virt, 0, vd->req.size);

    uint8_t *base = (uint8_t *)vd->ring.virt;
    vd->desc = (struct vring_desc *)base;
    vd->avail = (struct vring_avail *)(base + sizeof(struct vring_desc) * vd->queue_size);
    vd->used = (struct vring_used *)(base + used_offset);
    vd->hdr = (struct vblk_req_hdr *)vd->req.virt;
    vd->status_byte = (volatile uint8_t *)vd->req.virt + 32u;
    vd->last_used = 0;

    if (vd->ring.size < ring_bytes) {
        vblk_ignore(o0_dma_free(&vd->req));
        vblk_ignore(o0_dma_free(&vd->ring));
        return O0_ENOMEM;
    }

    if (o0_dma_pin(&vd->ring) != O0_OK || o0_dma_pin(&vd->req) != O0_OK) {
        vblk_ignore(o0_dma_free(&vd->req));
        vblk_ignore(o0_dma_free(&vd->ring));
        return O0_EBUSY;
    }

    vblk_outl((uint16_t)(vd->io_base + VBLK_REG_QUEUE_PFN),
              (uint32_t)(vd->ring.phys >> O0_PAGE_SHIFT));

    if (found->resources.msi_supported) {
        o0_vector_t vector = O0_VECTOR_NONE;
        if (o0_irq_alloc_vectors(1, &vector) == O0_OK &&
            o0_irq_register_vector(vector, vblk_irq, vd) == O0_OK &&
            o0_pci_msi_enable(found->bdf, found->resources.msi_cap_offset,
                              o0_lapic_id(), vector) == 0) {
            vd->vector = vector;
            vd->vector_ok = true;
        }
    }

    vd->capacity = (uint64_t)vblk_inl((uint16_t)(vd->io_base + VBLK_REG_DEVICE_CFG)) |
                   ((uint64_t)vblk_inl((uint16_t)(vd->io_base + VBLK_REG_DEVICE_CFG + 4u))
                    << 32);
    if (vd->capacity == 0u) {
        /* Without a capacity the block layer would reject every request, so
         * report the device as unusable instead of registering a dead disk. */
        vd->in_use = false;
        if (vd->vector_ok) {
            (void)o0_irq_unregister_vector(vd->vector, vblk_irq, vd);
            o0_irq_free_vectors(vd->vector, 1);
            vd->vector_ok = false;
        }
        vblk_ignore(o0_dma_unpin(&vd->req));
        vblk_ignore(o0_dma_unpin(&vd->ring));
        vblk_ignore(o0_dma_free(&vd->req));
        vblk_ignore(o0_dma_free(&vd->ring));
        return O0_ENOTSUP;
    }

    vblk_outb((uint16_t)(vd->io_base + VBLK_REG_STATUS),
              VBLK_STATUS_ACK | VBLK_STATUS_DRIVER | VBLK_STATUS_DRIVER_OK);
    o0_serial_puts("virtio-blk ready pfn="); o0_serial_hex(vd->ring.phys >> 12);
    o0_serial_puts(" status="); o0_serial_hex(vblk_inb(vd->io_base + VBLK_REG_STATUS));
    o0_serial_puts(" features="); o0_serial_hex(vd->device_features);
    o0_serial_puts(" capacity="); o0_serial_dec(vd->capacity);
    o0_serial_puts(" pci_cmd="); o0_serial_hex(vd->pci_command);
    o0_serial_puts(" isr="); o0_serial_hex(vd->last_isr);
    o0_serial_puts(" avail="); o0_serial_dec(vd->avail->idx);
    o0_serial_puts(" used="); o0_serial_dec(vd->used->idx);
    o0_serial_puts("\n");

    vd->in_use = true;
    vblk_set_name(vd->dev.name, name);
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
    vd->dev.ops = &vblk_block_ops;
    vd->dev.driver_data = vd;
    vd->queue.id = 0;
    vd->queue.depth = 1;
    vd->queue.ops = &vblk_queue_ops;
    vd->queue.driver_data = vd;

    st = o0_block_register(&vd->dev);
    if (st != O0_OK) {
        vd->in_use = false;
        vblk_ignore(o0_dma_unpin(&vd->req));
        vblk_ignore(o0_dma_unpin(&vd->ring));
        vblk_ignore(o0_dma_free(&vd->req));
        vblk_ignore(o0_dma_free(&vd->ring));
        return st;
    }

    *out = &vd->dev;
    return O0_OK;
}

uint64_t o0_virtio_blk_irq_count(struct o0_block_dev *dev) {
    struct o0_virtio_blk *vd = vblk_from_dev(dev);
    if (vd != NULL) {
        return vd->irq_count;
    }
    return o0_virtio_blk_modern_irq_count(dev);
}

o0_status o0_virtio_blk_inject_error(struct o0_block_dev *dev) {
    struct o0_virtio_blk *vd = vblk_from_dev(dev);
    if (vd != NULL) {
        /* A read starting at the last sector but two sectors long runs off the end
         * of the medium. The device reports the failure; the driver must decode it
         * rather than hang or report success. */
        uint64_t sector = vd->capacity > 1u ? vd->capacity - 1u : 0u;
        uint64_t phys = vd->req.phys + 512u;
        if (vd->req.size < 512u + 1024u) {
            return O0_ENOMEM;
        }
        o0_block_status st = vblk_run(vd, VBLK_T_IN, sector, phys, 1024u, false);
        if (st == O0_BLOCK_OK) {
            return O0_EIO; /* the device accepted an out-of-range read */
        }
        return O0_OK;
    }
    return o0_virtio_blk_modern_inject_error(dev);
}
