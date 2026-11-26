/* Ozero Stage 6.3 software surface implementation.
 * Pixel buffers are caller-owned; this module never allocates or maps memory.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include "ozero/surface.h"

static bool surface_valid(const struct o0_surface *s) {
    if (s == NULL || s->pixels == NULL || s->width == 0 || s->height == 0)
        return false;
    if (s->format != O0_PIXEL_RGBX8888 && s->format != O0_PIXEL_BGRX8888)
        return false;
    if (s->width > SIZE_MAX / 4u || s->pitch < s->width * 4u)
        return false;
    if (s->height > SIZE_MAX / s->pitch)
        return false;
    return true;
}

o0_status o0_surface_init(struct o0_surface *s, void *pixels,
                          size_t width, size_t height, size_t pitch,
                          enum o0_pixel_format format) {
    if (s == NULL || pixels == NULL || width == 0 || height == 0)
        return O0_EINVAL;
    if (format != O0_PIXEL_RGBX8888 && format != O0_PIXEL_BGRX8888)
        return O0_ENOTSUP;
    if (width > SIZE_MAX / 4u || pitch < width * 4u)
        return O0_ERANGE;
    if (pitch == 0 || height > SIZE_MAX / pitch)
        return O0_EOVERFLOW;
    s->pixels = (uint8_t *)pixels;
    s->width = width;
    s->height = height;
    s->pitch = pitch;
    s->format = format;
    return O0_OK;
}

static void pixel_write(uint8_t *p, enum o0_pixel_format format, uint32_t rgb) {
    uint8_t r = (uint8_t)(rgb >> 16);
    uint8_t g = (uint8_t)(rgb >> 8);
    uint8_t b = (uint8_t)rgb;
    if (format == O0_PIXEL_RGBX8888) {
        p[0] = r; p[1] = g; p[2] = b;
    } else {
        p[0] = b; p[1] = g; p[2] = r;
    }
    p[3] = 0xffu;
}

static uint32_t pixel_read(const uint8_t *p, enum o0_pixel_format format) {
    uint8_t r = format == O0_PIXEL_RGBX8888 ? p[0] : p[2];
    uint8_t g = p[1];
    uint8_t b = format == O0_PIXEL_RGBX8888 ? p[2] : p[0];
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

o0_status o0_surface_fill_rect(struct o0_surface *s, int32_t x, int32_t y,
                               int32_t width, int32_t height, uint32_t rgb) {
    if (!surface_valid(s) || width <= 0 || height <= 0)
        return O0_EINVAL;
    int64_t x0 = x, y0 = y;
    int64_t x1 = x0 + (int64_t)width;
    int64_t y1 = y0 + (int64_t)height;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int64_t)s->width) x1 = (int64_t)s->width;
    if (y1 > (int64_t)s->height) y1 = (int64_t)s->height;
    if (x0 >= x1 || y0 >= y1) return O0_OK;
    for (int64_t row = y0; row < y1; ++row) {
        uint8_t *p = s->pixels + (size_t)row * s->pitch + (size_t)x0 * 4u;
        for (int64_t col = x0; col < x1; ++col, p += 4)
            pixel_write(p, s->format, rgb);
    }
    return O0_OK;
}

o0_status o0_surface_fill(struct o0_surface *s, uint32_t rgb) {
    if (!surface_valid(s)) return O0_EINVAL;
    return o0_surface_fill_rect(s, 0, 0, (int32_t)(s->width > INT32_MAX ? INT32_MAX : s->width),
                               (int32_t)(s->height > INT32_MAX ? INT32_MAX : s->height), rgb);
}

o0_status o0_surface_blit(struct o0_surface *dst, int32_t dx, int32_t dy,
                          const struct o0_surface *src, int32_t sx, int32_t sy,
                          int32_t width, int32_t height) {
    if (!surface_valid(dst) || !surface_valid(src) || width <= 0 || height <= 0)
        return O0_EINVAL;
    int64_t x_dst = dx, y_dst = dy, x_src = sx, y_src = sy;
    int64_t w = width, h = height;
    if (x_dst < 0) { int64_t n = -x_dst; x_dst += n; x_src += n; w -= n; }
    if (y_dst < 0) { int64_t n = -y_dst; y_dst += n; y_src += n; h -= n; }
    if (x_src < 0) { int64_t n = -x_src; x_src += n; x_dst += n; w -= n; }
    if (y_src < 0) { int64_t n = -y_src; y_src += n; y_dst += n; h -= n; }
    if (x_dst + w > (int64_t)dst->width) w = (int64_t)dst->width - x_dst;
    if (y_dst + h > (int64_t)dst->height) h = (int64_t)dst->height - y_dst;
    if (x_src + w > (int64_t)src->width) w = (int64_t)src->width - x_src;
    if (y_src + h > (int64_t)src->height) h = (int64_t)src->height - y_src;
    if (w <= 0 || h <= 0) return O0_OK;

    bool reverse = false;
    if (dst->pixels == src->pixels) {
        size_t dst_off = (size_t)y_dst * dst->pitch + (size_t)x_dst * 4u;
        size_t src_off = (size_t)y_src * src->pitch + (size_t)x_src * 4u;
        reverse = dst_off > src_off;
    }
    for (int64_t yi = 0; yi < h; ++yi) {
        int64_t row = reverse ? h - 1 - yi : yi;
        for (int64_t xi = 0; xi < w; ++xi) {
            int64_t col = reverse ? w - 1 - xi : xi;
            const uint8_t *sp = src->pixels + (size_t)(y_src + row) * src->pitch +
                                (size_t)(x_src + col) * 4u;
            uint8_t *dp = dst->pixels + (size_t)(y_dst + row) * dst->pitch +
                          (size_t)(x_dst + col) * 4u;
            pixel_write(dp, dst->format, pixel_read(sp, src->format));
        }
    }
    return O0_OK;
}

uint32_t o0_surface_blend_rgb(uint32_t dst, uint32_t src, uint8_t alpha) {
    uint8_t dr = (uint8_t)(dst >> 16);
    uint8_t dg = (uint8_t)(dst >> 8);
    uint8_t db = (uint8_t)dst;
    uint8_t sr = (uint8_t)(src >> 16);
    uint8_t sg = (uint8_t)(src >> 8);
    uint8_t sb = (uint8_t)src;
    uint32_t r = ((uint32_t)sr * alpha + (uint32_t)dr * (255u - alpha)) / 255u;
    uint32_t g = ((uint32_t)sg * alpha + (uint32_t)dg * (255u - alpha)) / 255u;
    uint32_t b = ((uint32_t)sb * alpha + (uint32_t)db * (255u - alpha)) / 255u;
    return (r << 16) | (g << 8) | b;
}

o0_status o0_surface_put_pixel(struct o0_surface *s, int32_t x, int32_t y,
                               uint32_t rgb) {
    if (!surface_valid(s)) return O0_EINVAL;
    if (x < 0 || x >= (int32_t)s->width || y < 0 || y >= (int32_t)s->height)
        return O0_EINVAL;
    uint8_t *p = s->pixels + (size_t)y * s->pitch + (size_t)x * 4u;
    pixel_write(p, s->format, rgb);
    return O0_OK;
}

o0_status o0_surface_fill_circle(struct o0_surface *s, int32_t cx, int32_t cy,
                                 int32_t r, uint32_t rgb) {
    if (!surface_valid(s) || r <= 0) return O0_EINVAL;
    int32_t r2 = r * r;
    for (int32_t dy = -r; dy <= r; ++dy) {
        int32_t dy2 = dy * dy;
        if (dy2 > r2) continue;
        int32_t x_half = 0;
        /* Integer approximation of sqrt(r2 - dy2) via binary search. */
        int32_t lo = 0, hi = r;
        while (lo <= hi) {
            int32_t mid = (lo + hi) / 2;
            if (mid * mid + dy2 <= r2) {
                x_half = mid;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        int32_t x0 = cx - x_half;
        int32_t x1 = cx + x_half;
        int32_t py = cy + dy;
        if (py < 0 || py >= (int32_t)s->height) continue;
        if (x0 < 0) x0 = 0;
        if (x1 >= (int32_t)s->width) x1 = (int32_t)s->width - 1;
        if (x0 > x1) continue;
        uint8_t *p = s->pixels + (size_t)py * s->pitch + (size_t)x0 * 4u;
        for (int32_t x = x0; x <= x1; ++x, p += 4)
            pixel_write(p, s->format, rgb);
    }
    return O0_OK;
}

o0_status o0_surface_draw_circle(struct o0_surface *s, int32_t cx, int32_t cy,
                                 int32_t r, uint32_t rgb) {
    if (!surface_valid(s) || r <= 0) return O0_EINVAL;
    /* Midpoint circle algorithm, 8-way symmetric. */
    int32_t x = 0;
    int32_t y = r;
    int32_t d = 3 - 2 * r;
    while (x <= y) {
        int32_t pts[8][2] = {
            {cx + x, cy + y}, {cx - x, cy + y},
            {cx + x, cy - y}, {cx - x, cy - y},
            {cx + y, cy + x}, {cx - y, cy + x},
            {cx + y, cy - x}, {cx - y, cy - x},
        };
        for (int i = 0; i < 8; ++i) {
            int32_t px = pts[i][0], py = pts[i][1];
            if (px >= 0 && px < (int32_t)s->width && py >= 0 && py < (int32_t)s->height) {
                uint8_t *p = s->pixels + (size_t)py * s->pitch + (size_t)px * 4u;
                pixel_write(p, s->format, rgb);
            }
        }
        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            --y;
        }
        ++x;
    }
    return O0_OK;
}
