/* Ozero kernel - local APIC operations used by the SMP foundation.
 *
 * The BSP maps the architectural LAPIC MMIO page once in xAPIC mode. APs reuse
 * that mapping through the shared kernel half. x2APIC is not assumed: the
 * driver forces MMIO mode, matching ADR 0005's ExtINT setup.
 */
#ifndef O0_LAPIC_H
#define O0_LAPIC_H

#include <stdbool.h>
#include <stdint.h>

#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_LAPIC_IPI_VECTOR 0x40u

bool o0_lapic_available(void);
uint32_t o0_lapic_id(void);

/* Configures an AP's local APIC after it has loaded the kernel page tables. */
o0_status o0_lapic_ap_init(void);

/* Sends a fixed-delivery IPI to one LAPIC ID. Returns O0_ETIMEDOUT if the
 * delivery-status bit never clears. */
o0_status o0_lapic_send_ipi(uint32_t apic_id, uint32_t vector);

/* Signals end-of-interrupt on the current CPU. */
void o0_lapic_eoi(void);

/* Local APIC timer, one-shot and periodic, using divisor 16. */
void o0_lapic_timer_start_oneshot(uint32_t initial_count);
void o0_lapic_timer_start_periodic(uint32_t initial_count);
uint32_t o0_lapic_timer_current(void);
void o0_lapic_timer_mask(void);

#ifdef __cplusplus
}
#endif

#endif /* O0_LAPIC_H */
