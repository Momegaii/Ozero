/* Ozero kernel - virtio-blk driver, modern virtio PCI transport (Wave 4.2).
 *
 * Transport: the device is described entirely by PCI vendor-specific
 * capabilities. Common, notification, ISR and device configuration live in
 * mapped memory BARs; each queue is programmed with separate 64-bit descriptor,
 * available and used addresses and is only enabled after every field has been
 * accepted by the device. There is no PFN register and no I/O port.
 *
 * Ownership: the descriptor table, available ring and used ring are one
 * physically contiguous, pinned DMA buffer. The request header and the status
 * byte share a second pinned buffer. The data buffer belongs to the block layer
 * (its bounce buffer); the driver translates it and never retains it.
 *
 * Completion: one request is in flight. The driver publishes the chain, rings
 * the doorbell through the notification region and polls the used ring with a
 * bounded budget. A completion is only trusted when its id matches the chain
 * that was submitted.
 *
 * Quiesce: on a timeout or a malformed completion the device is reset and the
 * queue re-programmed before any ring or request memory is touched again. There
 * is no path that frees DMA memory while the device may still write to it.
 *
 * Evidence limits: QEMU only. No physical controller is claimed.
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
#include "ozero/virtio_pci_cap.h"
#include "ozero/virtio_pci_mmio.h"
#include "ozero/virtio_queue.h"
#include "ozero/virtio_ring.h"

#include "serial.h"
#include "string.h"
#include "virtio_blk_internal.h"

#define VBM_MAX_DEVICES 2u
#define VBM_PCI_DEVICE_ID 0x1042u
#define VBM_PCI_VENDOR_ID 0x1AF4u
#define VBM_QUEUE_INDEX 0u
/* Request depth is one, so a small queue is enough: header, data, status. */
#define VBM_QUEUE_SIZE 8u
#define VBM_TIMEOUT_SPINS 20000000u
#define VBM_DMA_MASK ((UINT64_C(1) << 44) - 1u)

/* One window per device slot, one sub-window per capability BAR. BARs on a
 * modern virtio function are a few pages, so 8 MiB per BAR is ample and keeps
 * two functions from ever aliasing. */
#define VBM_WINDOW_BASE 0xffffc00050000000ull
#define VBM_DEVICE_STRIDE 0x04000000ull
#define VBM_BAR_STRIDE 0x00800000ull

#define VBM_CFG_CAPACITY 0x00u
#define VBM_CFG_BLK_SIZE 0x14u

#define VBM_F_BLK_SIZE (1u << 6)
#define VBM_F_FLUSH    (1u << 9)

#define VBM_T_IN    0u
#define VBM_T_OUT   1u
#define VBM_T_FLUSH 4u

#define VBM_S_OK    0u
#define VBM_S_IOERR 1u
#define VBM_S_UNSUPP 2u

#define VBM_STATUS_BYTE_OFFSET 16u

struct vbm_req_hdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct vblk_modern {
    struct o0_block_dev dev;
    struct o0_block_queue queue;
    struct o0_pci_device pci;

    struct o0_virtio_pci_caps caps;
    struct o0_virtio_pci_mapping map;
    struct o0_virtio_regs common;
    struct o0_virtio_regs notify;
    struct o0_virtio_regs isr;
    struct o0_virtio_regs device_cfg;
    struct o0_virtio_queue_info qinfo;

    struct o0_virtio_vq vq;
    struct o0_dma_buffer ring;
    struct o0_dma_buffer req;
    struct vbm_req_hdr *hdr;
    volatile uint8_t *status_byte;

    uint64_t capacity;
    uint32_t features_low;
    uint32_t features_high;
    uint32_t notify_multiplier;

    bool in_use;
    bool has_flush;
    bool dead;
    o0_vector_t vector;
    bool vector_ok;
    volatile uint64_t irq_count;
    volatile uint8_t last_isr;
};

