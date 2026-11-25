/* Ozero kernel - interrupts, vectors and interrupt-controller routing.
 *
 * GSI means a global system interrupt as described by ACPI MADT; a vector is a CPU IDT
 * vector. The two are not interchangeable.
 */
#ifndef O0_IRQ_H
#define O0_IRQ_H

#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t o0_gsi_t;
typedef uint32_t o0_vector_t;

#define O0_GSI_NONE UINT32_MAX
#define O0_VECTOR_NONE UINT32_MAX

/* Interrupt state for critical sections. Opaque so the width can change. */
typedef unsigned long o0_irq_state_t;

enum o0_irq_flags {
    O0_IRQ_TRIGGER_EDGE = 0u,
    O0_IRQ_TRIGGER_LEVEL = 1u << 0,
    O0_IRQ_POLARITY_HIGH = 0u,
    O0_IRQ_POLARITY_LOW = 1u << 1,
    O0_IRQ_SHARED = 1u << 2,
    O0_IRQ_PER_CPU = 1u << 3
};

/* Returns true if the interrupt belonged to this handler (relevant for shared lines). */
typedef bool (*o0_irq_handler_t)(o0_gsi_t gsi, void *ctx);

o0_status o0_irq_register(o0_gsi_t gsi, uint32_t flags, o0_irq_handler_t handler,
                          void *ctx);
o0_status o0_irq_unregister(o0_gsi_t gsi, o0_irq_handler_t handler, void *ctx);

o0_status o0_irq_mask(o0_gsi_t gsi);
o0_status o0_irq_unmask(o0_gsi_t gsi);

/* Contiguous vector allocation for MSI/MSI-X or per-CPU interrupts. The 16 legacy
 * line vectors and the IPI/timer stubs are never handed out. */
O0_NODISCARD o0_status o0_irq_alloc_vectors(uint32_t count, o0_vector_t *out_first);
void o0_irq_free_vectors(o0_vector_t first, uint32_t count);

/* Registers a handler for one allocated device vector (MSI/MSI-X). The GSI argument
 * passed to the handler is O0_GSI_NONE: the vector is not an I/O APIC line. The LAPIC
 * EOI is issued before the handler runs. */
O0_NODISCARD o0_status o0_irq_register_vector(o0_vector_t vector,
                                              o0_irq_handler_t handler, void *ctx);
o0_status o0_irq_unregister_vector(o0_vector_t vector,
                                   o0_irq_handler_t handler, void *ctx);
uint64_t o0_irq_vector_count(o0_vector_t vector);

/* Disables interrupts and returns the previous state. Nesting-safe. */
o0_irq_state_t o0_irq_save(void);
void o0_irq_restore(o0_irq_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* O0_IRQ_H */
