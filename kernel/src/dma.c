/* Ozero kernel - DMA buffer allocator (Wave 4.1).
 *
 * One o0_dma_buffer is one physically contiguous run of pages. There is no IOMMU
 * at this stage, so the physical address handed to the device is the address the
 * CPU uses through the direct map. That is only safe for trusted in-kernel
 * drivers, as include/ozero/mm.h states.
 *
 * Ownership: a caller that hands the buffer to a device pins it with
 * o0_dma_pin(). o0_dma_free() then refuses (O0_EBUSY) until every pin has been
 * released, so a request that is still in flight can never have its memory
 * returned to the page allocator. See ADR 0014.
 *
 * Constraint support, stated honestly:
 *   - addr_mask is enforced by checking the allocated physical range. When the
 *     PMM cannot satisfy a low address mask the allocation fails with O0_ENOMEM;
 *     there is no bounce-buffer fallback yet.
 *   - alignment is met by allocating a block large enough to be naturally
 *     aligned to it; the unused tail stays owned by the buffer.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/irq.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define O0_DMA_MAX_BUFFERS 64u
#define O0_DMA_MAX_ORDER 20u /* 4 GiB; far beyond the boot contract's needs */

struct o0_dma_record {
    o0_paddr_t phys;
    size_t size;
    uint32_t order;
    uint32_t pins;
    bool in_use;
};

static struct o0_dma_record dma_records[O0_DMA_MAX_BUFFERS];