static struct vblk_modern vbm_devices[VBM_MAX_DEVICES];

static void vbm_ignore(o0_status status) {
    (void)status;
}

static o0_status vbm_fail(const char *stage, o0_status status) {
    o0_serial_puts("virtio-blk modern: fail stage=");
    o0_serial_puts(stage);
    o0_serial_puts(" status=");
    o0_serial_puts(o0_status_name(status));
    o0_serial_puts("\n");
    return status;
}

static void vbm_print_region(const char *tag, const struct o0_virtio_pci_region *r) {
    o0_serial_puts("virtio-blk modern: region ");
    o0_serial_puts(tag);
    o0_serial_puts(" bar=");
    o0_serial_dec(r->present ? r->bar : 0xFFu);
    o0_serial_puts(" off=");
    o0_serial_hex(r->offset);
    o0_serial_puts(" len=");
    o0_serial_dec(r->length);
    o0_serial_puts(r->present ? "" : " absent");
    o0_serial_puts("\n");
}

static void vbm_print_regions(const struct vblk_modern *vm) {
    vbm_print_region("common", &vm->caps.common);
    vbm_print_region("notify", &vm->caps.notify);
    vbm_print_region("isr", &vm->caps.isr);
    vbm_print_region("device", &vm->caps.device);
}

static struct vblk_modern *vbm_from_dev(const struct o0_block_dev *dev) {
    for (size_t i = 0; i < VBM_MAX_DEVICES; ++i) {
        if (&vbm_devices[i].dev == dev && vbm_devices[i].in_use) {
            return &vbm_devices[i];
        }
    }
    return NULL;
}

static bool vbm_irq(o0_gsi_t gsi, void *ctx) {
    (void)gsi;
    struct vblk_modern *vm = (struct vblk_modern *)ctx;
    if (vm == NULL) {
        return false;
    }
    uint8_t isr = 0;
    /* Reading the ISR region acknowledges the interrupt. */
    if (o0_virtio_reg_read8(&vm->isr, 0u, &isr)) {
        vm->last_isr = isr;
    }
    vm->irq_count = vm->irq_count + 1u;
    return true;
}

static o0_block_status vbm_map_status(uint8_t status) {
    switch (status) {
        case VBM_S_OK:
            return O0_BLOCK_OK;
        case VBM_S_UNSUPP:
            return O0_BLOCK_ERR_NOTSUP;
        case VBM_S_IOERR:
        default:
            return O0_BLOCK_ERR_IO;
    }
}

/* Conventional config space is snapshotted as aligned dwords: the byte and word
 * accessors reject offsets above 0xFC, and a capability chain can reach there. */
static bool vbm_snapshot(const struct o0_pci_bdf *bdf, uint8_t cfg[256]) {
    for (uint32_t i = 0; i < 64u; ++i) {
        uint32_t word = 0;
        if (o0_pci_config_read32(*bdf, (uint8_t)(i * 4u), &word) != 0) {
            return false;
        }
        for (uint32_t b = 0; b < 4u; ++b) {
            cfg[i * 4u + b] = (uint8_t)((word >> (8u * b)) & 0xFFu);
        }
    }
    return true;
}

/* ---- Device and queue initialisation ---- */

