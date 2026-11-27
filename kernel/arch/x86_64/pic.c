/* Ozero kernel - legacy 8259 PIC and 8254 PIT.
 *
 * The PIC is remapped to vectors 32..47 so the CPU exception vectors stay
 * untouched. The PIT provides the scheduler tick. The local APIC is not used as
 * a timer yet, but it must be configured as the 8259's virtual-wire receiver:
 * see lapic_extint_init below. This is a bring-up timer: the APIC timer is the
 * intended replacement once ACPI parsing lands.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/irq.h"
#include "ozero/lapic.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#include "io.h"

#define O0_PIC1_COMMAND 0x20
#define O0_PIC1_DATA 0x21
#define O0_PIC2_COMMAND 0xa0
#define O0_PIC2_DATA 0xa1

#define O0_ICW1_INIT 0x11
#define O0_ICW4_8086 0x01

#define O0_PIC_REMAP_BASE 0x20
#define O0_PIT_CHANNEL0 0x40
#define O0_PIT_COMMAND 0x43
#define O0_PIT_BASE_HZ 1193182u

static uint16_t pic1_mask = 0xffff;
static uint16_t pic2_mask = 0xffff;
static bool pic_ready;
static bool lapic_extint_ready;
static uint32_t pit_hz;

#define O0_IA32_APIC_BASE 0x1bu
#define O0_LAPIC_SVR 0xf0u
#define O0_LAPIC_TPR 0x80u
#define O0_LAPIC_LVT0 0x350u
#define O0_LAPIC_EOI 0xb0u
#define O0_LAPIC_ICR_LOW 0x300u
#define O0_LAPIC_ICR_HIGH 0x310u
#define O0_LAPIC_ID 0x20u
#define O0_LAPIC_LVT0_MASKED 0x00010000u
#define O0_LAPIC_ICR_ASSERT (1u << 14)
#define O0_LAPIC_ICR_DELIVERY_PENDING (1u << 12)
#define O0_LAPIC_LVT_TIMER 0x320u
#define O0_LAPIC_TIMER_INIT 0x380u
#define O0_LAPIC_TIMER_CURRENT 0x390u
#define O0_LAPIC_TIMER_DIVIDE 0x3e0u
#define O0_LAPIC_TIMER_VECTOR 0x41u
#define O0_LAPIC_TIMER_PERIODIC (1u << 17)
#define O0_LAPIC_TIMER_MASKED (1u << 16)
#define O0_LAPIC_TIMER_DIV_16 0x3u

/* Kernel virtual window reserved for the one-page LAPIC MMIO mapping. It sits
 * outside the HHDM window (0xffff800000000000 + 4 GiB), so it cannot alias RAM
 * or the framebuffer. */
#define O0_LAPIC_WINDOW 0xffffc00000000000ull
#define O0_LAPIC_WINDOW_SIZE 0x1000u

/* SVR: software-enable the APIC and give spurious interrupts vector 255.
 * LVT0: delivery mode ExtINT (111b) with the mask bit clear. ExtINT routes the
 * 8259's INTR output (the "virtual wire") to the CPU through LINT0. */
#define O0_LAPIC_SVR_VALUE 0x000001ffu
#define O0_LAPIC_LVT0_EXTINT 0x00000700u

static uint64_t read_msr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile ("wrmsr" : : "a"((uint32_t)value), "d"((uint32_t)(value >> 32)),
                      "c"(msr) : "memory");
}

static bool cpu_has_apic(void) {
    uint32_t eax, ebx, ecx, edx;
    eax = 1;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    (void)ebx;
    (void)ecx;
    return (edx & (1u << 9)) != 0;
}

/* Disables APIC interrupt delivery entirely. Used only when the ExtINT path is
 * unavailable; it is not a preferred configuration. */
static void disable_local_apic(void) {
    uint64_t base = read_msr(O0_IA32_APIC_BASE);
    write_msr(O0_IA32_APIC_BASE, base & ~(1ull << 11));
    lapic_extint_ready = false;
}

/* Programs the local APIC as the receiver for the legacy 8259 output.
 *
 * A CPU with the APIC globally disabled takes the 8259 INTR directly. Some
 * emulators (QEMU 8.2, shipped by Ubuntu 24.04) model the 8259 output as wired
 * only to LINT0 and then drop it while the APIC is disabled, so PIC-only
 * kernels lose every interrupt there while they work on real hardware and on
 * QEMU >= 9. Configuring the architectural virtual-wire path (APIC enabled,
 * LVT0 = ExtINT) works in all three cases and is the configuration Ozero keeps
 * until the SMP/APIC timer work in the hardening roadmap replaces the 8259.
 *
 * Device MMIO goes through an explicit uncached mapping; it is never touched
 * through the write-back direct map. */
