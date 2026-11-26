/* Ozero kernel - NVMe driver (Wave 4.3).
 *
 * Transport: BAR0 is a 64-bit memory BAR, mapped uncached through the kernel
 * address space. The driver runs the controller in NVM mode with one admin queue
 * pair and one I/O queue pair. Queues and data buffers come from the DMA
 * allocator, are physically contiguous and page aligned, and stay pinned for the
 * device lifetime.
 *
 * Completion: a command's completion entry is valid when its phase tag matches
 * the queue's expected phase. The submitting call polls with a bounded spin,
 * advances the completion head and rings the completion doorbell; a lost
 * interrupt becomes O0_BLOCK_ERR_TIMEOUT. MSI-X is programmed so the device has
 * a real interrupt path (admin CQ in table entry 0, I/O CQ in entry 1), and the
 * handler counts them, but the poll is what completion depends on.
 *
 * Evidence limits: QEMU `-device nvme` only, namespace 1, one page per request,
 * no PRP lists and no queue depth beyond one in flight.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/block.h"
#include "ozero/irq.h"
#include "ozero/lapic.h"
#include "ozero/mm.h"
#include "ozero/nvme.h"
#include "ozero/pci.h"
#include "ozero/status.h"
#include "ozero/types.h"

#include "string.h"

#define NVME_MAX_DEVICES 2u
#define NVME_BAR_WINDOW_BASE 0xffffc00030000000ull
#define NVME_BAR_WINDOW_STRIDE 0x02000000ull
#define NVME_MSIX_WINDOW_BASE 0xffffc00040000000ull
#define NVME_MSIX_WINDOW_STRIDE 0x02000000ull
#define NVME_TIMEOUT_SPINS 20000000u
#define NVME_READY_SPINS 8000000u
#define NVME_QDEPTH 8u

#define NVME_REG_CAP 0x00u
#define NVME_REG_VS 0x08u
#define NVME_REG_INTMS 0x0Cu
#define NVME_REG_INTMC 0x10u
#define NVME_REG_CC 0x14u
#define NVME_REG_CSTS 0x1Cu
#define NVME_REG_AQA 0x24u
#define NVME_REG_ASQ 0x28u
#define NVME_REG_ACQ 0x30u

#define NVME_CC_EN (1u << 0)
#define NVME_CC_IOSQES (6u << 16)
#define NVME_CC_IOCQES (4u << 20)
#define NVME_CSTS_RDY (1u << 0)

#define NVME_ADMIN_CREATE_SQ 0x01u
#define NVME_ADMIN_CREATE_CQ 0x05u
#define NVME_ADMIN_IDENTIFY 0x06u
#define NVME_IO_FLUSH 0x00u
#define NVME_IO_WRITE 0x01u
#define NVME_IO_READ 0x02u

struct o0_nvme_sqe {
    uint32_t cdw0;
    uint32_t nsid;
    uint32_t reserved[2];
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed));

struct o0_nvme_cqe {
    uint32_t result;
    uint32_t reserved;
    uint32_t sq_head_sqid;
    uint32_t cid_status; /* command id [15:0], phase [16], status [31:17] */
} __attribute__((packed));

struct o0_nvme {
    struct o0_block_dev dev;
    struct o0_block_queue queue;
    struct o0_pci_device pci;
    volatile uint8_t *regs;
    uint32_t doorbell_stride;
    uint16_t queue_depth;
    uint16_t nsid;
    bool io_ready;
    uint32_t sector_size;
    uint64_t sectors;
    bool in_use;

    struct o0_dma_buffer admin_sq;
    struct o0_dma_buffer admin_cq;
    struct o0_dma_buffer io_sq;
    struct o0_dma_buffer io_cq;
    struct o0_dma_buffer identify;
    struct o0_dma_buffer io_data;

    struct o0_nvme_sqe *asq;
    struct o0_nvme_sqe *isq;
    volatile struct o0_nvme_cqe *acq;
    volatile struct o0_nvme_cqe *icq;

    uint16_t aq_head;
    uint16_t aq_tail;
    uint32_t aq_phase;
    uint16_t admin_cid;
    uint16_t io_head;
    uint16_t io_tail;
    uint32_t io_phase;
    uint16_t io_cid;