static o0_status vbm_alloc_buffers(struct vblk_modern *vm, size_t ring_bytes,
                                   size_t req_bytes) {
    if (vm->ring.phys != 0) return O0_OK;
    struct o0_dma_constraint constraint;
    memset(&constraint, 0, sizeof(constraint));
    constraint.addr_mask = VBM_DMA_MASK;
    constraint.alignment = O0_PAGE_SIZE;
    constraint.max_segment = 0;
    constraint.coherent = true;

    size_t ring_alloc = (ring_bytes + O0_PAGE_SIZE - 1u) &
                        ~(size_t)(O0_PAGE_SIZE - 1u);
    if (o0_dma_alloc(&constraint, ring_alloc, &vm->ring) != O0_OK) {
        return O0_ENOMEM;
    }
    if (o0_dma_alloc(&constraint, req_bytes, &vm->req) != O0_OK) {
        vbm_ignore(o0_dma_free(&vm->ring));
        return O0_ENOMEM;
    }
    memset(vm->ring.virt, 0, vm->ring.size);
    memset(vm->req.virt, 0, vm->req.size);
    if (o0_dma_pin(&vm->ring) != O0_OK || o0_dma_pin(&vm->req) != O0_OK) {
        vbm_ignore(o0_dma_free(&vm->req));
        vbm_ignore(o0_dma_free(&vm->ring));
        return O0_EBUSY;
    }
    vm->hdr = (struct vbm_req_hdr *)vm->req.virt;
    vm->status_byte = (volatile uint8_t *)vm->req.virt + VBM_STATUS_BYTE_OFFSET;
    return O0_OK;
}

static o0_status vbm_read_capacity(struct vblk_modern *vm, uint64_t *out) {
    uint8_t raw[8];
    for (uint32_t i = 0; i < sizeof(raw); ++i) {
        if (!o0_virtio_reg_read8(&vm->device_cfg, VBM_CFG_CAPACITY + i, &raw[i])) {
            return O0_EIO;
        }
    }
    uint64_t value = 0;
    for (uint32_t i = 0; i < sizeof(raw); ++i) {
        value |= (uint64_t)raw[i] << (8u * i);
    }
    *out = value;
    return value != 0u ? O0_OK : O0_ENOTSUP;
}

/* Reset, renegotiate, re-program the queue and set DRIVER_OK. Used at probe and
 * again after a fault, so it must not free or reallocate anything. */
static o0_status vbm_device_init(struct vblk_modern *vm) {
    o0_status st = o0_virtio_reset(&vm->common);
    if (st != O0_OK) return vbm_fail("reset", st);

    uint32_t low = 0, high = 0;
    st = o0_virtio_features(&vm->common, &low, &high);
    if (st != O0_OK) return vbm_fail("features", st);

    uint32_t accept_low = 0;
    vm->has_flush = (low & VBM_F_FLUSH) != 0u;
    if (vm->has_flush) accept_low |= VBM_F_FLUSH;
    st = o0_virtio_negotiate(&vm->common, accept_low,
                             1u << (O0_VIRTIO_F_VERSION_1 - 32u), &low, &high);
    if (st != O0_OK) return vbm_fail("negotiate", st);
    vm->features_low = low;
    vm->features_high = high;

    uint16_t num_queues = 0;
    if (!o0_virtio_reg_read16(&vm->common, O0_VIRTIO_COMMON_NUM_QUEUES, &num_queues) ||
        num_queues <= VBM_QUEUE_INDEX) {
        return vbm_fail("num_queues", O0_ENOTSUP);
    }
    if (!o0_virtio_reg_write16(&vm->common, O0_VIRTIO_COMMON_QUEUE_SELECT,
                               VBM_QUEUE_INDEX)) {
        return O0_EIO;
    }
    uint16_t device_max = 0;
    if (!o0_virtio_reg_read16(&vm->common, O0_VIRTIO_COMMON_QUEUE_SIZE, &device_max)) {
        return O0_EIO;
    }
    uint16_t qsize = 0;
    if (!o0_virtio_queue_size_choose(device_max, VBM_QUEUE_SIZE, &qsize)) {
        return vbm_fail("queue_size", O0_ENOTSUP);
    }

    size_t avail_off = 0, used_off = 0, total = 0;
    if (!o0_virtio_split_ring_layout(qsize, &avail_off, &used_off, &total)) {
        return O0_EINVAL;
    }
    st = vbm_alloc_buffers(vm, total, O0_PAGE_SIZE);
    if (st != O0_OK) return vbm_fail("dma_alloc", st);
    if (vm->ring.size < total) return O0_ENOMEM;

    /* The rings are zeroed before the queue is enabled, so the device never
     * observes stale indices from a previous configuration. */
    if (!o0_virtio_vq_init(&vm->vq, vm->ring.virt, qsize)) return O0_EINVAL;

    struct o0_virtio_queue_plan plan = {
        .index = VBM_QUEUE_INDEX,
        .size = qsize,
        .desc_phys = vm->ring.phys,
        .avail_phys = vm->ring.phys + (uint64_t)avail_off,
        .used_phys = vm->ring.phys + (uint64_t)used_off,
        .addr_mask = VBM_DMA_MASK
    };
    st = o0_virtio_queue_configure(&vm->common, &plan, &vm->qinfo);
    if (st != O0_OK) return vbm_fail("queue_configure", st);

    /* Re-arm the vector: a device reset clears the MSI enable state. */
    if (vm->vector_ok && vm->pci.resources.msi_supported) {
        (void)o0_pci_msi_enable(vm->pci.bdf, vm->pci.resources.msi_cap_offset,
                                o0_lapic_id(), vm->vector);
    }

    st = o0_virtio_driver_ok(&vm->common);
    if (st != O0_OK) return vbm_fail("driver_ok", st);
    st = vbm_read_capacity(vm, &vm->capacity);
    if (st != O0_OK) return vbm_fail("capacity", st);
    return O0_OK;
}

