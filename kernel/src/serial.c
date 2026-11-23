/* Ozero kernel - COM1 serial console.
 *
 * Polled 16550-compatible output on the first legacy serial port. No receive
 * path and no interrupt-driven transmit yet: early bring-up only needs a
 * reliable synchronous log.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "serial.h"

#define COM1 0x3f8

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static bool serial_ready;

void o0_serial_init(void) {
    outb(COM1 + 1, 0x00); /* disable interrupts */
    outb(COM1 + 3, 0x80); /* enable DLAB */
    outb(COM1 + 0, 0x01); /* divisor low: 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xc7); /* FIFO enable, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0b); /* DTR, RTS, OUT2 */
    serial_ready = true;
}

static void serial_wait_tx(void) {
    for (uint32_t spin = 0; spin < 100000u; ++spin) {
        if ((inb(COM1 + 5) & 0x20) != 0) {
            return;
        }
    }
}

void o0_serial_putc(char c) {
    if (!serial_ready) {
        return;
    }
    if (c == '\n') {
        serial_wait_tx();
        outb(COM1, '\r');
    }
    serial_wait_tx();
    outb(COM1, (uint8_t)c);
}

void o0_serial_puts(const char *text) {
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        o0_serial_putc(*text++);
    }
}

void o0_serial_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    o0_serial_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        o0_serial_putc(digits[(value >> (unsigned)shift) & 0xfU]);
    }
}

void o0_serial_dec(uint64_t value) {
    char buffer[21];
    size_t count = 0;
    do {
        buffer[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0 && count < sizeof(buffer));
    while (count > 0) {
        o0_serial_putc(buffer[--count]);
    }
}

void o0_serial_dec32(uint32_t value) {
    o0_serial_dec((uint64_t)value);
}