    o0_vector_t vector;
    bool vector_ok;
    volatile uint64_t irq_count;
};

static struct o0_nvme nvme_devices[NVME_MAX_DEVICES];

static inline uint32_t nvme_read32(struct o0_nvme *nd, uint32_t off) {
    return *(volatile uint32_t *)(nd->regs + off);
}

static inline void nvme_write32(struct o0_nvme *nd, uint32_t off,
                                uint32_t value) {
    *(volatile uint32_t *)(nd->regs + off) = value;
}

static inline uint64_t nvme_read64(struct o0_nvme *nd, uint32_t off) {
    uint64_t lo = nvme_read32(nd, off);
    uint64_t hi = nvme_read32(nd, off + 4u);
    return lo | (hi << 32);
}

static inline void nvme_write64(struct o0_nvme *nd, uint32_t off,
                                uint64_t value) {
    nvme_write32(nd, off, (uint32_t)(value & 0xFFFFFFFFu));
    nvme_write32(nd, off + 4u, (uint32_t)(value >> 32));
}

static o0_status nvme_restart(struct o0_nvme *nd);

static void nvme_ignore(o0_status status) {
    (void)status;
}

static struct o0_nvme *nvme_from_dev(const struct o0_block_dev *dev) {
    for (size_t i = 0; i < NVME_MAX_DEVICES; ++i) {
        if (&nvme_devices[i].dev == dev && nvme_devices[i].in_use) {
            return &nvme_devices[i];
        }
    }
    return NULL;
}

static bool nvme_irq(o0_gsi_t gsi, void *ctx) {
    (void)gsi;
    struct o0_nvme *nd = (struct o0_nvme *)ctx;
    if (nd == NULL) {
        return false;
    }
    /* NVMe acknowledges a completion by advancing the queue head doorbell, which
     * the submitting call does. The handler only records that the vector fired;
     * it is deliberately not load-bearing for completion. */
    nd->irq_count = nd->irq_count + 1u;
    return true;
}

static void nvme_dma_teardown(struct o0_nvme *nd) {
    struct o0_dma_buffer *buffers[6] = {
        &nd->admin_sq, &nd->admin_cq, &nd->io_sq,
        &nd->io_cq, &nd->identify, &nd->io_data
    };
    for (size_t i = 0; i < 6; ++i) {
        nvme_ignore(o0_dma_unpin(buffers[i]));
        nvme_ignore(o0_dma_free(buffers[i]));
    }
    nd->asq = NULL;
    nd->isq = NULL;
    nd->acq = NULL;
    nd->icq = NULL;
}

static o0_status nvme_dma_setup(struct o0_nvme *nd) {
    struct o0_dma_constraint constraint;
    memset(&constraint, 0, sizeof(constraint));
    constraint.addr_mask = UINT64_MAX;
    constraint.alignment = O0_PAGE_SIZE;
    constraint.coherent = true;

    struct o0_dma_buffer *buffers[6] = {
        &nd->admin_sq, &nd->admin_cq, &nd->io_sq,
        &nd->io_cq, &nd->identify, &nd->io_data
    };
    for (size_t i = 0; i < 6; ++i) {
        o0_status st = o0_dma_alloc(&constraint, O0_PAGE_SIZE, buffers[i]);
        if (st != O0_OK) {
            nvme_dma_teardown(nd);
            return st;
        }
        st = o0_dma_pin(buffers[i]);
        if (st != O0_OK) {
            nvme_dma_teardown(nd);
            return st;
        }
        memset(buffers[i]->virt, 0, buffers[i]->size);
    }

    nd->asq = (struct o0_nvme_sqe *)nd->admin_sq.virt;
    nd->isq = (struct o0_nvme_sqe *)nd->io_sq.virt;
    nd->acq = (volatile struct o0_nvme_cqe *)nd->admin_cq.virt;
    nd->icq = (volatile struct o0_nvme_cqe *)nd->io_cq.virt;
    /* The controller starts each completion queue in phase 1 and flips the tag
     * after every wrap, so the first expected phase is 1, not 0. */
    nd->aq_phase = 1u;
    nd->io_phase = 1u;
    return O0_OK;
}

static void nvme_ring_sq(struct o0_nvme *nd, uint32_t qid, uint16_t tail) {
    nvme_write32(nd, 0x1000u + (2u * qid) * nd->doorbell_stride, tail);
}