/* ---- Request submission and completion ---- */

/* Returns the driver to a known state after a fault. The device is reset first,
 * because that is the only operation that guarantees it has stopped touching
 * ring and request memory. */
static o0_block_status vbm_recover(struct vblk_modern *vm) {
    if (o0_virtio_reset(&vm->common) != O0_OK) {
        vm->dead = true;
        return O0_BLOCK_ERR_RESET;
    }
    if (vbm_device_init(vm) != O0_OK) {
        vm->dead = true;
        return O0_BLOCK_ERR_RESET;
    }
    return O0_BLOCK_ERR_TRANSPORT;
}

static o0_block_status vbm_run(struct vblk_modern *vm, uint32_t type,
                               uint64_t sector, uint64_t data_phys,
                               uint32_t data_len, bool data_to_device) {
    if (vm->dead) return O0_BLOCK_ERR_RESET;

    uint16_t idx[3];
    uint16_t count = data_len != 0u ? 3u : 2u;
    for (uint16_t i = 0; i < count; ++i) {
        if (!o0_virtio_vq_alloc(&vm->vq, 1u, &idx[i])) {
            for (uint16_t j = 0; j < i; ++j) {
                o0_virtio_vq_release(&vm->vq, idx[j], 1u);
            }
            return O0_BLOCK_ERR_AGAIN;
        }
    }

    vm->hdr->type = type;
    vm->hdr->reserved = 0;
    vm->hdr->sector = sector;
    *vm->status_byte = 0xFFu;

    uint16_t head = idx[0];
    o0_virtio_vq_set(&vm->vq, idx[0], vm->req.phys,
                     (uint32_t)sizeof(struct vbm_req_hdr),
                     O0_VRING_DESC_F_NEXT, idx[1]);
    uint16_t status_idx = count - 1u;
    if (data_len != 0u) {
        o0_virtio_vq_set(&vm->vq, idx[1], data_phys, data_len,
                         (uint16_t)(O0_VRING_DESC_F_NEXT |
                                    (data_to_device ? 0u : O0_VRING_DESC_F_WRITE)),
                         idx[2]);
    }
    o0_virtio_vq_set(&vm->vq, status_idx,
                     vm->req.phys + VBM_STATUS_BYTE_OFFSET, 1u,
                     O0_VRING_DESC_F_WRITE, 0u);

    if (!o0_virtio_vq_publish(&vm->vq, head)) {
        o0_virtio_vq_release(&vm->vq, head, count);
        return O0_BLOCK_ERR_AGAIN;
    }
    if (o0_virtio_notify(&vm->notify, (uint16_t)vm->qinfo.index,
                         vm->qinfo.notify_off, vm->notify_multiplier) != O0_OK) {
        return vbm_recover(vm);
    }

    for (uint32_t spin = 0; spin < VBM_TIMEOUT_SPINS; ++spin) {
        uint32_t len = 0;
        enum o0_virtio_vq_poll poll =
            o0_virtio_vq_poll(&vm->vq, head, &len);
        if (poll == O0_VIRTIO_VQ_DONE) {
            uint8_t status = *vm->status_byte;
            o0_virtio_vq_release(&vm->vq, head, count);
            return vbm_map_status(status);
        }
        if (poll == O0_VIRTIO_VQ_BAD_ID || poll == O0_VIRTIO_VQ_OVERRUN) {
            o0_serial_puts("virtio-blk modern: malformed completion\n");
            return vbm_recover(vm);
        }
        __asm__ volatile ("" : : : "memory");
    }

    o0_serial_puts("virtio-blk modern timeout dump: qsize=");
    o0_serial_dec(vm->qinfo.size);
    o0_serial_puts(" desc="); o0_serial_hex(vm->ring.phys);
    o0_serial_puts(" status="); o0_serial_hex(vm->status_byte != NULL ? *vm->status_byte : 0xFFu);
    o0_serial_puts(" features="); o0_serial_hex(vm->features_low);
    o0_serial_puts("/"); o0_serial_hex(vm->features_high);
    o0_serial_puts(" dev_off="); o0_serial_hex(vm->caps.device.offset);
    o0_serial_puts(" dev_len="); o0_serial_dec(vm->caps.device.length);
    o0_serial_puts(" capacity="); o0_serial_dec(vm->capacity);
    o0_serial_puts(" notify_off="); o0_serial_dec(vm->qinfo.notify_off);
    o0_serial_puts(" avail="); o0_serial_dec(vm->vq.next_avail);
    o0_serial_puts(" used="); o0_serial_dec(vm->vq.last_used);
    o0_serial_puts(" isr="); o0_serial_hex(vm->last_isr);
    o0_serial_puts(" irq="); o0_serial_dec(vm->irq_count);
    o0_serial_puts("\n");
    /* The device may still own the descriptors and the data buffer, so nothing
     * is released here: reset first. */
    return vbm_recover(vm);
}

