/* Ozero kernel - legacy 8259 PIC and 8254 PIT.
 *
 * The PIC is remapped to vectors 32..47 so the CPU exception vectors stay
 * untouched. The PIT provides the scheduler tick. This is a bring-up timer: the
 * local APIC is the intended replacement once ACPI parsing lands.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/irq.h"
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
static uint32_t pit_hz;

o0_status o0_pic_init(void) {
    if (pic_ready) {
        return O0_EBUSY;
    }

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