static void nvme_ring_cq(struct o0_nvme *nd, uint32_t qid, uint16_t head) {
    nvme_write32(nd, 0x1000u + (2u * qid + 1u) * nd->doorbell_stride, head);
}

enum nvme_wait {
    NVME_WAIT_DONE,
    NVME_WAIT_TIMEOUT,
    NVME_WAIT_STALE
};

static enum nvme_wait nvme_wait_cqe(struct o0_nvme *nd, uint32_t qid,
                                    volatile struct o0_nvme_cqe *cq,
                                    uint16_t *head, uint32_t *phase,
                                    uint16_t qsize, uint16_t cid,
                                    struct o0_nvme_cqe *out) {
    for (uint32_t spin = 0; spin < NVME_TIMEOUT_SPINS; ++spin) {
        struct o0_nvme_cqe entry = cq[*head];
        __asm__ volatile ("" : : : "memory");
        if (o0_nvme_cqe_phase(entry.cid_status) != *phase) {
            continue;
        }
        /* The phase tag alone cannot tell this command's completion from a late
         * one belonging to a command that already timed out. */
        if (o0_nvme_cqe_cid(entry.cid_status) != cid) {
            return NVME_WAIT_STALE;
        }
        *out = entry;
        ++(*head);
        if (*head == qsize) {
            *head = 0;
            *phase ^= 1u;
        }
        nvme_ring_cq(nd, qid, *head);
        return NVME_WAIT_DONE;
    }
    return NVME_WAIT_TIMEOUT;
}