static o0_status lapic_extint_init(void) {
    if (!cpu_has_apic()) {
        lapic_extint_ready = false;
        return O0_ENOSYS;
    }

    uint64_t base = read_msr(O0_IA32_APIC_BASE);
    o0_paddr_t phys = (o0_paddr_t)(base & 0x000ffffffffff000ull);
    if (phys == 0) {
        phys = 0xfee00000u; /* architectural default APIC base */
    }

    if (o0_as_map(o0_as_kernel(), O0_LAPIC_WINDOW, phys, O0_LAPIC_WINDOW_SIZE,
                  O0_MAP_READ | O0_MAP_WRITE | O0_MAP_UC | O0_MAP_NX) != O0_OK) {
        disable_local_apic();
        return O0_ENOMEM;
    }

    /* Force legacy xAPIC MMIO mode (clear x2APIC bit 10) and enable the unit. */
    write_msr(O0_IA32_APIC_BASE, (base & ~(1ull << 10)) | (1ull << 11));

    volatile uint32_t *lapic = (volatile uint32_t *)O0_LAPIC_WINDOW;
    lapic[O0_LAPIC_TPR / 4u] = 0;                 /* accept every priority */
    lapic[O0_LAPIC_SVR / 4u] = O0_LAPIC_SVR_VALUE;
    lapic[O0_LAPIC_LVT0 / 4u] = O0_LAPIC_LVT0_EXTINT;
    (void)lapic[O0_LAPIC_SVR / 4u];               /* flush posted writes */

    lapic_extint_ready = true;
    return O0_OK;
}

bool o0_pic_apic_extint_active(void) {
    return lapic_extint_ready;
}

bool o0_lapic_available(void) {
    return lapic_extint_ready;
}

uint32_t o0_lapic_id(void) {
    if (!lapic_extint_ready) {
        return UINT32_MAX;
    }
    volatile uint32_t *lapic = (volatile uint32_t *)O0_LAPIC_WINDOW;
    return lapic[O0_LAPIC_ID / 4u] >> 24;
}

o0_status o0_lapic_ap_init(void) {
    if (!lapic_extint_ready) {
        return O0_ENOSYS;
    }
    uint64_t base = read_msr(O0_IA32_APIC_BASE);
    write_msr(O0_IA32_APIC_BASE, (base & ~(1ull << 10)) | (1ull << 11));

    volatile uint32_t *lapic = (volatile uint32_t *)O0_LAPIC_WINDOW;
    lapic[O0_LAPIC_TPR / 4u] = 0;
    lapic[O0_LAPIC_SVR / 4u] = O0_LAPIC_SVR_VALUE;
    /* The legacy 8259 belongs to the BSP; APs must not accept ExtINT. */
    lapic[O0_LAPIC_LVT0 / 4u] = O0_LAPIC_LVT0_MASKED;
    (void)lapic[O0_LAPIC_SVR / 4u];
    return O0_OK;
}

o0_status o0_lapic_send_ipi(uint32_t apic_id, uint32_t vector) {
    if (!lapic_extint_ready || apic_id > 0xffu) {
        return O0_EINVAL;
    }
    volatile uint32_t *lapic = (volatile uint32_t *)O0_LAPIC_WINDOW;
    lapic[O0_LAPIC_ICR_HIGH / 4u] = apic_id << 24;
    lapic[O0_LAPIC_ICR_LOW / 4u] = vector | O0_LAPIC_ICR_ASSERT;
    for (uint32_t spin = 0; spin < 1000000u; ++spin) {
        if ((lapic[O0_LAPIC_ICR_LOW / 4u] & O0_LAPIC_ICR_DELIVERY_PENDING) == 0) {
            return O0_OK;
        }
        __asm__ volatile ("pause");
    }
    return O0_ETIMEDOUT;
}

void o0_lapic_eoi(void) {
    if (!lapic_extint_ready) {
        return;
    }
    volatile uint32_t *lapic = (volatile uint32_t *)O0_LAPIC_WINDOW;
    lapic[O0_LAPIC_EOI / 4u] = 0;
}

static void lapic_timer_program(uint32_t lvt, uint32_t initial_count) {
    volatile uint32_t *lapic = (volatile uint32_t *)O0_LAPIC_WINDOW;
    lapic[O0_LAPIC_TIMER_DIVIDE / 4u] = O0_LAPIC_TIMER_DIV_16;
    lapic[O0_LAPIC_LVT_TIMER / 4u] = lvt;
    lapic[O0_LAPIC_TIMER_INIT / 4u] = initial_count;
}

void o0_lapic_timer_start_oneshot(uint32_t initial_count) {
    if (lapic_extint_ready) {
        lapic_timer_program(O0_LAPIC_TIMER_VECTOR, initial_count);
    }
}

void o0_lapic_timer_start_periodic(uint32_t initial_count) {
    if (lapic_extint_ready) {
        lapic_timer_program(O0_LAPIC_TIMER_VECTOR | O0_LAPIC_TIMER_PERIODIC,
                            initial_count);
    }
}