static o0_status vbm_queue_rq(struct o0_block_queue *queue, struct o0_bio *bio) {
    if (queue == NULL || bio == NULL) return O0_EINVAL;
    struct vblk_modern *vm = (struct vblk_modern *)queue->driver_data;
    if (vm == NULL || !vm->in_use) return O0_ENOENT;

    if (bio->op == O0_BLOCK_FLUSH) {
        if (!vm->has_flush) {
            o0_block_complete_bio(bio, O0_BLOCK_ERR_NOTSUP, 0u);
            return O0_OK;
        }
        o0_block_status st = vbm_run(vm, VBM_T_FLUSH, 0u, 0u, 0u, false);
        o0_block_complete_bio(bio, st, 0u);
        return O0_OK;
    }
    if (bio->op != O0_BLOCK_READ && bio->op != O0_BLOCK_WRITE) {
        return O0_ENOTSUP;
    }
    if (bio->nr_vecs != 1u || bio->vecs == NULL || bio->vecs[0].page == NULL) {
        return O0_EINVAL;
    }
    const struct o0_bio_vec *vec = &bio->vecs[0];
    if (vec->len == 0u || vec->len > UINT32_MAX) return O0_EINVAL;
    uint8_t *buffer = (uint8_t *)vec->page + vec->offset;
    uint64_t phys = o0_virt_to_phys(buffer);
    /* No IOMMU and no bounce buffer for data yet, so a buffer the device cannot
     * address is refused instead of being handed over untranslated. */
    if (!o0_virtio_queue_address_ok(phys, (uint64_t)vec->len, VBM_DMA_MASK, 1u)) {
        return O0_ENOTSUP;
    }

    o0_block_status st = vbm_run(vm, bio->op == O0_BLOCK_WRITE ? VBM_T_OUT : VBM_T_IN,
                                 bio->lba, phys, (uint32_t)vec->len,
                                 bio->op == O0_BLOCK_WRITE);
    o0_block_complete_bio(bio, st, st == O0_BLOCK_OK ? vec->len : 0u);
    return O0_OK;
}