static bool dma_power_of_two(uint32_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

/* Contiguous low-bit mask: 0, 0x1, 0x3, 0x7, ... or all ones. */
static bool dma_mask_ok(uint64_t mask) {
    return (mask & (mask + 1u)) == 0u;
}

static struct o0_dma_record *dma_find(o0_paddr_t phys) {
    for (size_t i = 0; i < O0_DMA_MAX_BUFFERS; ++i) {
        if (dma_records[i].in_use && dma_records[i].phys == phys) {
            return &dma_records[i];
        }
    }
    return NULL;
}

static uint32_t dma_order_for(size_t pages, uint32_t alignment) {
    uint32_t order = 0;
    while ((UINT64_C(1) << order) < (uint64_t)pages) {
        ++order;
    }
    if (alignment != 0u) {
        uint64_t align_pages = ((uint64_t)alignment + O0_PAGE_SIZE - 1u) >> O0_PAGE_SHIFT;
        while ((UINT64_C(1) << order) < align_pages) {
            ++order;
        }
    }
    return order;
}

o0_status o0_dma_alloc(const struct o0_dma_constraint *constraint,
                       size_t size, struct o0_dma_buffer *out) {
    if (constraint == NULL || out == NULL || size == 0u) {
        return O0_EINVAL;
    }
    if (!dma_mask_ok(constraint->addr_mask)) {
        return O0_EINVAL;
    }
    if (constraint->alignment != 0u && !dma_power_of_two(constraint->alignment)) {
        return O0_EINVAL;
    }
    if (constraint->max_segment != 0u && size > constraint->max_segment) {
        return O0_ENOTSUP;
    }
    if (size > (size_t)(UINT64_C(1) << (O0_DMA_MAX_ORDER + O0_PAGE_SHIFT))) {
        return O0_ENOMEM;
    }

    size_t pages = (size + O0_PAGE_SIZE - 1u) >> O0_PAGE_SHIFT;
    uint32_t order = dma_order_for(pages, constraint->alignment);
    if (order > O0_DMA_MAX_ORDER) {
        return O0_ENOMEM;
    }
    size_t span = (size_t)UINT64_C(1) << order << O0_PAGE_SHIFT;

    o0_irq_state_t irq = o0_irq_save();
    struct o0_dma_record *record = NULL;
    for (size_t i = 0; i < O0_DMA_MAX_BUFFERS; ++i) {
        if (!dma_records[i].in_use) {
            record = &dma_records[i];
            break;
        }
    }
    if (record == NULL) {
        o0_irq_restore(irq);
        return O0_ENOMEM;
    }

    o0_paddr_t phys = o0_pmm_alloc_pages(order);
    if (phys == 0u) {
        o0_irq_restore(irq);
        return O0_ENOMEM;
    }
    if ((phys & ~constraint->addr_mask) != 0u) {
        o0_pmm_free_pages(phys, order);
        o0_irq_restore(irq);
        return O0_ENOMEM;
    }

    record->phys = phys;
    record->size = span;
    record->order = order;
    record->pins = 0;
    record->in_use = true;
    o0_irq_restore(irq);

    out->phys = phys;
    out->virt = o0_phys_to_virt(phys);
    out->size = span;
    out->flags = constraint->coherent ? O0_DMA_F_COHERENT : 0u;
    out->reserved = 0u;
    return O0_OK;
}

o0_status o0_dma_pin(struct o0_dma_buffer *buf) {
    if (buf == NULL || buf->phys == 0u) {
        return O0_EINVAL;
    }
    o0_irq_state_t irq = o0_irq_save();
    struct o0_dma_record *record = dma_find(buf->phys);
    if (record == NULL) {
        o0_irq_restore(irq);
        return O0_ENOENT;
    }
    if (record->pins == UINT32_MAX) {
        o0_irq_restore(irq);
        return O0_EOVERFLOW;
    }
    ++record->pins;
    buf->flags |= O0_DMA_F_PINNED;
    o0_irq_restore(irq);
    return O0_OK;
}

o0_status o0_dma_unpin(struct o0_dma_buffer *buf) {
    if (buf == NULL || buf->phys == 0u) {
        return O0_EINVAL;
    }
    o0_irq_state_t irq = o0_irq_save();
    struct o0_dma_record *record = dma_find(buf->phys);
    if (record == NULL || record->pins == 0u) {
        o0_irq_restore(irq);
        return O0_EINVAL;
    }
    --record->pins;
    if (record->pins == 0u) {
        buf->flags &= ~O0_DMA_F_PINNED;
    }
    o0_irq_restore(irq);
    return O0_OK;
}

o0_status o0_dma_sync(struct o0_dma_buffer *buf, size_t offset, size_t len,
                      enum o0_dma_dir dir) {
    if (buf == NULL || buf->phys == 0u) {
        return O0_EINVAL;
    }
    if (dir != O0_DMA_TO_DEVICE && dir != O0_DMA_FROM_DEVICE &&
        dir != O0_DMA_BIDIRECTIONAL) {
        return O0_EINVAL;
    }
    if (offset > buf->size || len > buf->size - offset) {
        return O0_ERANGE;
    }
    /* x86-64 keeps DMA coherent with CPU caches (no non-coherent PCI agents are
     * assumed at this stage). The fence still orders CPU stores and loads
     * against the subsequent device doorbell write. */
    __asm__ volatile ("mfence" : : : "memory");
    return O0_OK;
}

o0_status o0_dma_free(struct o0_dma_buffer *buf) {
    if (buf == NULL || buf->phys == 0u) {
        return O0_EINVAL;
    }
    o0_irq_state_t irq = o0_irq_save();
    struct o0_dma_record *record = dma_find(buf->phys);
    if (record == NULL) {
        o0_irq_restore(irq);
        return O0_ENOENT; /* unknown or already freed */
    }
    if (record->pins != 0u) {
        o0_irq_restore(irq);
        return O0_EBUSY; /* a request still owns the memory */
    }
    o0_paddr_t phys = record->phys;
    uint32_t order = record->order;
    record->in_use = false;
    record->phys = 0u;
    record->size = 0u;
    record->order = 0u;
    record->pins = 0u;
    o0_irq_restore(irq);

    o0_pmm_free_pages(phys, order);
    buf->phys = 0u;
    buf->virt = NULL;
    buf->size = 0u;
    buf->flags = 0u;
    return O0_OK;
}

size_t o0_dma_live_buffers(void) {
    size_t count = 0;
    o0_irq_state_t irq = o0_irq_save();
    for (size_t i = 0; i < O0_DMA_MAX_BUFFERS; ++i) {
        if (dma_records[i].in_use) {
            ++count;
        }
    }
    o0_irq_restore(irq);
    return count;
}
