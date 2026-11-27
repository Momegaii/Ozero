/* Ozero kernel - slab allocator backing the kernel heap.
 *
 * Fixed size classes, one 4 KiB page per slab, free lists per class. Each
 * allocation carries a 16-byte header holding its size class, so o0_kfree does
 * not have to trust the caller's size argument.
 *
 * Limits of this stage: alignments above 16 bytes are rejected, freed pages are
 * never returned to the PMM, and there is no double-free detection.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/irq.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define O0_SLAB_CLASS_COUNT 8u
#define O0_SLAB_HEADER 16u
#define O0_SLAB_PAGE_ORDER 0u
#define O0_SLAB_MAGIC 0x534c4142u /* "SLAB" */

static const size_t slab_class_sizes[O0_SLAB_CLASS_COUNT] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096
};

struct slab_class {
    void *free_list;
    uint64_t object_size;
    uint64_t capacity;
    uint64_t live_objects;
    uint64_t total_objects;
    uint64_t slab_pages;
};

struct slab_header {
    uint32_t magic;
    uint32_t class_index;
    uint64_t reserved;
};

static struct slab_class slab_classes[O0_SLAB_CLASS_COUNT];
static bool slab_ready;

static void *slab_page_alloc(void) {
    o0_paddr_t phys = o0_pmm_alloc_pages(O0_SLAB_PAGE_ORDER);
    if (phys == 0) {
        return NULL;
    }
    void *virt = o0_phys_to_virt(phys);
    if (virt == NULL) {
        o0_pmm_free_pages(phys, O0_SLAB_PAGE_ORDER);
        return NULL;
    }
    return virt;
}

static bool slab_grow(uint32_t index) {
    struct slab_class *cls = &slab_classes[index];
    void *page = slab_page_alloc();
    if (page == NULL) {
        return false;
    }

    uint8_t *base = (uint8_t *)page;
    size_t stride = (size_t)cls->object_size;
    size_t count = O0_PAGE_SIZE / stride;

    for (size_t i = 0; i < count; ++i) {
        uint8_t *chunk = base + i * stride;
        *(void **)chunk = cls->free_list;
        cls->free_list = chunk;
    }
    cls->capacity += count;
    cls->total_objects += count;
    ++cls->slab_pages;
    return true;
}

void o0_slab_init(void) {
    for (size_t i = 0; i < O0_SLAB_CLASS_COUNT; ++i) {
        slab_classes[i].free_list = NULL;
        slab_classes[i].object_size = slab_class_sizes[i];
        slab_classes[i].capacity = 0;
        slab_classes[i].live_objects = 0;
        slab_classes[i].total_objects = 0;
        slab_classes[i].slab_pages = 0;
    }
    slab_ready = true;
}

bool o0_slab_ready(void) {
    return slab_ready;
}

/* Smallest class whose payload fits size and whose chunk alignment satisfies
 * align. Class chunks are at least 32-byte aligned; the payload starts 16 bytes
 * into a chunk, so payloads are 16-byte aligned. */
static int slab_select_class(size_t size, size_t align) {
    if (size == 0) {
        size = 1;
    }
    if (align == 0) {
        align = 1;
    }
    if (align > O0_SLAB_HEADER) {
        return -1;
    }
    for (size_t i = 0; i < O0_SLAB_CLASS_COUNT; ++i) {
        size_t payload = slab_class_sizes[i] - O0_SLAB_HEADER;
        if (payload >= size && align <= O0_SLAB_HEADER) {
            return (int)i;
        }
    }
    return -1;
}

O0_NODISCARD void *o0_kalloc(size_t size, size_t align) {
    if (!slab_ready) {
        return NULL;
    }
    int index = slab_select_class(size, align);
    if (index < 0) {
        return NULL;
    }

    o0_irq_state_t irq = o0_irq_save();
    struct slab_class *cls = &slab_classes[index];
    if (cls->free_list == NULL && !slab_grow((uint32_t)index)) {
        o0_irq_restore(irq);
        return NULL;
    }

    uint8_t *chunk = (uint8_t *)cls->free_list;
    cls->free_list = *(void **)chunk;
    ++cls->live_objects;
    o0_irq_restore(irq);

    struct slab_header *header = (struct slab_header *)chunk;
    header->magic = O0_SLAB_MAGIC;
    header->class_index = (uint32_t)index;
    header->reserved = 0;
    return chunk + O0_SLAB_HEADER;
}

void o0_kfree(void *ptr, size_t size) {
    (void)size;
    if (!slab_ready || ptr == NULL) {
        return;
    }
    uint8_t *chunk = (uint8_t *)ptr - O0_SLAB_HEADER;
    struct slab_header *header = (struct slab_header *)chunk;
    if (header->magic != O0_SLAB_MAGIC ||
        header->class_index >= O0_SLAB_CLASS_COUNT) {
        return;
    }

    o0_irq_state_t irq = o0_irq_save();
    struct slab_class *cls = &slab_classes[header->class_index];
    header->magic = 0;
    *(void **)chunk = cls->free_list;
    cls->free_list = chunk;
    if (cls->live_objects > 0) {
        --cls->live_objects;
    }
    o0_irq_restore(irq);
}

uint64_t o0_kheap_total_bytes(void) {
    uint64_t total = 0;
    for (size_t i = 0; i < O0_SLAB_CLASS_COUNT; ++i) {
        total += slab_classes[i].capacity * slab_classes[i].object_size;
    }
    return total;
}

uint64_t o0_kheap_live_objects(void) {
    uint64_t live = 0;
    for (size_t i = 0; i < O0_SLAB_CLASS_COUNT; ++i) {
        live += slab_classes[i].live_objects;
    }
    return live;
}

uint64_t o0_kheap_slab_count(void) {
    uint64_t slabs = 0;
    for (size_t i = 0; i < O0_SLAB_CLASS_COUNT; ++i) {
        slabs += slab_classes[i].slab_pages;
    }
    return slabs;
}
