/* Stage 6.2: PS/2 keyboard and pointer driver for QEMU.
 *
 * Drives the i8042 controller on the default QEMU machine (keyboard on IRQ1,
 * mouse on IRQ12). The controller and both devices are brought up during boot;
 * IRQ handlers read the data port and publish decoded events into the bounded
 * input queue. Interrupt context performs no allocation, blocking, or rendering.
 *
 * Scan code set 2 is used for the keyboard; the mouse runs in stream mode with
 * 3-byte packets.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/irq.h"
#include "ozero/input_ps2.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#include "arch/x86_64/io.h"

extern void o0_serial_puts(const char *text);

/* Bounded input queue shared between the IRQ producer and the consumer. */
static struct o0_input_queue input_queue;

/* -------------------------------------------------------------------------- */
/* Keyboard scan-code set 2 state machine                                     */
/* -------------------------------------------------------------------------- */

typedef enum {
    KBD_NORMAL,           /* expecting a make or prefix byte */
    KBD_BREAK,            /* 0xF0 seen: next byte is the make code being released */
    KBD_E0_PREFIX,        /* 0xE0 seen: next byte is an extended make/prefix */
    KBD_E0_BREAK,         /* 0xE0 0xF0 seen: next byte is the extended make code released */
} kbd_state_t;

static kbd_state_t kbd_state = KBD_NORMAL;
static uint8_t kbd_modifiers = 0;   /* O0_MOD_* bitmask */
static bool kbd_caps_lock = false;
static bool kbd_num_lock = false;

static bool kbd_is_modifier(uint8_t scan, uint8_t *mod_bit) {
    switch (scan) {
    case O0_PS2_SCAN_LSHIFT: *mod_bit = O0_MOD_LSHIFT; return true;
    case O0_PS2_SCAN_RSHIFT: *mod_bit = O0_MOD_RSHIFT; return true;
    case O0_PS2_SCAN_LCTRL:  *mod_bit = O0_MOD_LCTRL;  return true;
    case O0_PS2_SCAN_RCTRL:  *mod_bit = O0_MOD_RCTRL;  return true;
    case O0_PS2_SCAN_LALT:   *mod_bit = O0_MOD_LALT;   return true;
    case O0_PS2_SCAN_RALT:   *mod_bit = O0_MOD_RALT;   return true;
    default: return false;
    }
}

static void kbd_update_modifier(uint8_t scan, bool pressed) {
    uint8_t bit = 0;
    if (!kbd_is_modifier(scan, &bit)) return;
    if (pressed) kbd_modifiers |= bit; else kbd_modifiers &= (uint8_t)~bit;
}

/* Returns true when a complete key event is in out. */
bool o0_ps2_key_feed(uint8_t byte, struct o0_input_event *out) {
    if (out == NULL) return false;

    out->type = O0_INPUT_KEY;
    out->pressed = 0;
    out->code = byte;
    out->dx = 0;
    out->dy = 0;

    /* Extended-key handling: prefix bytes do not produce events by themselves. */
    if (byte == O0_PS2_E0) {
        kbd_state = KBD_E0_PREFIX;
        return false;
    }

    bool is_break = false;
    if (kbd_state == KBD_BREAK || kbd_state == KBD_E0_BREAK) {
        is_break = true;
    }
    bool make = !is_break;

    uint8_t mod_bit = 0;
    if (kbd_is_modifier(byte, &mod_bit)) {
        kbd_update_modifier(byte, make);
        kbd_state = KBD_NORMAL;
        out->buttons = kbd_modifiers; /* post-update modifier state */
        return false;
    }

    /* Toggle keys. */
    if (byte == O0_PS2_SCAN_CAPS && make) {
        kbd_caps_lock = !kbd_caps_lock;
        if (kbd_caps_lock) kbd_modifiers |= O0_MOD_CAPS;
        else kbd_modifiers &= (uint8_t)~O0_MOD_CAPS;
        kbd_state = KBD_NORMAL;
        out->buttons = kbd_modifiers; /* post-update modifier state */
        return false;
    }
    if (byte == O0_PS2_SCAN_NUMLK && make) {
        kbd_num_lock = !kbd_num_lock;
        if (kbd_num_lock) kbd_modifiers |= O0_MOD_NUMLK;
        else kbd_modifiers &= (uint8_t)~O0_MOD_NUMLK;
        kbd_state = KBD_NORMAL;
        out->buttons = kbd_modifiers; /* post-update modifier state */
        return false;
    }

    out->pressed = make ? 1u : 0u;
    out->buttons = kbd_modifiers;
    out->code = (uint16_t)byte;

    /* Advance state machine. */
    if (byte == O0_PS2_BREAK) {
        kbd_state = (kbd_state == KBD_E0_PREFIX) ? KBD_E0_BREAK : KBD_BREAK;
        return false;
    }
    kbd_state = KBD_NORMAL;
    return true;
}

