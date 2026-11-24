/* Ozero Stage 6.3 software surfaces and 32-bit pixel primitives. */
#ifndef O0_SURFACE_H
#define O0_SURFACE_H

#include <stddef.h>
#include <stdint.h>
#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

enum o0_pixel_format {
    O0_PIXEL_RGBX8888 = 1, /* bytes in memory: R, G, B, X */
    O0_PIXEL_BGRX8888 = 2  /* bytes in memory: B, G, R, X */
};

struct o0_surface {
    uint8_t *pixels;
    size_t width;
    size_t height;
    size_t pitch;
    enum o0_pixel_format format;
};

/* A surface is a non-owning view. The caller retains and frees the pixel buffer. */
o0_status o0_surface_init(struct o0_surface *surface, void *pixels,
                          size_t width, size_t height, size_t pitch,
                          enum o0_pixel_format format);
o0_status o0_surface_fill(struct o0_surface *surface, uint32_t rgb);
o0_status o0_surface_fill_rect(struct o0_surface *surface, int32_t x, int32_t y,
                               int32_t width, int32_t height, uint32_t rgb);
o0_status o0_surface_blit(struct o0_surface *destination, int32_t dx, int32_t dy,
                          const struct o0_surface *source, int32_t sx, int32_t sy,
                          int32_t width, int32_t height);

#ifdef __cplusplus
}
#endif
#endif
