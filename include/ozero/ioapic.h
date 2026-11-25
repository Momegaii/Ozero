/* Ozero kernel - I/O APIC redirection.
 *
 * The MADT describes one or more I/O APICs and the interrupt-source overrides
 * that map ISA IRQs onto GSIs. This driver programs the redirection table; it
 * does not own device policy. The legacy 8259 is masked once the I/O APIC is
 * active so an interrupt cannot be delivered twice.
 */
#ifndef O0_IOAPIC_H
#define O0_IOAPIC_H

#include <stdbool.h>
#include <stdint.h>

#include "ozero/acpi.h"
#include "ozero/irq.h"
#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_IOAPIC_MAX_PINS 24u
#define O0_IOAPIC_RTE_MASKED (1u << 16)
#define O0_IOAPIC_RTE_LEVEL (1u << 15)
#define O0_IOAPIC_RTE_LOW_ACTIVE (1u << 13)
#define O0_IOAPIC_RTE_LOGICAL (1u << 11)

/* Encodes one redirection-table entry. Pure so host tests can assert the
 * polarity/trigger/mask/destination bits without touching MMIO. */
static inline uint64_t o0_ioapic_encode_rte(uint32_t vector, uint32_t apic_id,
                                            uint32_t flags, bool masked) {
    uint64_t rte = (uint64_t)(vector & 0xffu);
    if ((flags & O0_IRQ_TRIGGER_LEVEL) != 0) {
        rte |= O0_IOAPIC_RTE_LEVEL;
    }
    if ((flags & O0_IRQ_POLARITY_LOW) != 0) {
        rte |= O0_IOAPIC_RTE_LOW_ACTIVE;
    }
    if (masked) {
        rte |= O0_IOAPIC_RTE_MASKED;
    }
    rte |= (uint64_t)(apic_id & 0xffu) << 56;
    return rte;
}

/* Resolves the GSI and effective trigger/polarity for an ISA IRQ. Overrides
 * from the MADT win; without one the identity mapping and ISA defaults apply. */
static inline uint32_t o0_ioapic_gsi_for_isa(const struct o0_acpi_info *info,
                                             uint32_t isa_irq,
                                             uint32_t *out_flags) {
    uint32_t flags = O0_IRQ_TRIGGER_EDGE | O0_IRQ_POLARITY_HIGH;
    uint32_t gsi = isa_irq;
    if (info != NULL) {
        for (uint32_t i = 0; i < info->override_count; ++i) {
            const struct o0_acpi_override *ov = &info->overrides[i];
            if (ov->bus == 0u && ov->source == isa_irq) {
                gsi = ov->gsi;
                if ((ov->flags & 0x3u) == 0x3u) {
                    flags |= O0_IRQ_POLARITY_LOW;
                }
                if (((ov->flags >> 2) & 0x3u) == 0x3u) {
                    flags |= O0_IRQ_TRIGGER_LEVEL;
                }
                break;
            }
        }
    }
    if (out_flags != NULL) {
        *out_flags = flags;
    }
    return gsi;
}

bool o0_ioapic_available(void);
o0_status o0_ioapic_init(void);
o0_status o0_ioapic_route_isa(uint32_t isa_irq, uint32_t vector, uint32_t flags,
                              bool masked);
o0_status o0_ioapic_set_mask(uint32_t isa_irq, bool masked);
uint32_t o0_ioapic_gsi(uint32_t isa_irq);
uint32_t o0_ioapic_pin_count(void);

#ifdef __cplusplus
}
#endif

#endif /* O0_IOAPIC_H */