/* -------------------------------------------------------------------------- */
/* Mouse 3-byte packet state machine                                          */
/* -------------------------------------------------------------------------- */

typedef enum {
    MOUSE_STATUS,   /* expecting first byte (button flags + sign/overflow) */
    MOUSE_DX,       /* expecting second byte (x delta) */
    MOUSE_DY,       /* expecting third byte (y delta) */
} mouse_state_t;

static mouse_state_t mouse_state = MOUSE_STATUS;
static uint8_t mouse_status = 0;
static int16_t mouse_dx = 0;
static int16_t mouse_dy = 0;

#define MOUSE_BTN_LEFT   (1u << 0)
#define MOUSE_BTN_RIGHT  (1u << 1)
#define MOUSE_BTN_MIDDLE (1u << 2)

/* Returns true when a complete pointer event is in out. */
bool o0_ps2_mouse_feed(uint8_t byte, struct o0_input_event *out) {
    if (out == NULL) return false;

    switch (mouse_state) {
    case MOUSE_STATUS:
        mouse_status = byte;
        mouse_state = MOUSE_DX;
        return false;
    case MOUSE_DX: {
        uint8_t raw = byte;
        /* Bit 4 = X sign (1 = negative). */
        if (mouse_status & (1u << 4)) mouse_dx = (int16_t)(0xFF00u | raw);
        else mouse_dx = (int16_t)raw;
        mouse_state = MOUSE_DY;
        return false;
    }
    case MOUSE_DY: {
        uint8_t raw = byte;
        /* Bit 5 = Y sign (1 = negative). */
        if (mouse_status & (1u << 5)) mouse_dy = (int16_t)(0xFF00u | raw);
        else mouse_dy = (int16_t)raw;
        mouse_state = MOUSE_STATUS;

        out->type = O0_INPUT_POINTER;
        out->pressed = 0;
        out->buttons = 0;
        if (mouse_status & (1u << 0)) out->buttons |= MOUSE_BTN_LEFT;
        if (mouse_status & (1u << 1)) out->buttons |= MOUSE_BTN_RIGHT;
        if (mouse_status & (1u << 2)) out->buttons |= MOUSE_BTN_MIDDLE;
        out->code = 0;
        out->dx = mouse_dx;
        out->dy = mouse_dy;
        return true;
    }
    }
    return false;
}

/* -------------------------------------------------------------------------- */
/* IRQ handlers                                                               */
/* -------------------------------------------------------------------------- */

/* Called from the assembly IRQ trampoline with interrupts disabled. */
static bool ps2_kbd_irq(uint32_t gsi, void *ctx) {
    (void)gsi; (void)ctx;
    uint8_t byte = inb(O0_PS2_DATA_PORT);
    struct o0_input_event ev;
    if (o0_ps2_key_feed(byte, &ev)) {
        (void)o0_input_push(&input_queue, &ev);
    }
    return true;
}

