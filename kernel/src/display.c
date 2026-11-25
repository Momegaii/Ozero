/* Stage 6.1: validated framebuffer and visible software fallback pattern. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ozero/display.h"
#include "ozero/mm.h"

static struct o0_display_mode active_mode;
static bool display_ready;

static uint64_t channel_mask(uint8_t shift, uint8_t size) {
    return ((1ull << size) - 1ull) << shift;
}

o0_status o0_display_mode_validate(const struct o0_display_mode *m) {
    if (!m || !m->address || !m->width || !m->height || !m->pitch)
        return O0_EINVAL;
    if (m->bpp != 24 && m->bpp != 32) return O0_ENOTSUP;
    if (m->memory_model != 1) return O0_ENOTSUP;
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

static uint32_t scale_channel(uint8_t value, uint8_t size, uint8_t shift) {
    const uint32_t maximum = (1u << size) - 1u;
    return (((uint32_t)value * maximum + 127u) / 255u) << shift;
}

static const uint8_t pattern_colors[8][3] = {
    {24, 32, 72}, {24, 92, 150}, {20, 150, 120}, {72, 160, 48},
    {210, 170, 32}, {210, 100, 32}, {160, 48, 72}, {220, 220, 220}
};

uint32_t o0_display_pattern_pixel(const struct o0_display_mode *mode,
                                  uint64_t x, uint64_t y) {
    if (mode == NULL || mode->width == 0 || x >= mode->width) return 0;
    const unsigned band = (unsigned)((x * 8u) / mode->width);
    const unsigned stripe = (unsigned)(((x / 24u) + (y / 24u)) & 1u);
    uint8_t r = pattern_colors[band][0];
    uint8_t g = pattern_colors[band][1];
    uint8_t b = pattern_colors[band][2];
    if (stripe) {
        r = (uint8_t)(r * 3u / 4u);
        g = (uint8_t)(g * 3u / 4u);
        b = (uint8_t)(b * 3u / 4u);
    }
    return scale_channel(r, mode->red_size, mode->red_shift) |
           scale_channel(g, mode->green_size, mode->green_shift) |
           scale_channel(b, mode->blue_size, mode->blue_shift);
}

uint32_t o0_display_bar_center_expected(const struct o0_display_mode *mode,
                                        uint32_t band) {
    if (mode == NULL || mode->width == 0 || mode->height == 0 || band >= 8u) {
        return 0;
    }
    const uint64_t x = (mode->width * (2u * (uint64_t)band + 1u)) / 16u;
    return o0_display_pattern_pixel(mode, x, mode->height / 2u);
}

static uint32_t checksum_pixels(const uint32_t pixels[8]) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 8u; ++i) {
        hash ^= pixels[i];
        hash *= 16777619u;
    }
    return hash;
}

uint32_t o0_display_expected_checksum(const struct o0_display_mode *mode) {
    uint32_t pixels[8];
    for (uint32_t i = 0; i < 8u; ++i) {
        pixels[i] = o0_display_bar_center_expected(mode, i);
    }
    return checksum_pixels(pixels);
}

static uint32_t read_pixel(const struct o0_display_mode *mode, uint64_t x,
                           uint64_t y) {
    const volatile uint8_t *row =
        (const volatile uint8_t *)mode->address + y * mode->pitch;
    const volatile uint8_t *p = row + x * (mode->bpp / 8u);
    uint32_t pixel = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16);
    if (mode->bpp == 32u) {
        pixel |= (uint32_t)p[3] << 24;
    }
    return pixel;
}

uint32_t o0_display_bar_center_actual(uint32_t band) {
    if (!display_ready || band >= 8u) return 0;
    const uint64_t x =
        (active_mode.width * (2u * (uint64_t)band + 1u)) / 16u;
    return read_pixel(&active_mode, x, active_mode.height / 2u);
}

uint32_t o0_display_actual_checksum(void) {
    if (!display_ready) return 0;
    uint32_t pixels[8];
    for (uint32_t i = 0; i < 8u; ++i) {
        pixels[i] = o0_display_bar_center_actual(i);
    }
    return checksum_pixels(pixels);
}

static void draw_boot_pattern(void) {
    volatile uint8_t *base = (volatile uint8_t *)active_mode.address;
    const uint32_t bytes_per_pixel = active_mode.bpp / 8u;
    for (uint64_t y = 0; y < active_mode.height; ++y) {
        volatile uint8_t *row = base + y * active_mode.pitch;
        for (uint64_t x = 0; x < active_mode.width; ++x) {
            const uint32_t pixel = o0_display_pattern_pixel(&active_mode, x, y);
            volatile uint8_t *dst = row + x * bytes_per_pixel;
            dst[0] = (uint8_t)pixel;
            dst[1] = (uint8_t)(pixel >> 8);
            dst[2] = (uint8_t)(pixel >> 16);
            if (bytes_per_pixel == 4u) dst[3] = (uint8_t)(pixel >> 24);
        }
    }
}

o0_status o0_display_init(const struct o0_display_mode *mode,
                          o0_paddr_t framebuffer_phys,
                          const struct o0_boot_info *bi) {
    if (mode == NULL || bi == NULL || bi->memmap == NULL) return O0_EINVAL;
    if (mode->height == 0 || mode->pitch > UINT64_MAX / mode->height)
        return O0_EOVERFLOW;
    const uint64_t extent = mode->pitch * mode->height;
    if (extent == 0 || extent > SIZE_MAX) return O0_ERANGE;
    if (framebuffer_phys > UINT64_MAX - extent) return O0_EOVERFLOW;

    bool covered = false;
    for (uint64_t i = 0; i < bi->memmap_count; ++i) {
        const struct o0_mem_region *r = &bi->memmap[i];
        if (r->type != O0_MEM_FRAMEBUFFER || r->length == 0 ||
            r->base > UINT64_MAX - r->length) continue;
        if (framebuffer_phys >= r->base &&
            framebuffer_phys + extent <= r->base + r->length) {
            covered = true;
            break;
        }
    }
    if (!covered) return O0_EPERM;
    if (!o0_hhdm_range_ok(framebuffer_phys, (size_t)extent)) return O0_ERANGE;
    void *mapped = o0_phys_to_virt(framebuffer_phys);
    if (mapped == NULL) return O0_ERANGE;

    active_mode = *mode;
    active_mode.address = mapped;
    active_mode.bytes = extent;
    o0_status status = o0_display_mode_validate(&active_mode);
    if (status != O0_OK) return status;

    display_ready = true;
    draw_boot_pattern();
    return O0_OK;
}

bool o0_display_ready(void) {
    return display_ready;
}
