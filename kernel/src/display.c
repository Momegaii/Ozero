/* Stage 6.1: framebuffer metadata validation only; mapping/cache policy is separate. */
#include <stdint.h>
#include "ozero/display.h"

static uint64_t channel_mask(uint8_t shift, uint8_t size) {
    return ((1ull << size) - 1ull) << shift;
}

o0_status o0_display_mode_validate(const struct o0_display_mode *m) {
    if (!m || !m->address || !m->width || !m->height || !m->pitch)
        return O0_EINVAL;
    if (m->bpp != 24 && m->bpp != 32) return O0_ENOTSUP;
    if (m->memory_model != 1) return O0_ENOTSUP; /* Limine RGB */
    const uint8_t sizes[] = {m->red_size, m->green_size, m->blue_size};
    const uint8_t shifts[] = {m->red_shift, m->green_shift, m->blue_shift};
    uint64_t masks[3];
    for (unsigned i = 0; i < 3; ++i) {
        if (!sizes[i] || sizes[i] > 8 || shifts[i] >= m->bpp ||
            (unsigned)shifts[i] + sizes[i] > m->bpp)
            return O0_EINVAL;
        masks[i] = channel_mask(shifts[i], sizes[i]);
    }
    if ((masks[0] & masks[1]) || (masks[0] & masks[2]) ||
        (masks[1] & masks[2]))
        return O0_EINVAL;

    const uint64_t bytes_per_pixel = m->bpp / 8u;
    if (m->width > UINT64_MAX / bytes_per_pixel) return O0_EOVERFLOW;
    const uint64_t row_bytes = m->width * bytes_per_pixel;
    if (m->pitch < row_bytes) return O0_ERANGE;
    if (m->height > UINT64_MAX / m->pitch) return O0_EOVERFLOW;
    const uint64_t extent = m->pitch * m->height;
    if (m->bytes && extent > m->bytes) return O0_ERANGE;
    return O0_OK;
}
