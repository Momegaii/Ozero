#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ozero/gfx_backend.h"
#include "ozero/surface.h"
#include "ozero/status.h"

static uint32_t *alloc_surface_buf(uint32_t width, uint32_t height) {
    uint32_t *buf = (uint32_t *)calloc(width * height, sizeof(uint32_t));
    assert(buf != NULL);
    return buf;
}

static struct o0_surface make_surface(uint32_t *buf, uint32_t w, uint32_t h) {
    struct o0_surface s;
    o0_status st = o0_surface_init(&s, buf, w, h, w * 4, O0_PIXEL_RGBX8888);
    assert(st == O0_OK);
    return s;
}

static uint32_t rgb_at(const struct o0_surface *s, size_t x, size_t y) {
    const uint8_t *p = s->pixels + y * s->pitch + x * 4u;
    uint8_t r = s->format == O0_PIXEL_RGBX8888 ? p[0] : p[2];
    uint8_t g = p[1];
    uint8_t b = s->format == O0_PIXEL_RGBX8888 ? p[2] : p[0];
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void test_software_backend_fill(void) {
    const struct o0_gfx_backend *backend = o0_gfx_software_backend();
    assert(backend != NULL);
    assert(strcmp(backend->name, "software") == 0);
    assert(backend->features == (O0_GFX_FEATURE_FILL | O0_GFX_FEATURE_BLIT));
    assert(backend->hardware == false);

    uint32_t *buf = alloc_surface_buf(100, 100);
    struct o0_surface s = make_surface(buf, 100, 100);

    o0_status st = backend->fill(&s, 0x123456);
    assert(st == O0_OK);
    assert(rgb_at(&s, 0, 0) == 0x123456);
    assert(rgb_at(&s, 99, 99) == 0x123456);

    free(buf);
    puts("gfx_backend_test: software_backend_fill passed");
}

static void test_software_backend_blit(void) {
    const struct o0_gfx_backend *backend = o0_gfx_software_backend();

    uint32_t *src_buf = alloc_surface_buf(50, 50);
    struct o0_surface src = make_surface(src_buf, 50, 50);
    o0_status st = backend->fill(&src, 0xFF0000);
    assert(st == O0_OK);

    uint32_t *dst_buf = alloc_surface_buf(100, 100);
    struct o0_surface dst = make_surface(dst_buf, 100, 100);
    st = backend->fill(&dst, 0x0000FF);
    assert(st == O0_OK);

    /* Blit source to destination */
    st = backend->blit(&dst, 10, 10, &src, 0, 0, 50, 50);
    assert(st == O0_OK);

    /* Check blitted area */
    assert(rgb_at(&dst, 10, 10) == 0xFF0000);
    assert(rgb_at(&dst, 59, 59) == 0xFF0000);

    /* Check unblitted area */
    assert(rgb_at(&dst, 0, 0) == 0x0000FF);
    assert(rgb_at(&dst, 99, 99) == 0x0000FF);

    free(src_buf);
    free(dst_buf);
    puts("gfx_backend_test: software_backend_blit passed");
}

static void test_software_backend_blit_clipping(void) {
    const struct o0_gfx_backend *backend = o0_gfx_software_backend();

    uint32_t *src_buf = alloc_surface_buf(100, 100);
    struct o0_surface src = make_surface(src_buf, 100, 100);
    o0_status st = backend->fill(&src, 0x00FF00);
    assert(st == O0_OK);

    uint32_t *dst_buf = alloc_surface_buf(100, 100);
    struct o0_surface dst = make_surface(dst_buf, 100, 100);
    st = backend->fill(&dst, 0x000000);
    assert(st == O0_OK);

    /* Blit with negative destination - should clip */
    st = backend->blit(&dst, -50, -50, &src, 0, 0, 100, 100);
    assert(st == O0_OK);

    /* Only bottom-right 50x50 should be copied */
    assert(rgb_at(&dst, 0, 0) == 0x00FF00);
    assert(rgb_at(&dst, 49, 49) == 0x00FF00);

    free(src_buf);
    free(dst_buf);
    puts("gfx_backend_test: software_backend_blit_clipping passed");
}

static void test_validate_backend(void) {
    /* Valid software backend */
    const struct o0_gfx_backend *sw = o0_gfx_software_backend();
    o0_status st = o0_gfx_backend_validate(sw);
    assert(st == O0_OK);

    /* NULL backend */
    st = o0_gfx_backend_validate(NULL);
    assert(st == O0_EINVAL);

    /* Missing name */
    struct o0_gfx_backend bad1 = {0};
    bad1.fill = sw->fill;
    bad1.blit = sw->blit;
    st = o0_gfx_backend_validate(&bad1);
    assert(st == O0_EINVAL);

    /* Missing fill */
    struct o0_gfx_backend bad2 = *sw;
    bad2.fill = NULL;
    st = o0_gfx_backend_validate(&bad2);
    assert(st == O0_EINVAL);

    /* Missing blit */
    struct o0_gfx_backend bad3 = *sw;
    bad3.blit = NULL;
    st = o0_gfx_backend_validate(&bad3);
    assert(st == O0_EINVAL);

    /* Hardware backend without accelerated feature */
    struct o0_gfx_backend bad4 = *sw;
    bad4.hardware = true;
    bad4.features = O0_GFX_FEATURE_FILL | O0_GFX_FEATURE_BLIT;
    st = o0_gfx_backend_validate(&bad4);
    assert(st == O0_EINVAL);

    /* Hardware backend with accelerated feature - valid */
    struct o0_gfx_backend good = *sw;
    good.hardware = true;
    good.features = O0_GFX_FEATURE_FILL | O0_GFX_FEATURE_BLIT | O0_GFX_FEATURE_ACCELERATED;
    st = o0_gfx_backend_validate(&good);
    assert(st == O0_OK);

    puts("gfx_backend_test: validate_backend passed");
}

static void test_feature_flags(void) {
    const struct o0_gfx_backend *backend = o0_gfx_software_backend();

    /* Check feature flags are correct */
    assert(backend->features & O0_GFX_FEATURE_FILL);
    assert(backend->features & O0_GFX_FEATURE_BLIT);
    assert(!(backend->features & O0_GFX_FEATURE_ACCELERATED));

    puts("gfx_backend_test: feature_flags passed");
}

static void test_same_buffer_blit(void) {
    const struct o0_gfx_backend *backend = o0_gfx_software_backend();

    uint32_t *buf = alloc_surface_buf(100, 100);
    struct o0_surface s = make_surface(buf, 100, 100);
    o0_status st = backend->fill(&s, 0x000000);
    assert(st == O0_OK);

    /* Fill left half with red */
    st = backend->fill(&s, 0xFF0000);
    assert(st == O0_OK);

    /* Fill right half with blue using fill_rect directly */
    st = o0_surface_fill_rect(&s, 50, 0, 50, 100, 0x0000FF);
    assert(st == O0_OK);

    /* Blit left half to right half (overlapping) - should behave like memmove */
    st = backend->blit(&s, 50, 0, &s, 0, 0, 50, 100);
    assert(st == O0_OK);

    /* Right half should now be red */
    assert(rgb_at(&s, 50, 0) == 0xFF0000);
    assert(rgb_at(&s, 99, 99) == 0xFF0000);

    free(buf);
    puts("gfx_backend_test: same_buffer_blit passed");
}

int main(void) {
    test_software_backend_fill();
    test_software_backend_blit();
    test_software_backend_blit_clipping();
    test_validate_backend();
    test_feature_flags();
    test_same_buffer_blit();
    puts("gfx_backend_test: all checks passed");
    return 0;
}