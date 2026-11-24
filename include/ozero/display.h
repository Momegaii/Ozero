/* Stage 6.1: validated boot framebuffer metadata. */
#ifndef O0_DISPLAY_H
#define O0_DISPLAY_H
#include <stdbool.h>
#include <stdint.h>
#include "ozero/status.h"
struct o0_display_mode {
    volatile void *address;
    uint64_t width, height, pitch, bytes;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
};
o0_status o0_display_mode_validate(const struct o0_display_mode *mode);
#endif