static bool ps2_mouse_irq(uint32_t gsi, void *ctx) {
    (void)gsi; (void)ctx;
    uint8_t byte = inb(O0_PS2_DATA_PORT);
    struct o0_input_event ev;
    if (o0_ps2_mouse_feed(byte, &ev)) {
        (void)o0_input_push(&input_queue, &ev);
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/* Controller initialization                                                  */
/* -------------------------------------------------------------------------- */

/* Wait until the controller's output buffer is full (bit 0 of status). */
static bool ps2_wait_output(uint32_t tries) {
    while (tries-- > 0) {
        if ((inb(O0_PS2_STATUS_PORT) & 0x01u) != 0) return true;
    }
    return false;
}

/* Flush any stale bytes from the output buffer. */
static void ps2_flush_output(void) {
    while ((inb(O0_PS2_STATUS_PORT) & 0x01u) != 0) {
        (void)inb(O0_PS2_DATA_PORT);
    }
}

/* Write a command byte to the controller. Returns false on timeout. */
static bool ps2_write_cmd(uint8_t cmd) {
    for (uint32_t i = 0; i < 10000u; ++i) {
        if ((inb(O0_PS2_STATUS_PORT) & 0x02u) == 0) { /* input buffer empty */
            outb(O0_PS2_STATUS_PORT, cmd);
            return true;
        }
    }
    return false;
}

/* Write a data byte to port 0x60. Returns false on timeout. */
static bool ps2_write_data(uint8_t data) {
    for (uint32_t i = 0; i < 10000u; ++i) {
        if ((inb(O0_PS2_STATUS_PORT) & 0x02u) == 0) {
            outb(O0_PS2_DATA_PORT, data);
            return true;
        }
    }
    return false;
}

/* Read a data byte from port 0x60. Returns false on timeout. */
static bool ps2_read_data(uint8_t *out) {
    if (!ps2_wait_output(10000u)) return false;
    *out = inb(O0_PS2_DATA_PORT);
    return true;
}

/* Send a command to the auxiliary device (mouse) via 0xD4. */
static bool ps2_write_aux(uint8_t cmd) {
    if (!ps2_write_cmd(O0_PS2_CMD_WRITE_AUX)) return false;
    if (!ps2_write_data(cmd)) return false;
    uint8_t resp = 0;
    if (!ps2_read_data(&resp)) return false;
    return resp == 0xFAu; /* ACK */
}

void o0_input_ps2_init(void) {
    o0_input_init(&input_queue);

    /* Disable both devices while we reconfigure. */
    (void)ps2_write_cmd(O0_PS2_CMD_DISABLE_KBD);
    (void)ps2_write_cmd(O0_PS2_CMD_DISABLE_AUX);
    ps2_flush_output();

    /* Controller self-test. */
    if (!ps2_write_cmd(O0_PS2_CMD_SELF_TEST)) {
        o0_serial_puts("OZERO: input: controller self-test timeout\n");
    } else {
        uint8_t rc = 0;
        if (!ps2_read_data(&rc) || rc != 0x55u) {
            o0_serial_puts("OZERO: input: controller self-test failed\n");
        }
    }

    /* Enable keyboard. */
    (void)ps2_write_cmd(O0_PS2_CMD_ENABLE_KBD);

    /* Select scan code set 2 for the keyboard. */
    (void)ps2_write_aux(O0_PS2_CMD_SEL_SCAN_SET);
    (void)ps2_write_data(0x02u);

    /* Enable the mouse. */
    (void)ps2_write_cmd(O0_PS2_CMD_ENABLE_AUX);

    /* Enable mouse streaming mode. */
    (void)ps2_write_aux(O0_PS2_CMD_MOUSE_ENABLE);

    /* Register IRQ1 (keyboard) and IRQ12 (mouse). PIC vectors are 33 and 44. */
    if (o0_irq_register(1, O0_IRQ_TRIGGER_EDGE, ps2_kbd_irq, NULL) != O0_OK) {
        o0_serial_puts("OZERO: input: failed to register keyboard IRQ\n");
    }
    if (o0_irq_register(12, O0_IRQ_TRIGGER_EDGE, ps2_mouse_irq, NULL) != O0_OK) {
        o0_serial_puts("OZERO: input: failed to register mouse IRQ\n");
    }

    o0_irq_unmask(1);
    o0_irq_unmask(12);

    o0_serial_puts("OZERO: input: PS/2 keyboard+mouse initialized\n");
}

bool o0_input_poll(struct o0_input_event *event) {
    return o0_input_pop(&input_queue, event);
}

uint64_t o0_input_dropped(void) {
    return input_queue.dropped;
}

/* Desktop/router bridge: the IRQ producer owns this queue for the kernel lifetime. */
struct o0_input_queue *o0_input_queue_get(void) {
    return &input_queue;
}
