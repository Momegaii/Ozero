/* Stage 6.1: metadata validation only; mapping/cache policy belongs to arch code. */
#include <stdint.h>
#include "ozero/display.h"
o0_status o0_display_mode_validate(const struct o0_display_mode *m) {
    if (!m || !m->address || !m->width || !m->height) return O0_EINVAL;
    if (m->bpp != 24 && m->bpp != 32) return O0_ENOTSUP;
    if (m->memory_model != 1) return O0_ENOTSUP; /* Limine RGB */
    uint64_t bytes_per_pixel = m->bpp / 8u;
    if (m->width > UINT64_MAX / bytes_per_pixel) return O0_EOVERFLOW;
    uint64_t row = m->width * bytes_per_pixel;
    if (m->pitch < row || m->height > UINT64_MAX / m->pitch) return O0_ERANGE;
    uint64_t extent = m->pitch * m->height;
    if (m->bytes && extent > m->bytes) return O0_ERANGE;
    if (!m->red_size || !m->green_size || !m->blue_size ||
        m->red_size > 8 || m->green_size > 8 || m->blue_size > 8 ||
        m->red_shift + m->red_size > m->bpp ||
        m->green_shift + m->green_size > m->bpp ||
        m->blue_shift + m->blue_size > m->bpp) return O0_EINVAL;
    return O0_OK;
}
