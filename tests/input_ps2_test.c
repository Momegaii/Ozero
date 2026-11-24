/* Stage 6.2: host unit tests for PS/2 scan-code and mouse-packet decoding.
 * These decode helpers are pure (no port I/O), so they run on the host.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ozero/input_ps2.h"
#include "ozero/irq.h"
#include "ozero/status.h"

/* Stubs for kernel-only functions referenced by input_ps2.c but never called
 * in these host tests (only the pure decode helpers and the queue are used). */
void o0_serial_puts(const char *text) { (void)text; }
o0_status o0_irq_register(o0_gsi_t gsi, uint32_t flags, o0_irq_handler_t handler, void *ctx) { (void)gsi; (void)flags; (void)handler; (void)ctx; return O0_OK; }
o0_status o0_irq_unmask(o0_gsi_t gsi) { (void)gsi; return O0_OK; }

static void test_key_make_break(void) {
    struct o0_input_event ev;

    /* Press scan 0x1C ('a'). */
    assert(o0_ps2_key_feed(0x1Cu, &ev) == true);
    assert(ev.type == O0_INPUT_KEY);
    assert(ev.pressed == 1u);
    assert(ev.code == 0x1Cu);
    assert(ev.dx == 0 && ev.dy == 0);

    /* Release scan 0x1C: break prefix 0xF0 then make code. */
    assert(o0_ps2_key_feed(0xF0u, &ev) == false);
    assert(o0_ps2_key_feed(0x1Cu, &ev) == true);
    assert(ev.type == O0_INPUT_KEY);
    assert(ev.pressed == 0u);
    assert(ev.code == 0x1Cu);

    printf("  TEST: key make/break ... PASS\n");
}

static void test_key_extended(void) {
    struct o0_input_event ev;

    /* E0 prefix then a non-modifier key (0x1C = enter). */
    assert(o0_ps2_key_feed(0xE0u, &ev) == false);
    assert(o0_ps2_key_feed(0x1Cu, &ev) == true);
    assert(ev.type == O0_INPUT_KEY);
    assert(ev.pressed == 1u);
    assert(ev.code == 0x1Cu);

    /* E0 break: 0xE0 0xF0 0x1C. */
    assert(o0_ps2_key_feed(0xE0u, &ev) == false);
    assert(o0_ps2_key_feed(0xF0u, &ev) == false);
    assert(o0_ps2_key_feed(0x1Cu, &ev) == true);
    assert(ev.pressed == 0u);
    assert(ev.code == 0x1Cu);

    printf("  TEST: key extended codes ... PASS\n");
}

static void test_key_modifiers(void) {
    struct o0_input_event ev;

    /* Press left shift: modifier state changes, no key event. ev.buttons
     * reports the post-update modifier state. */
    assert(o0_ps2_key_feed(O0_PS2_SCAN_LSHIFT, &ev) == false);
    assert((ev.buttons & O0_MOD_LSHIFT) != 0u);

    /* Press 'a' while shift held: event carries modifier bits. */
    assert(o0_ps2_key_feed(0x1Cu, &ev) == true);
    assert(ev.pressed == 1u);
    assert((ev.buttons & O0_MOD_LSHIFT) != 0u);

    /* Release shift. */
    assert(o0_ps2_key_feed(0xF0u, &ev) == false);
    assert(o0_ps2_key_feed(O0_PS2_SCAN_LSHIFT, &ev) == false);
    assert((ev.buttons & O0_MOD_LSHIFT) == 0u);

    printf("  TEST: key modifiers ... PASS\n");
}

static void test_key_toggle(void) {
    struct o0_input_event ev;

    /* Caps lock press toggles modifier state, no key event. */
    assert(o0_ps2_key_feed(O0_PS2_SCAN_CAPS, &ev) == false);
    assert((ev.buttons & O0_MOD_CAPS) != 0u);

    /* Press again: toggles off. */
    assert(o0_ps2_key_feed(O0_PS2_SCAN_CAPS, &ev) == false);
    assert((ev.buttons & O0_MOD_CAPS) == 0u);

    printf("  TEST: key toggle locks ... PASS\n");
}

static void test_mouse_packet(void) {
    struct o0_input_event ev;

    /* Packet: status=0x09 (left button + bit3), dx=10, dy=0. */
    assert(o0_ps2_mouse_feed(0x09u, &ev) == false);
    assert(o0_ps2_mouse_feed(0x0Au, &ev) == false);
    assert(o0_ps2_mouse_feed(0x00u, &ev) == true);
    assert(ev.type == O0_INPUT_POINTER);
    assert(ev.pressed == 0u);
    assert((ev.buttons & 0x01u) != 0u);   /* left button */
    assert(ev.dx == 10);
    assert(ev.dy == 0);

    /* Packet: status=0x1A (right button + x-sign + bit3), dx=-5, dy=0. */
    assert(o0_ps2_mouse_feed(0x1Au, &ev) == false);
    assert(o0_ps2_mouse_feed(0xFBu, &ev) == false); /* 0xFB sign-extended = -5 */
    assert(o0_ps2_mouse_feed(0x00u, &ev) == true);
    assert((ev.buttons & 0x02u) != 0u);   /* right button */
    assert(ev.dx == -5);
    assert(ev.dy == 0);

    /* Packet: status=0xCC (middle button + x-overflow + y-overflow + bit3). */
    assert(o0_ps2_mouse_feed(0xCCu, &ev) == false);
    assert(o0_ps2_mouse_feed(0x00u, &ev) == false);
    assert(o0_ps2_mouse_feed(0x00u, &ev) == true);
    assert((ev.buttons & 0x04u) != 0u);   /* middle button */

    printf("  TEST: mouse packet decoding ... PASS\n");
}

static void test_queue_overflow(void) {
    struct o0_input_event ev = {0};
    struct o0_input_queue q;
    o0_input_init(&q);

    /* Fill the queue to its effective capacity (CAPACITY-1 due to the
     * head+1==tail full condition). */
    for (uint32_t i = 0; i < O0_INPUT_CAPACITY - 1u; ++i) {
        ev.type = O0_INPUT_KEY;
        ev.code = (uint16_t)i;
        assert(o0_input_push(&q, &ev) == O0_OK);
    }

    /* Next push should fail and count as dropped. */
    assert(o0_input_push(&q, &ev) == O0_EBUSY);
    assert(q.dropped == 1u);

    /* Popping should free space. */
    assert(o0_input_pop(&q, &ev) == true);
    assert(ev.code == 0u);
    assert(o0_input_push(&q, &ev) == O0_OK);

    printf("  TEST: queue overflow ... PASS\n");
}

int main(void) {
    printf("Ozero PS/2 input host tests\n");
    test_key_make_break();
    test_key_extended();
    test_key_modifiers();
    test_key_toggle();
    test_mouse_packet();
    test_queue_overflow();
    printf("PS/2 input host tests: all passed\n");
    return 0;
}