static o0_status vbm_reset(struct o0_block_dev *dev) {
    return vbm_from_dev(dev) != NULL ? O0_OK : O0_ENOENT;
}

static o0_status vbm_flush(struct o0_block_dev *dev) {
    return dev != NULL ? O0_OK : O0_EINVAL;
}

static o0_status vbm_geometry(struct o0_block_dev *dev,
                              struct o0_block_geometry *out) {
    if (dev == NULL || out == NULL) return O0_EINVAL;
    *out = dev->geo;
    return O0_OK;
}

static bool vbm_media_changed(struct o0_block_dev *dev) {
    (void)dev;
    return false;
}

static o0_status vbm_open(struct o0_block_dev *dev, bool exclusive) {
    (void)exclusive;
    return dev != NULL ? O0_OK : O0_EINVAL;
}

static void vbm_close(struct o0_block_dev *dev) {
    (void)dev;
}

static const struct o0_block_ops vbm_block_ops = {
    .reset = vbm_reset,
    .flush = vbm_flush,
    .discard = NULL,
    .write_zeroes = NULL,
    .open = vbm_open,
    .close = vbm_close,
    .media_changed = vbm_media_changed,
    .get_geometry = vbm_geometry,
    .set_features = NULL
};

static const struct o0_block_queue_ops vbm_queue_ops = {
    .queue_rq = vbm_queue_rq,
    .poll = NULL,
    .timeout = NULL
};

