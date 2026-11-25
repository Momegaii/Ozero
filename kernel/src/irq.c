/* Ozero kernel - interrupt dispatch and IRQ routing.
 *
 * Stage 3 routes the 16 legacy PIC lines only. Vector allocation is generic so
 * MSI/MSI-X and per-CPU vectors can be added without changing callers.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/irq.h"
#include "ozero/status.h"
#include "ozero/types.h"

#include "thread_internal.h"

#define O0_IRQ_LINES 16u
#define O0_VECTOR_FIRST 32u
#define O0_VECTOR_COUNT 208u /* 32..239 */

struct o0_irq_slot {
    o0_irq_handler_t handler;
    void *ctx;
    uint32_t flags;
    bool in_use;
};

static struct o0_irq_slot irq_table[O0_IRQ_LINES];
static uint64_t irq_counts[O0_IRQ_LINES];
static uint8_t vector_bitmap[O0_VECTOR_COUNT / 8u];
static bool dispatch_ready;

extern bool o0_pic_available(void);
extern o0_status o0_pic_mask(uint32_t line);
extern o0_status o0_pic_unmask(uint32_t line);
extern void o0_pic_eoi(uint32_t line);

struct x86_64_interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code, rip, cs, rflags;
};

extern void o0_serial_puts(const char *text);
extern void o0_serial_dec(uint64_t value);
extern void o0_serial_hex(uint64_t value);

void o0_irq_dispatch_init(void) {
    for (size_t i = 0; i < O0_IRQ_LINES; ++i) {
        irq_table[i].handler = NULL;
        irq_table[i].ctx = NULL;
        irq_table[i].flags = 0;
        irq_table[i].in_use = false;
        irq_counts[i] = 0;
    }
    for (size_t i = 0; i < sizeof(vector_bitmap); ++i) {
        vector_bitmap[i] = 0;
    }
    /* Vectors 0x40 (IPI) and 0x41 (LAPIC timer) have dedicated stubs and must
     * never be handed to a device through the generic allocator. */
    vector_bitmap[(0x40u - O0_VECTOR_FIRST) / 8u] |=
        (uint8_t)(1u << ((0x40u - O0_VECTOR_FIRST) % 8u));
    vector_bitmap[(0x41u - O0_VECTOR_FIRST) / 8u] |=
        (uint8_t)(1u << ((0x41u - O0_VECTOR_FIRST) % 8u));
    dispatch_ready = true;
}

/* Called from the assembly IRQ trampoline with interrupts disabled. */
void x86_irq_dispatch(struct x86_64_interrupt_frame *frame) {
    if (frame == NULL) {
        return;
    }
    uint64_t vector = frame->vector;
    if (vector < O0_VECTOR_FIRST || vector >= O0_VECTOR_FIRST + O0_IRQ_LINES) {
        if (dispatch_ready) {
            o0_serial_puts("OZERO: unexpected interrupt vector=");
            o0_serial_dec(vector);
            o0_serial_puts("\n");
        }
        return;
    }

    uint32_t line = (uint32_t)(vector - O0_VECTOR_FIRST);
    ++irq_counts[line];

    /* Acknowledge the controller before the handler runs so a slow handler
     * cannot lose the next interrupt. Spurious IRQ7/IRQ15 are not detected
     * yet; they are counted like any other line. */
    o0_pic_eoi(line);

    struct o0_irq_slot *slot = &irq_table[line];
    if (slot->in_use && slot->handler != NULL) {
        (void)slot->handler(line, slot->ctx);
    }

    if (line == 0) {
        o0_sched_tick();
    }
}

o0_status o0_irq_register(o0_gsi_t gsi, uint32_t flags, o0_irq_handler_t handler,
                          void *ctx) {
    if (handler == NULL) {
        return O0_EINVAL;
    }
    if (gsi >= O0_IRQ_LINES) {
        return O0_ENOSYS;
    }
    if (irq_table[gsi].in_use) {
        return O0_EBUSY;
    }
    irq_table[gsi].handler = handler;
    irq_table[gsi].ctx = ctx;
    irq_table[gsi].flags = flags;
    irq_table[gsi].in_use = true;
    return o0_pic_unmask(gsi);
}

o0_status o0_irq_unregister(o0_gsi_t gsi, o0_irq_handler_t handler, void *ctx) {
    if (gsi >= O0_IRQ_LINES) {
        return O0_ENOSYS;
    }
    if (!irq_table[gsi].in_use || irq_table[gsi].handler != handler ||
        irq_table[gsi].ctx != ctx) {
        return O0_ENOENT;
    }
    irq_table[gsi].handler = NULL;
    irq_table[gsi].ctx = NULL;
    irq_table[gsi].in_use = false;
    return o0_pic_mask(gsi);
}

o0_status o0_irq_mask(o0_gsi_t gsi) {
    if (gsi >= O0_IRQ_LINES || !o0_pic_available()) {
        return O0_ENOSYS;
    }
    return o0_pic_mask(gsi);
}

o0_status o0_irq_unmask(o0_gsi_t gsi) {
    if (gsi >= O0_IRQ_LINES || !o0_pic_available()) {
        return O0_ENOSYS;
    }
    return o0_pic_unmask(gsi);
}

uint64_t o0_irq_count(o0_gsi_t gsi) {
    return gsi < O0_IRQ_LINES ? irq_counts[gsi] : 0;
}

O0_NODISCARD o0_status o0_irq_alloc_vectors(uint32_t count, o0_vector_t *out_first) {
    if (out_first == NULL || count == 0 || count > O0_VECTOR_COUNT) {
        return O0_EINVAL;
    }

    for (uint32_t start = 0; start + count <= O0_VECTOR_COUNT; ++start) {
        bool free = true;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t bit = start + i;
            if ((vector_bitmap[bit / 8u] & (uint8_t)(1u << (bit % 8u))) != 0) {
                free = false;
                break;
            }
        }
        if (!free) {
            continue;
        }
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t bit = start + i;
            vector_bitmap[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
        }
        *out_first = O0_VECTOR_FIRST + start;
        return O0_OK;
    }
    return O0_ENOMEM;
}

void o0_irq_free_vectors(o0_vector_t first, uint32_t count) {
    if (first < O0_VECTOR_FIRST || count > O0_VECTOR_COUNT) {
        return;
    }
    uint32_t start = first - O0_VECTOR_FIRST;
    if (start + count > O0_VECTOR_COUNT) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t bit = start + i;
        vector_bitmap[bit / 8u] &= (uint8_t)~(1u << (bit % 8u));
    }
}

o0_irq_state_t o0_irq_save(void) {
    o0_irq_state_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void o0_irq_restore(o0_irq_state_t state) {
    if ((state & (1ul << 9)) != 0) {
        __asm__ volatile ("sti" : : : "memory");
    }
}

bool o0_irq_enabled(void) {
    o0_irq_state_t flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    return (flags & (1ul << 9)) != 0;
}
