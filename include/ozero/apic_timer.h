/* Ozero kernel - BSP scheduler timer backed by the local APIC. */
#ifndef O0_APIC_TIMER_H
#define O0_APIC_TIMER_H

#include <stdint.h>

#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Calibrates the LAPIC timer against ten PIT ticks, then stops the PIT and
 * programs a periodic timer at `hz`. Returns O0_ENOSYS when no local APIC is
 * available; the caller then keeps the PIT. */
o0_status o0_apic_timer_start(uint32_t hz);

/* IDT vector 0x41 handler. */
void x86_apic_timer_dispatch(void *frame);

#ifdef __cplusplus
}
#endif

#endif /* O0_APIC_TIMER_H */