static void vbm_set_name(char *dst, const char *src) {
    size_t i = 0;
    for (; i + 1u < O0_BLOCK_NAME_MAX && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* Releases every resource. The device is reset first so it cannot be writing to
 * ring or request memory while the buffers are freed and the BARs unmapped. */
static void vbm_teardown(struct vblk_modern *vm) {
    if (vm->common.iface != NULL) {
        vbm_ignore(o0_virtio_reset(&vm->common));
    }
    if (vm->vector_ok) {
        (void)o0_irq_unregister_vector(vm->vector, vbm_irq, vm);
        o0_irq_free_vectors(vm->vector, 1);
        vm->vector_ok = false;
    }
    if (vm->req.phys != 0) {
        vbm_ignore(o0_dma_unpin(&vm->req));
        vbm_ignore(o0_dma_free(&vm->req));
    }
    if (vm->ring.phys != 0) {
        vbm_ignore(o0_dma_unpin(&vm->ring));
        vbm_ignore(o0_dma_free(&vm->ring));
    }
    if (vm->map.window_stride != 0u) {
        vbm_ignore(o0_virtio_pci_unmap_caps(&vm->map));
    }
    vm->in_use = false;
}

o0_status o0_virtio_blk_modern_probe(const char *name, struct o0_block_dev **out) {
    if (name == NULL || out == NULL || name[0] == '\0') return O0_EINVAL;
    *out = NULL;

    const struct o0_pci_device *found = NULL;
    for (size_t i = 0; i < o0_pci_device_count(); ++i) {
        const struct o0_pci_device *pci = o0_pci_device(i);
        if (pci != NULL && pci->vendor_id == VBM_PCI_VENDOR_ID &&
            pci->device_id == VBM_PCI_DEVICE_ID) {
            found = pci;
            break;
        }
    }
    if (found == NULL) {
        o0_serial_puts("virtio-blk modern: no function id=");
        o0_serial_hex(VBM_PCI_DEVICE_ID);
        o0_serial_puts(" among ");
        o0_serial_dec(o0_pci_device_count());
        o0_serial_puts(" pci devices\n");
        return O0_ENOENT;
    }

    struct vblk_modern *vm = NULL;
    size_t slot = 0;
    for (size_t i = 0; i < VBM_MAX_DEVICES; ++i) {
        if (!vbm_devices[i].in_use) {
            vm = &vbm_devices[i];
            slot = i;
            break;
        }
    }
    if (vm == NULL) return O0_EBUSY;
    memset(vm, 0, sizeof(*vm));
    vm->pci = *found;

    uint8_t cfg[256];
    if (!vbm_snapshot(&found->bdf, cfg)) return vbm_fail("snapshot", O0_EIO);

    struct o0_pci_bar bars[O0_VIRTIO_PCI_CFG_MAX_BARS];
    uint32_t bar_lengths[O0_VIRTIO_PCI_CFG_MAX_BARS];
    for (uint8_t bar = 0; bar < O0_VIRTIO_PCI_CFG_MAX_BARS; ++bar) {
        memset(&bars[bar], 0, sizeof(bars[bar]));
        if (o0_pci_probe_bar(found->bdf, bar, found->resources.header_type,
                             &bars[bar]) != 0) {
            return vbm_fail("bar_probe", O0_EIO);
        }
        bar_lengths[bar] = bars[bar].size > UINT32_MAX ? 0u : (uint32_t)bars[bar].size;
    }
    if (!o0_virtio_pci_parse_caps(cfg, sizeof(cfg), bar_lengths, &vm->caps)) {
        return vbm_fail("caps", O0_ENOTSUP);
    }
    if (!o0_virtio_pci_validate_bars(&vm->caps, bars)) return vbm_fail("bars", O0_ENOTSUP);

    /* Memory decode and bus mastering: a modern device has no I/O BAR. */
    uint16_t command = 0;
    if (o0_pci_config_read16(found->bdf, 0x04u, &command) != 0) {
        return vbm_fail("pci_cmd_read", O0_EIO);
    }
    command = (uint16_t)(command | 0x0002u | 0x0004u);
    if (o0_pci_config_write16(found->bdf, 0x04u, command) != 0) {
        return vbm_fail("pci_cmd_write", O0_EIO);
    }
    uint16_t observed = 0;
    if (o0_pci_config_read16(found->bdf, 0x04u, &observed) != 0 ||
        (observed & 0x0006u) != 0x0006u) {
        return vbm_fail("pci_cmd_verify", O0_EIO);
    }

    o0_status st = o0_virtio_pci_map_caps(
        &found->bdf, found->resources.header_type, &vm->caps,
        VBM_WINDOW_BASE + (uint64_t)slot * VBM_DEVICE_STRIDE, VBM_BAR_STRIDE,
        &vm->map);
    if (st != O0_OK) return vbm_fail("map", st);

    if (!o0_virtio_pci_common_regs(&vm->caps, &vm->map, &vm->common) ||
        !o0_virtio_pci_region_regs(&vm->caps.notify, &vm->map, &vm->notify) ||
        !o0_virtio_pci_region_regs(&vm->caps.isr, &vm->map, &vm->isr) ||
        !o0_virtio_pci_region_regs(&vm->caps.device, &vm->map, &vm->device_cfg)) {
        vbm_print_regions(vm);
        vbm_teardown(vm);
        return vbm_fail("regions", O0_ERANGE);
    }
    vm->notify_multiplier = vm->caps.notify_multiplier;

    if (found->resources.msi_supported) {
        o0_vector_t vector = O0_VECTOR_NONE;
        if (o0_irq_alloc_vectors(1, &vector) == O0_OK &&
            o0_irq_register_vector(vector, vbm_irq, vm) == O0_OK) {
            vm->vector = vector;
            vm->vector_ok = true;
        }
    }

    st = vbm_device_init(vm);
    if (st != O0_OK) {
        vbm_teardown(vm);
        return st;
    }

    o0_serial_puts("OZERO: virtio-blk modern ready queue=");
    o0_serial_dec(vm->qinfo.size);
    o0_serial_puts(" qindex="); o0_serial_dec(vm->qinfo.index);
    o0_serial_puts(" notify_off="); o0_serial_dec(vm->qinfo.notify_off);
    o0_serial_puts(" notify_mul="); o0_serial_dec(vm->notify_multiplier);
    o0_serial_puts(" desc="); o0_serial_hex(vm->ring.phys);
    o0_serial_puts(" features="); o0_serial_hex(vm->features_low);
    o0_serial_puts("/"); o0_serial_hex(vm->features_high);
    o0_serial_puts(" capacity="); o0_serial_dec(vm->capacity);
    o0_serial_puts(" pci_cmd="); o0_serial_hex(observed);
    o0_serial_puts(" msi="); o0_serial_puts(vm->vector_ok ? "on" : "off");
    o0_serial_puts("\n");

    vm->in_use = true;
    vbm_set_name(vm->dev.name, name);
    vm->dev.geo.logical_block_size = 512u;
    vm->dev.geo.physical_block_size = 512u;
    vm->dev.geo.nr_sectors = vm->capacity;
    vm->dev.geo.max_hw_sectors = O0_PAGE_SIZE / 512u;
    vm->dev.geo.max_segments = 1;
    vm->dev.geo.max_segment_size = O0_PAGE_SIZE;
    vm->dev.geo.dma_alignment = 512u;
    vm->dev.geo.queue_depth = 1;
    vm->dev.geo.features = O0_BLOCK_F_DMA_COHERENT |
                           (vm->has_flush ? O0_BLOCK_F_FLUSH : 0u);
    vm->dev.nr_queues = 1;
    vm->dev.queues = &vm->queue;
    vm->dev.ops = &vbm_block_ops;
    vm->dev.driver_data = vm;
    vm->queue.id = 0;
    vm->queue.depth = 1;
    vm->queue.ops = &vbm_queue_ops;
    vm->queue.driver_data = vm;

    st = o0_block_register(&vm->dev);
    if (st != O0_OK) {
        vbm_teardown(vm);
        return st;
    }
    *out = &vm->dev;
    return O0_OK;
}

uint64_t o0_virtio_blk_modern_irq_count(struct o0_block_dev *dev) {
    struct vblk_modern *vm = vbm_from_dev(dev);
    return vm != NULL ? vm->irq_count : 0u;
}

o0_status o0_virtio_blk_modern_inject_error(struct o0_block_dev *dev) {
    struct vblk_modern *vm = vbm_from_dev(dev);
    if (vm == NULL) return O0_ENOENT;
    if (vm->dead) return O0_EIO;
    /* A two-sector read starting at the last sector runs off the end of the
     * medium. The device must report the failure and the driver must decode it
     * instead of hanging or reporting success. */
    uint64_t sector = vm->capacity > 1u ? vm->capacity - 1u : 0u;
    if (vm->req.size < VBM_STATUS_BYTE_OFFSET + 1024u) return O0_ENOMEM;
    uint64_t phys = vm->req.phys + VBM_STATUS_BYTE_OFFSET;
    o0_block_status st = vbm_run(vm, VBM_T_IN, sector, phys, 1024u, false);
    if (st == O0_BLOCK_OK) return O0_EIO; /* the device accepted an out-of-range read */
    if (st == O0_BLOCK_ERR_TIMEOUT || st == O0_BLOCK_ERR_RESET ||
        st == O0_BLOCK_ERR_TRANSPORT || st == O0_BLOCK_ERR_AGAIN) {
        return O0_EIO;
    }
    return O0_OK;
}
