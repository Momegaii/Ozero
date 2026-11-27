/* Ozero kernel - local APIC timer calibration and dispatch.
 *
 * The PIT is used only as the calibration reference: ten PIT ticks at the
 * scheduler frequency are a known interval, and the LAPIC timer's decrement
 * over that interval gives the periodic reload value. The PIT is stopped after
 * a successful calibration. No invariant-TSC or TSC-deadline assumption is
 * made, and x2APIC is not used.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ozero/apic_timer.h"
#include "ozero/cpu.h"
#include "ozero/lapic.h"
#include "ozero/status.h"

#include "thread_internal.h"

#define O0_CALIBRATION_TICKS 10u

extern o0_status o0_pic_mask(uint32_t line);
extern void o0_pit_stop(void);

static uint32_t calibrated_period;

o0_status o0_apic_timer_start(uint32_t hz) {
    if (hz == 0 || !o0_lapic_available()) {
        return O0_ENOSYS;
    }

    uint64_t start = o0_sched_ticks();
    o0_lapic_timer_start_oneshot(0xffffffffu);

    __asm__ volatile ("sti" : : : "memory");
    uint64_t spin = 0;
    while (o0_sched_ticks() - start < O0_CALIBRATION_TICKS) {
        if (++spin > 500000000ull) {
            __asm__ volatile ("cli" : : : "memory");
            o0_lapic_timer_mask();
            return O0_ETIMEDOUT;
        }
        o0_cpu_relax();
    }
    __asm__ volatile ("cli" : : : "memory");

    uint32_t remaining = o0_lapic_timer_current();
    uint32_t elapsed = 0xffffffffu - remaining;
    if (elapsed < 1000u) {
        o0_lapic_timer_mask();
        return O0_EIO;
    }

    uint32_t per_tick = elapsed / O0_CALIBRATION_TICKS;
    if (per_tick < 1000u) {
        per_tick = 1000u;
    }

    o0_pit_stop();
    (void)o0_pic_mask(0);
    calibrated_period = per_tick;
    o0_lapic_timer_start_periodic(per_tick);
    return O0_OK;
}

o0_status o0_apic_timer_start_ap(void) {
    if (!o0_lapic_available() || calibrated_period == 0u) {
        return O0_ENOSYS;
    }
    o0_lapic_timer_start_periodic(calibrated_period);
    return O0_OK;
}

void x86_apic_timer_dispatch(void *frame) {
    (void)frame;
    o0_lapic_eoi();
    o0_sched_tick();
}
