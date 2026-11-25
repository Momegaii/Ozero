/* Ozero kernel - COM1 serial console (internal interface). */
#ifndef O0_SERIAL_H
#define O0_SERIAL_H

#include <stdint.h>

void o0_serial_init(void);
void o0_serial_putc(char c);
void o0_serial_puts(const char *text);
void o0_serial_hex(uint64_t value);
void o0_serial_dec(uint64_t value);
void o0_serial_int(int64_t value);
void o0_serial_dec32(uint32_t value);

#endif /* O0_SERIAL_H */