uint32_t o0_lapic_timer_current(void) {
    if (!lapic_extint_ready) {
        return 0;
    }
    volatile uint32_t *lapic = (volatile uint32_t *)O0_LAPIC_WINDOW;
    return lapic[O0_LAPIC_TIMER_CURRENT / 4u];
}

void o0_lapic_timer_mask(void) {
    if (lapic_extint_ready) {
        volatile uint32_t *lapic = (volatile uint32_t *)O0_LAPIC_WINDOW;
        lapic[O0_LAPIC_LVT_TIMER / 4u] =
            O0_LAPIC_TIMER_VECTOR | O0_LAPIC_TIMER_MASKED;
    }
}

o0_status o0_pic_init(void) {
    if (pic_ready) {
        return O0_EBUSY;
    }

    /* Configure interrupt delivery before any 8259 line is unmasked. */
    (void)lapic_extint_init();

    /* Start the initialisation sequence and remap IRQ0..15 to 0x20..0x2f. */
    outb(O0_PIC1_COMMAND, O0_ICW1_INIT);
    io_wait();
    outb(O0_PIC2_COMMAND, O0_ICW1_INIT);
    io_wait();
    outb(O0_PIC1_DATA, O0_PIC_REMAP_BASE);
    io_wait();
    outb(O0_PIC2_DATA, O0_PIC_REMAP_BASE + 8);
    io_wait();
    outb(O0_PIC1_DATA, 0x04); /* slave on IRQ2 */
    io_wait();
    outb(O0_PIC2_DATA, 0x02);
    io_wait();
    outb(O0_PIC1_DATA, O0_ICW4_8086);
    io_wait();
    outb(O0_PIC2_DATA, O0_ICW4_8086);
    io_wait();

    /* Mask everything; handlers unmask what they own. */
    outb(O0_PIC1_DATA, 0xff);
    outb(O0_PIC2_DATA, 0xff);
    pic1_mask = 0xffff;
    pic2_mask = 0xffff;
    pic_ready = true;
    return O0_OK;
}

bool o0_pic_available(void) {
    return pic_ready;
}

o0_status o0_pic_mask(uint32_t line) {
    if (!pic_ready || line > 15) {
        return O0_EINVAL;
    }
    if (line < 8) {
        pic1_mask = (uint16_t)(pic1_mask | (1u << line));
        outb(O0_PIC1_DATA, (uint8_t)(pic1_mask & 0xff));
    } else {
        pic2_mask = (uint16_t)(pic2_mask | (1u << (line - 8)));
        outb(O0_PIC2_DATA, (uint8_t)(pic2_mask & 0xff));
    }
    return O0_OK;
}

o0_status o0_pic_unmask(uint32_t line) {
    if (!pic_ready || line > 15) {
        return O0_EINVAL;
    }
    if (line < 8) {
        pic1_mask = (uint16_t)(pic1_mask & ~(1u << line));
        outb(O0_PIC1_DATA, (uint8_t)(pic1_mask & 0xff));
    } else {
        pic2_mask = (uint16_t)(pic2_mask & ~(1u << (line - 8)));
        outb(O0_PIC2_DATA, (uint8_t)(pic2_mask & 0xff));
        /* IRQ2 is the cascade line; it must stay unmasked for slave IRQs. */
        pic1_mask = (uint16_t)(pic1_mask & ~(1u << 2));
        outb(O0_PIC1_DATA, (uint8_t)(pic1_mask & 0xff));
    }
    return O0_OK;
}

void o0_pic_eoi(uint32_t line) {
    if (!pic_ready) {
        return;
    }
    if (line >= 8) {
        outb(O0_PIC2_COMMAND, 0x20);
    }
    outb(O0_PIC1_COMMAND, 0x20);
}

/* Programs PIT channel 0 as a rate generator at the requested frequency. */
o0_status o0_pit_start(uint32_t hz) {
    if (hz == 0) {
        return O0_EINVAL;
    }
    uint32_t divisor = O0_PIT_BASE_HZ / hz;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xffffu) {
        divisor = 0xffffu;
    }

    outb(O0_PIT_COMMAND, 0x34); /* channel 0, lobyte/hibyte, mode 2, binary */
    outb(O0_PIT_CHANNEL0, (uint8_t)(divisor & 0xff));
    outb(O0_PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xff));
    pit_hz = O0_PIT_BASE_HZ / divisor;
    return O0_OK;
}

void o0_pit_stop(void) {
    outb(O0_PIT_COMMAND, 0x30);
    outb(O0_PIT_CHANNEL0, 0);
    outb(O0_PIT_CHANNEL0, 0);
    pit_hz = 0;
}

uint32_t o0_pit_hz(void) {
    return pit_hz;
}
