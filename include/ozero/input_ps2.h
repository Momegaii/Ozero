/* Stage 6.2: PS/2 keyboard and pointer driver for QEMU.
 *
 * Drives the i8042 controller present on the default QEMU machine (keyboard on
 * IRQ1, mouse on IRQ12). All port I/O and IRQ registration happen in kernel
 * context; the decode helpers are pure so they can be unit-tested on the host.
 *
 * Interrupt context never allocates, blocks, or renders: it only reads the
 * controller data port and publishes events into the bounded input queue.
 */
#ifndef O0_INPUT_PS2_H
#define O0_INPUT_PS2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ozero/input.h"
#include "ozero/status.h"

/* i8042 controller ports. */
#define O0_PS2_DATA_PORT 0x60u
#define O0_PS2_STATUS_PORT 0x64u

/* Controller commands. */
#define O0_PS2_CMD_DISABLE_KBD 0xAdu
#define O0_PS2_CMD_DISABLE_AUX 0xA7u
#define O0_PS2_CMD_ENABLE_KBD 0xAEu
#define O0_PS2_CMD_ENABLE_AUX 0xA8u
#define O0_PS2_CMD_WRITE_AUX 0xD4u   /* next byte to 0x60 goes to the mouse */
#define O0_PS2_CMD_SEL_SCAN_SET 0xF0u /* keyboard: select scan code set */
#define O0_PS2_CMD_MOUSE_ENABLE 0xF4u /* mouse: enable streaming */
#define O0_PS2_CMD_SELF_TEST 0xAAu    /* controller self-test */

/* Keyboard scan-code set 2 codes we track for modifier state. */
#define O0_PS2_SCAN_LSHIFT 0x12u
#define O0_PS2_SCAN_RSHIFT 0x59u
#define O0_PS2_SCAN_LCTRL  0x14u
#define O0_PS2_SCAN_RCTRL  0x5Au
#define O0_PS2_SCAN_LALT   0x11u
#define O0_PS2_SCAN_RALT   0x5Bu
#define O0_PS2_SCAN_CAPS   0x58u
#define O0_PS2_SCAN_NUMLK  0x45u

#define O0_PS2_BREAK  0xF0u /* break-code prefix (scan set 2) */
#define O0_PS2_E0     0xE0u /* extended-key prefix */

/* Modifier bitmask reported in o0_input_event.buttons for key events. */
#define O0_MOD_LSHIFT (1u << 0)
#define O0_MOD_RSHIFT (1u << 1)
#define O0_MOD_LCTRL  (1u << 2)
#define O0_MOD_RCTRL  (1u << 3)
#define O0_MOD_LALT   (1u << 4)
#define O0_MOD_RALT   (1u << 5)
#define O0_MOD_CAPS   (1u << 6)
#define O0_MOD_NUMLK  (1u << 7)

/* Initialize the PS/2 controller: disable devices, run self-test, enable the
 * keyboard (scan code set 2) and mouse (streaming mode), register IRQ1/IRQ12
 * handlers and unmask both lines. Safe to call once during boot. */
void o0_input_ps2_init(void);

/* Pop the next input event, if any. Returns false when the queue is empty. */
bool o0_input_poll(struct o0_input_event *event);

/* Number of events dropped because the queue was full. */
uint64_t o0_input_dropped(void);

/* --- Pure decode helpers (no port I/O), exposed for host testing. --- */

/* Feed one keyboard byte into the scan-code state machine. Returns true when a
 * complete key event has been decoded and written to out. */
bool o0_ps2_key_feed(uint8_t byte, struct o0_input_event *out);

/* Feed one mouse byte into the packet state machine. Returns true when a
 * complete 3-byte pointer packet has been decoded and written to out. */
bool o0_ps2_mouse_feed(uint8_t byte, struct o0_input_event *out);

#endif /* O0_INPUT_PS2_H */
