/* Stage 6.2: bounded input event queue. */
#ifndef O0_INPUT_H
#define O0_INPUT_H
#include <stdbool.h>
#include <stdint.h>
#include "ozero/status.h"
#define O0_INPUT_CAPACITY 128u
enum o0_input_type { O0_INPUT_KEY=1, O0_INPUT_POINTER=2 };
struct o0_input_event {
    uint8_t type, pressed, buttons, reserved;
    uint16_t code;
    int16_t dx, dy;
    uint64_t timestamp;
};
struct o0_input_queue {
    struct o0_input_event events[O0_INPUT_CAPACITY];
    volatile uint32_t head, tail;
    volatile uint64_t dropped;
};
void o0_input_init(struct o0_input_queue *q);
o0_status o0_input_push(struct o0_input_queue *q, const struct o0_input_event *event);
bool o0_input_pop(struct o0_input_queue *q, struct o0_input_event *event);
#endif