static o0_status nvme_admin(struct o0_nvme *nd, uint8_t opcode, uint32_t nsid,
                            uint64_t prp1, uint32_t cdw10, uint32_t cdw11,
                            uint32_t cdw12) {
    uint16_t index = nd->aq_tail;
    uint16_t cid = nd->admin_cid;
    struct o0_nvme_sqe *sqe = &nd->asq[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->cdw0 = o0_nvme_cdw0(opcode, cid);
    sqe->nsid = nsid;
    sqe->prp1 = prp1;
    sqe->cdw10 = cdw10;
    sqe->cdw11 = cdw11;
    sqe->cdw12 = cdw12;
    nd->admin_cid = (uint16_t)(cid + 1u);
    nd->aq_tail = (uint16_t)((index + 1u) % nd->queue_depth);
    __asm__ volatile ("mfence" : : : "memory");
    nvme_ring_sq(nd, 0u, nd->aq_tail);

    struct o0_nvme_cqe cqe;
    enum nvme_wait r = nvme_wait_cqe(nd, 0u, nd->acq, &nd->aq_head,
                                     &nd->aq_phase, nd->queue_depth, cid, &cqe);
    if (r != NVME_WAIT_DONE) {
        /* The command still owns its slot, so nothing may reuse the queue until
         * the controller has been brought back to a known state. */
        (void)nvme_restart(nd);
        return r == NVME_WAIT_TIMEOUT ? O0_ETIMEDOUT : O0_EIO;
    }
    return o0_nvme_cqe_status(cqe.cid_status) == 0u ? O0_OK : O0_EIO;
}

static o0_block_status nvme_io(struct o0_nvme *nd, bool write, uint64_t slba,
                               uint16_t nlb, uint64_t data_phys) {
    if (nlb == 0u || nd->sector_size == 0u) {
        return O0_BLOCK_ERR_INVAL;
    }
    if (!o0_nvme_prp_fits_one_page(data_phys,
                                   (uint32_t)nlb * nd->sector_size)) {
        return O0_BLOCK_ERR_INVAL;
    }
    uint16_t index = nd->io_tail;
    uint16_t cid = nd->io_cid;
    struct o0_nvme_sqe *sqe = &nd->isq[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->cdw0 = o0_nvme_cdw0(write ? NVME_IO_WRITE : NVME_IO_READ, cid);
    sqe->nsid = nd->nsid;
    sqe->prp1 = data_phys;
    sqe->cdw10 = (uint32_t)(slba & 0xFFFFFFFFu);
    sqe->cdw11 = (uint32_t)(slba >> 32);
    sqe->cdw12 = (uint32_t)(nlb - 1u);
    nd->io_cid = (uint16_t)(cid + 1u);
    nd->io_tail = (uint16_t)((index + 1u) % nd->queue_depth);
    __asm__ volatile ("mfence" : : : "memory");
    nvme_ring_sq(nd, 1u, nd->io_tail);

    struct o0_nvme_cqe cqe;
    enum nvme_wait r = nvme_wait_cqe(nd, 1u, nd->icq, &nd->io_head,
                                     &nd->io_phase, nd->queue_depth, cid, &cqe);
    if (r != NVME_WAIT_DONE) {
        (void)nvme_restart(nd);
        return r == NVME_WAIT_TIMEOUT ? O0_BLOCK_ERR_TIMEOUT
                                      : O0_BLOCK_ERR_IO;
    }
    return o0_nvme_cqe_status(cqe.cid_status) == 0u ? O0_BLOCK_OK
                                                    : O0_BLOCK_ERR_IO;
}

static o0_block_status nvme_flush_command(struct o0_nvme *nd) {
    uint16_t index = nd->io_tail;
    uint16_t cid = nd->io_cid;
    struct o0_nvme_sqe *sqe = &nd->isq[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->cdw0 = o0_nvme_cdw0(NVME_IO_FLUSH, cid);
    sqe->nsid = nd->nsid;
    nd->io_cid = (uint16_t)(cid + 1u);
    nd->io_tail = (uint16_t)((index + 1u) % nd->queue_depth);
    __asm__ volatile ("mfence" : : : "memory");
    nvme_ring_sq(nd, 1u, nd->io_tail);

    struct o0_nvme_cqe cqe;
    enum nvme_wait r = nvme_wait_cqe(nd, 1u, nd->icq, &nd->io_head,
                                     &nd->io_phase, nd->queue_depth, cid, &cqe);
    if (r != NVME_WAIT_DONE) {
        (void)nvme_restart(nd);
        return r == NVME_WAIT_TIMEOUT ? O0_BLOCK_ERR_TIMEOUT
                                      : O0_BLOCK_ERR_IO;
    }
    return o0_nvme_cqe_status(cqe.cid_status) == 0u ? O0_BLOCK_OK
                                                    : O0_BLOCK_ERR_IO;
}

static o0_status nvme_identify_namespace(struct o0_nvme *nd) {
    o0_status st = nvme_admin(nd, NVME_ADMIN_IDENTIFY, 1u, nd->identify.phys,
                              0u, 0u, 0u);
    if (st != O0_OK) {
        return st;
    }
    const uint8_t *id = (const uint8_t *)nd->identify.virt;
    uint64_t nsze = 0;
    memcpy(&nsze, id, sizeof(nsze));
    uint8_t flbas = (uint8_t)(id[26] & 0x0Fu);
    uint8_t lbads = id[128u + 4u * (size_t)flbas + 2u];
    nd->sector_size = (lbads >= 9u && lbads <= 12u) ? (1u << lbads) : 512u;
    nd->sectors = nsze;
    return nsze != 0u ? O0_OK : O0_ENOTSUP;
}

/* Maps BAR0 and returns its reported size. A 64-bit memory BAR is required. */
static o0_status nvme_map_bar0(const struct o0_pci_device *pci, uint64_t window,
                               volatile uint8_t **out) {
    struct o0_pci_bar bar;
    if (o0_pci_probe_bar(pci->bdf, 0u, pci->resources.header_type, &bar) != 0) {
        return O0_EIO;
    }
    if (!bar.valid || !bar.is_memory || bar.base == 0u || bar.size == 0u) {
        return O0_ENOTSUP;
    }
    uint64_t base = bar.base & ~(uint64_t)(O0_PAGE_SIZE - 1u);
    size_t size = (size_t)((bar.base - base) + bar.size);
    if (size < O0_PAGE_SIZE) {
        size = O0_PAGE_SIZE;
    }
    size = (size + O0_PAGE_SIZE - 1u) & ~(O0_PAGE_SIZE - 1u);
    o0_status st = o0_as_map(o0_as_kernel(), window, base, size,
                             O0_MAP_READ | O0_MAP_WRITE | O0_MAP_UC | O0_MAP_NX);
    if (st != O0_OK) {
        return st;
    }
    *out = (volatile uint8_t *)(uintptr_t)window;
    return O0_OK;
}

static bool nvme_disable_controller(struct o0_nvme *nd) {
    uint32_t cc = nvme_read32(nd, NVME_REG_CC);
    if ((cc & NVME_CC_EN) != 0u) {
        nvme_write32(nd, NVME_REG_CC, cc & ~NVME_CC_EN);
    }
    for (uint32_t spin = 0; spin < NVME_READY_SPINS; ++spin) {
        if ((nvme_read32(nd, NVME_REG_CSTS) & NVME_CSTS_RDY) == 0u) {
            return true;
        }
    }
    return false;
}

static bool nvme_enable_controller(struct o0_nvme *nd) {
    uint32_t cc = nvme_read32(nd, NVME_REG_CC);
    cc = (cc | NVME_CC_EN | NVME_CC_IOSQES | NVME_CC_IOCQES) & ~(0xFu << 7);
    nvme_write32(nd, NVME_REG_CC, cc);
    for (uint32_t spin = 0; spin < NVME_READY_SPINS; ++spin) {
        if ((nvme_read32(nd, NVME_REG_CSTS) & NVME_CSTS_RDY) != 0u) {
            return true;
        }
    }
    return false;
}

/* Creates I/O queue pair 1. Re-runnable: a controller reset destroys every I/O
 * queue the controller had, so the pair has to be created again afterwards. */
static o0_status nvme_io_queues_create(struct o0_nvme *nd) {
    uint32_t iv = nd->vector_ok ? 1u : 0u;
    uint32_t ien = nd->vector_ok ? 1u : 0u;
    o0_status st = nvme_admin(nd, NVME_ADMIN_CREATE_CQ, 0u, 0u,
                              (uint32_t)(1u | ((nd->queue_depth - 1u) << 16)),
                              (1u | (ien << 1) | (iv << 16)), 0u);
    if (st != O0_OK) {
        return st;
    }
    return nvme_admin(nd, NVME_ADMIN_CREATE_SQ, 0u, 0u,
                      (uint32_t)(1u | ((nd->queue_depth - 1u) << 16)),
                      (1u | (1u << 16)), 0u);
}

/* Disable, drop every queue and its bookkeeping, then enable again. A command
 * that never completed may still be in flight, and its buffers belong to a
 * caller that has moved on, so the controller must not keep them. */
static bool nvme_controller_reset(struct o0_nvme *nd) {
    if (nd->queue_depth == 0u || nd->regs == NULL ||
        nd->admin_sq.virt == NULL || nd->admin_cq.virt == NULL ||
        nd->io_sq.virt == NULL || nd->io_cq.virt == NULL) {
        return false;
    }
    if (!nvme_disable_controller(nd)) {
        return false;
    }
    nvme_write32(nd, NVME_REG_INTMS, 0xFFFFFFFFu);
    nd->aq_head = 0u;
    nd->aq_tail = 0u;
    nd->aq_phase = 1u;
    nd->admin_cid = 0u;
    nd->io_head = 0u;
    nd->io_tail = 0u;
    nd->io_phase = 1u;
    nd->io_cid = 0u;
    memset(nd->admin_sq.virt, 0, nd->admin_sq.size);
    memset(nd->admin_cq.virt, 0, nd->admin_cq.size);
    memset(nd->io_sq.virt, 0, nd->io_sq.size);
    memset(nd->io_cq.virt, 0, nd->io_cq.size);
    uint32_t entries = (uint32_t)(nd->queue_depth - 1u);
    nvme_write32(nd, NVME_REG_AQA, entries | (entries << 16));
    nvme_write64(nd, NVME_REG_ASQ, nd->admin_sq.phys);
    nvme_write64(nd, NVME_REG_ACQ, nd->admin_cq.phys);
    return nvme_enable_controller(nd);
}

static o0_status nvme_restart(struct o0_nvme *nd) {
    if (!nvme_controller_reset(nd)) {
        return O0_EIO;
    }
    if (nd->io_ready) {
        o0_status st = nvme_io_queues_create(nd);
        if (st != O0_OK) {
            return st;
        }
    }
    if (nd->vector_ok) {
        nvme_write32(nd, NVME_REG_INTMC, 0xFFFFFFFFu);
    }
    return O0_OK;
}

static o0_status nvme_queue_rq(struct o0_block_queue *queue, struct o0_bio *bio) {
    if (queue == NULL || bio == NULL) {
        return O0_EINVAL;
    }
    struct o0_nvme *nd = (struct o0_nvme *)queue->driver_data;
    if (nd == NULL || !nd->in_use) {
        return O0_ENOENT;
    }

    if (bio->op == O0_BLOCK_FLUSH) {
        o0_block_complete_bio(bio, nvme_flush_command(nd), 0u);
        return O0_OK;
    }
    if (bio->op != O0_BLOCK_READ && bio->op != O0_BLOCK_WRITE) {
        return O0_ENOTSUP;
    }
    if (bio->nr_vecs != 1u || bio->vecs == NULL || bio->vecs[0].page == NULL) {
        return O0_EINVAL;
    }

    const struct o0_bio_vec *vec = &bio->vecs[0];
    if (nd->sector_size == 0u || vec->len == 0u ||
        (vec->len % nd->sector_size) != 0u) {
        return O0_EINVAL;
    }
    uint64_t nlb = vec->len / nd->sector_size;
    if (nlb > 0xFFFFu) {
        return O0_EINVAL;
    }
    uint8_t *buffer = (uint8_t *)vec->page + vec->offset;
    uint64_t phys = o0_virt_to_phys(buffer);
    if (phys == 0u) {
        return O0_EINVAL;
    }

    bool write = bio->op == O0_BLOCK_WRITE;
    o0_block_status st = nvme_io(nd, write, bio->lba, (uint16_t)nlb, phys);
    o0_block_complete_bio(bio, st, st == O0_BLOCK_OK ? vec->len : 0u);
    return O0_OK;
}

static o0_status nvme_reset(struct o0_block_dev *dev) {
    struct o0_nvme *nd = nvme_from_dev(dev);
    if (nd == NULL) {
        return O0_ENOENT;
    }
    return nvme_restart(nd);
}

static o0_status nvme_flush(struct o0_block_dev *dev) {
    struct o0_nvme *nd = nvme_from_dev(dev);
    if (nd == NULL) {
        return O0_ENOENT;
    }
    return nvme_flush_command(nd) == O0_BLOCK_OK ? O0_OK : O0_EIO;
}

static o0_status nvme_geometry(struct o0_block_dev *dev,
                               struct o0_block_geometry *out) {
    if (dev == NULL || out == NULL) {
        return O0_EINVAL;
    }
    *out = dev->geo;
    return O0_OK;
}

static bool nvme_media_changed(struct o0_block_dev *dev) {
    (void)dev;
    return false;
}

static o0_status nvme_open(struct o0_block_dev *dev, bool exclusive) {
    (void)exclusive;
    return dev != NULL ? O0_OK : O0_EINVAL;
}

static void nvme_close(struct o0_block_dev *dev) {
    (void)dev;
}

static const struct o0_block_ops nvme_block_ops = {
    .reset = nvme_reset,
    .flush = nvme_flush,
    .discard = NULL,
    .write_zeroes = NULL,
    .open = nvme_open,
    .close = nvme_close,
    .media_changed = nvme_media_changed,
    .get_geometry = nvme_geometry,
    .set_features = NULL
};

static const struct o0_block_queue_ops nvme_queue_ops = {
    .queue_rq = nvme_queue_rq,
    .poll = NULL,
    .timeout = NULL
};

static void nvme_set_name(char *dst, const char *src) {
    size_t i = 0;
    for (; i + 1u < O0_BLOCK_NAME_MAX && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* Programs the MSI-X table and enables the function. Entry 0 carries the admin
 * completion, entry 1 the I/O completion; both target one allocated vector,
 * because the driver polls and never needs them to be distinguishable. */
static bool nvme_msix_setup(struct o0_nvme *nd, uint64_t window) {
    const struct o0_pci_device *pci = &nd->pci;
    if (!pci->resources.msix_supported || pci->resources.msix_table_size < 2u) {
        return false;
    }
    uint8_t bir = pci->resources.msix_table_bir;
    if (bir >= pci->resources.bar_count) {
        return false;
    }
    struct o0_pci_bar bar;
    if (o0_pci_probe_bar(pci->bdf, bir, pci->resources.header_type, &bar) != 0) {
        return false;
    }
    if (!bar.valid || !bar.is_memory || bar.base == 0u) {
        return false;
    }
    uint64_t table_phys = bar.base + pci->resources.msix_table_offset;
    uint64_t page = table_phys & ~(uint64_t)(O0_PAGE_SIZE - 1u);
    size_t size = (size_t)((table_phys - page) + 2u * 16u);
    size = (size + O0_PAGE_SIZE - 1u) & ~(O0_PAGE_SIZE - 1u);
    o0_status st = o0_as_map(o0_as_kernel(), window, page, size,
                             O0_MAP_READ | O0_MAP_WRITE | O0_MAP_UC | O0_MAP_NX);
    if (st != O0_OK) {
        return false;
    }
    volatile void *table = (volatile void *)(uintptr_t)(window + (table_phys - page));

    o0_vector_t vector = O0_VECTOR_NONE;
    if (o0_irq_alloc_vectors(1u, &vector) != O0_OK) {
        return false;
    }
    if (o0_irq_register_vector(vector, nvme_irq, nd) != O0_OK) {
        o0_irq_free_vectors(vector, 1u);
        return false;
    }
    uint16_t entries = pci->resources.msix_table_size;
    if (entries > 2u) {
        entries = 2u;
    }
    if (o0_pci_msix_program(table, entries, 1u, o0_lapic_id(), vector, true) != 0 ||
        o0_pci_msix_enable(pci->bdf, pci->resources.msix_cap_offset, table, 0u,
                           o0_lapic_id(), vector) != 0) {
        (void)o0_irq_unregister_vector(vector, nvme_irq, nd);
        o0_irq_free_vectors(vector, 1u);
        return false;
    }
    /* The I/O entry was programmed masked so the table was never live with a
     * stale vector; unmask it now that the function mask is clear. */
    if (o0_pci_msix_program(table, entries, 1u, o0_lapic_id(), vector, false) != 0) {
        (void)o0_pci_msix_disable(pci->bdf, pci->resources.msix_cap_offset);
        (void)o0_irq_unregister_vector(vector, nvme_irq, nd);
        o0_irq_free_vectors(vector, 1u);
        return false;
    }
    nd->vector = vector;
    nd->vector_ok = true;
    return true;
}

o0_status o0_nvme_probe(const char *name, struct o0_block_dev **out) {
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
        if (pci->class_code == 0x01u && pci->subclass == 0x08u &&
            pci->prog_if == 0x02u) {
            found = pci;
            break;
        }
    }
    if (found == NULL) {
        return O0_ENOENT;
    }

    struct o0_nvme *nd = NULL;
    size_t slot = 0;
    for (size_t i = 0; i < NVME_MAX_DEVICES; ++i) {
        if (!nvme_devices[i].in_use) {
            nd = &nvme_devices[i];
            slot = i;
            break;
        }
    }
    if (nd == NULL) {
        return O0_EBUSY;
    }
    memset(nd, 0, sizeof(*nd));
    nd->pci = *found;
    nd->vector = O0_VECTOR_NONE;

    uint16_t cmd = 0;
    if (o0_pci_config_read16(found->bdf, 0x04u, &cmd) != 0) {
        return O0_EIO;
    }
    cmd = (uint16_t)(cmd | 0x0002u | 0x0004u);
    if (o0_pci_config_write16(found->bdf, 0x04u, cmd) != 0) {
        return O0_EIO;
    }

    o0_status st = nvme_map_bar0(found, NVME_BAR_WINDOW_BASE +
                                            (uint64_t)slot * NVME_BAR_WINDOW_STRIDE,
                                 &nd->regs);
    if (st != O0_OK) {
        return st;
    }

    uint64_t cap = nvme_read64(nd, NVME_REG_CAP);
    if (cap == 0u || cap == UINT64_MAX) {
        return O0_ENOTSUP;
    }
    uint32_t dstrd = (uint32_t)((cap >> 32) & 0xFu);
    nd->doorbell_stride = 4u << dstrd;
    uint32_t max_entries = (uint32_t)(cap & 0xFFFFu) + 1u;

    if (!nvme_disable_controller(nd)) {
        return O0_EIO;
    }

    st = nvme_dma_setup(nd);
    if (st != O0_OK) {
        return st;
    }

    /* Mask interrupts until the queue pairs and MSI-X exist. */
    nvme_write32(nd, NVME_REG_INTMS, 0xFFFFFFFFu);

    uint32_t qdepth = NVME_QDEPTH;
    if (qdepth > max_entries) {
        qdepth = max_entries;
    }
    if (qdepth < 2u) {
        nvme_dma_teardown(nd);
        return O0_ENOTSUP;
    }
    nd->queue_depth = (uint16_t)qdepth;
    uint32_t aqa = (qdepth - 1u) | ((qdepth - 1u) << 16);
    nvme_write32(nd, NVME_REG_AQA, aqa);
    nvme_write64(nd, NVME_REG_ASQ, nd->admin_sq.phys);
    nvme_write64(nd, NVME_REG_ACQ, nd->admin_cq.phys);
    if (!nvme_enable_controller(nd)) {
        nvme_dma_teardown(nd);
        return O0_EIO;
    }

    nd->nsid = 1u;
    if (nvme_identify_namespace(nd) != O0_OK) {
        nvme_dma_teardown(nd);
        return O0_ENOTSUP;
    }

    bool msix = nvme_msix_setup(nd, NVME_MSIX_WINDOW_BASE +
                                        (uint64_t)slot * NVME_MSIX_WINDOW_STRIDE);

    /* I/O completion queue (qid 1), then its submission queue. Both physically
     * contiguous and 4 KiB aligned. */
    st = nvme_io_queues_create(nd);
    if (st != O0_OK) {
        nvme_dma_teardown(nd);
        return st;
    }
    nd->io_ready = true;
    if (msix) {
        nvme_write32(nd, NVME_REG_INTMC, 0xFFFFFFFFu);
    }

    nd->in_use = true;
    nvme_set_name(nd->dev.name, name);
    nd->dev.geo.logical_block_size = nd->sector_size;
    nd->dev.geo.physical_block_size = nd->sector_size;
    nd->dev.geo.nr_sectors = nd->sectors;
    nd->dev.geo.max_hw_sectors = O0_PAGE_SIZE / nd->sector_size;
    nd->dev.geo.max_segments = 1;
    nd->dev.geo.max_segment_size = O0_PAGE_SIZE;
    nd->dev.geo.dma_alignment = nd->sector_size;
    nd->dev.geo.queue_depth = 1;
    nd->dev.geo.features = O0_BLOCK_F_DMA_COHERENT | O0_BLOCK_F_FLUSH;
    nd->dev.nr_queues = 1;
    nd->dev.queues = &nd->queue;
    nd->dev.ops = &nvme_block_ops;
    nd->dev.driver_data = nd;
    nd->queue.id = 0;
    nd->queue.depth = 1;
    nd->queue.ops = &nvme_queue_ops;
    nd->queue.driver_data = nd;

    st = o0_block_register(&nd->dev);
    if (st != O0_OK) {
        nd->in_use = false;
        if (nd->vector_ok) {
            (void)o0_irq_unregister_vector(nd->vector, nvme_irq, nd);
            o0_irq_free_vectors(nd->vector, 1u);
            nd->vector_ok = false;
        }
        nvme_dma_teardown(nd);
        return st;
    }

    *out = &nd->dev;
    return O0_OK;
}

uint64_t o0_nvme_irq_count(struct o0_block_dev *dev) {
    struct o0_nvme *nd = nvme_from_dev(dev);
    return nd != NULL ? nd->irq_count : 0u;
}

o0_status o0_nvme_inject_error(struct o0_block_dev *dev) {
    struct o0_nvme *nd = nvme_from_dev(dev);
    if (nd == NULL) {
        return O0_ENOENT;
    }
    if (nd->sectors < 2u || nd->sector_size == 0u) {
        return O0_ENOTSUP;
    }
    /* Two blocks starting on the last block: the second is out of range, so the
     * controller must complete the command with an error status. */
    uint32_t len = nd->sector_size * 2u;
    if (len > nd->io_data.size) {
        return O0_ENOMEM;
    }
    o0_block_status st = nvme_io(nd, false, nd->sectors - 1u, 2u,
                                 nd->io_data.phys);
    if (st == O0_BLOCK_OK) {
        return O0_EIO; /* the controller accepted an out-of-range read */
    }
    return O0_OK;
}
