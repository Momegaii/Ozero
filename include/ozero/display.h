/* Stage 6.1: validated boot framebuffer metadata and safe presentation. */
#ifndef O0_DISPLAY_H
#define O0_DISPLAY_H
#include <stdbool.h>
#include <stdint.h>
#include "ozero/boot.h"
#include "ozero/status.h"

struct o0_display_mode {
    volatile void *address;
    uint64_t width, height, pitch, bytes;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
};

o0_status o0_display_mode_validate(const struct o0_display_mode *mode);
/* Requires the physical extent to be covered by Limine framebuffer memory.
 * Paging maps framebuffer-containing direct-map chunks uncached. */
o0_status o0_display_init(const struct o0_display_mode *mode,
                          o0_paddr_t framebuffer_phys,
                          const struct o0_boot_info *boot_info);
bool o0_display_ready(void);

/* Deterministic boot-pattern pixel for a mode, without touching the framebuffer.
 * `band` is derived from x; this is the reference used by the host test and by
 * the kernel's sampled-pixel log marker. */
uint32_t o0_display_pattern_pixel(const struct o0_display_mode *mode,
                                  uint64_t x, uint64_t y);
uint32_t o0_display_bar_center_expected(const struct o0_display_mode *mode,
                                        uint32_t band);
uint32_t o0_display_expected_checksum(const struct o0_display_mode *mode);

/* Read back the centre of a bar from the live framebuffer, and checksum all
 * eight read-back samples. Both return 0 when no display is active. */
uint32_t o0_display_bar_center_actual(uint32_t band);
uint32_t o0_display_actual_checksum(void);
#endif
