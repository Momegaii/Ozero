#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ozero/compositor.h"
#include "ozero/window.h"
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

static void test_basic_compose(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    /* Create target surface */
    uint32_t *target_buf = alloc_surface_buf(200, 200);
    struct o0_surface target = make_surface(target_buf, 200, 200);

    /* Create window surface */
    uint32_t *win_buf = alloc_surface_buf(50, 50);
    struct o0_surface win = make_surface(win_buf, 50, 50);
    o0_surface_fill(&win, 0xFF0000); /* Red */

    o0_window_id id;
    o0_status st = o0_wm_create(&wm, &win, 10, 10, &id);
    assert(st == O0_OK);

    /* Compose with blue background */
    st = o0_compositor_compose(&target, &wm, 0x0000FF);
    assert(st == O0_OK);

    /* Check background */
    assert(rgb_at(&target, 0, 0) == 0x0000FF);
    assert(rgb_at(&target, 199, 199) == 0x0000FF);

    /* Check window area */
    assert(rgb_at(&target, 10, 10) == 0x00FF00); /* focused border */
    assert(rgb_at(&target, 11, 11) == 0xFF0000);
    assert(rgb_at(&target, 59, 59) == 0x00FF00); /* focused border */
    assert(rgb_at(&target, 58, 58) == 0xFF0000);

    free(target_buf);
    free(win_buf);
    puts("compositor_test: basic_compose passed");
}

static void test_overlapping_windows(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    uint32_t *target_buf = alloc_surface_buf(200, 200);
    struct o0_surface target = make_surface(target_buf, 200, 200);

    /* Bottom window - green */
    uint32_t *buf1 = alloc_surface_buf(100, 100);
    struct o0_surface s1 = make_surface(buf1, 100, 100);
    o0_surface_fill(&s1, 0x00FF00);
    o0_window_id id1;
    o0_status st = o0_wm_create(&wm, &s1, 10, 10, &id1);
    assert(st == O0_OK);

    /* Top window - red, overlapping */
    uint32_t *buf2 = alloc_surface_buf(100, 100);
    struct o0_surface s2 = make_surface(buf2, 100, 100);
    o0_surface_fill(&s2, 0xFF0000);
    o0_window_id id2;
    st = o0_wm_create(&wm, &s2, 50, 50, &id2);
    assert(st == O0_OK);

    /* Compose with blue background */
    st = o0_compositor_compose(&target, &wm, 0x0000FF);
    assert(st == O0_OK);

    /* Bottom window only area (not overlapped) */
    assert(rgb_at(&target, 10, 10) == 0x00FF00);
    assert(rgb_at(&target, 49, 49) == 0x00FF00);

    /* Top window area */
    assert(rgb_at(&target, 50, 50) == 0x00FF00); /* focused top-left border */
    assert(rgb_at(&target, 52, 52) == 0xFF0000);
    assert(rgb_at(&target, 149, 149) == 0x00FF00); /* focused bottom-right border */
    assert(rgb_at(&target, 147, 147) == 0xFF0000);

    /* Background area */
    assert(rgb_at(&target, 0, 0) == 0x0000FF);
    assert(rgb_at(&target, 199, 199) == 0x0000FF);

    free(target_buf);
    free(buf1);
    free(buf2);
    puts("compositor_test: overlapping_windows passed");
}

static void test_clipping(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    uint32_t *target_buf = alloc_surface_buf(100, 100);
    struct o0_surface target = make_surface(target_buf, 100, 100);

    /* Window larger than target, partially off-screen */
    uint32_t *buf = alloc_surface_buf(200, 200);
    struct o0_surface win = make_surface(buf, 200, 200);
    o0_surface_fill(&win, 0xFF00FF); /* Magenta */

    o0_window_id id;
    o0_status st = o0_wm_create(&wm, &win, -50, -50, &id);
    assert(st == O0_OK);

    st = o0_compositor_compose(&target, &wm, 0x000000);
    assert(st == O0_OK);

    /* Only the overlapping portion should be visible */
    assert(rgb_at(&target, 0, 0) == 0x00FF00); /* clipped focus border */
    assert(rgb_at(&target, 99, 99) == 0xFF00FF);

    /* Move window completely off-screen */
    st = o0_wm_move(&wm, id, 200, 200);
    assert(st == O0_OK);

    st = o0_compositor_compose(&target, &wm, 0x0000FF);
    assert(st == O0_OK);

    /* Should see only background */
    assert(rgb_at(&target, 0, 0) == 0x0000FF);
    assert(rgb_at(&target, 99, 99) == 0x0000FF);

    /* Move back partially on-screen */
    st = o0_wm_move(&wm, id, 50, 50);
    assert(st == O0_OK);

    st = o0_compositor_compose(&target, &wm, 0x000000);
    assert(st == O0_OK);

    /* Focus border is visible; interior remains magenta. */
    assert(rgb_at(&target, 50, 50) == 0x00FF00);
    assert(rgb_at(&target, 52, 52) == 0xFF00FF);
    /* Top-left should be background */
    assert(rgb_at(&target, 0, 0) == 0x000000);

    free(target_buf);
    free(buf);
    puts("compositor_test: clipping passed");
}

static void test_off_screen_windows(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    uint32_t *target_buf = alloc_surface_buf(100, 100);
    struct o0_surface target = make_surface(target_buf, 100, 100);

    /* Window completely off-screen to the left */
    uint32_t *buf1 = alloc_surface_buf(50, 50);
    struct o0_surface s1 = make_surface(buf1, 50, 50);
    o0_surface_fill(&s1, 0xFFFF00); /* Yellow */
    o0_window_id id1;
    o0_status st = o0_wm_create(&wm, &s1, -100, 0, &id1);
    assert(st == O0_OK);

    /* Window completely off-screen to the top */
    uint32_t *buf2 = alloc_surface_buf(50, 50);
    struct o0_surface s2 = make_surface(buf2, 50, 50);
    o0_surface_fill(&s2, 0x00FFFF); /* Cyan */
    o0_window_id id2;
    st = o0_wm_create(&wm, &s2, 0, -100, &id2);
    assert(st == O0_OK);

    /* Window completely off-screen to the right */
    uint32_t *buf3 = alloc_surface_buf(50, 50);
    struct o0_surface s3 = make_surface(buf3, 50, 50);
    o0_surface_fill(&s3, 0xFFFFFF); /* White */
    o0_window_id id3;
    st = o0_wm_create(&wm, &s3, 200, 0, &id3);
    assert(st == O0_OK);

    /* Window completely off-screen to the bottom */
    uint32_t *buf4 = alloc_surface_buf(50, 50);
    struct o0_surface s4 = make_surface(buf4, 50, 50);
    o0_surface_fill(&s4, 0x808080); /* Gray */
    o0_window_id id4;
    st = o0_wm_create(&wm, &s4, 0, 200, &id4);
    assert(st == O0_OK);

    st = o0_compositor_compose(&target, &wm, 0x000000);
    assert(st == O0_OK);

    /* Should see only background since all windows are off-screen */
    assert(rgb_at(&target, 0, 0) == 0x000000);
    assert(rgb_at(&target, 99, 99) == 0x000000);

    free(target_buf);
    free(buf1);
    free(buf2);
    free(buf3);
    free(buf4);
    puts("compositor_test: off_screen_windows passed");
}

static void test_invisible_windows(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    uint32_t *target_buf = alloc_surface_buf(100, 100);
    struct o0_surface target = make_surface(target_buf, 100, 100);

    uint32_t *buf = alloc_surface_buf(50, 50);
    struct o0_surface win = make_surface(buf, 50, 50);
    o0_surface_fill(&win, 0xFF0000);

    o0_window_id id;
    o0_status st = o0_wm_create(&wm, &win, 10, 10, &id);
    assert(st == O0_OK);

    /* Make window invisible */
    wm.windows[0].visible = false;

    st = o0_compositor_compose(&target, &wm, 0x0000FF);
    assert(st == O0_OK);

    /* Should see only background */
    assert(rgb_at(&target, 10, 10) == 0x0000FF);
    assert(rgb_at(&target, 59, 59) == 0x0000FF);

    /* Make visible again */
    wm.windows[0].visible = true;
    st = o0_compositor_compose(&target, &wm, 0x0000FF);
    assert(st == O0_OK);

    /* Window returns with its focus border. */
    assert(rgb_at(&target, 10, 10) == 0x00FF00);
    assert(rgb_at(&target, 12, 12) == 0xFF0000);
    assert(rgb_at(&target, 59, 59) == 0x00FF00);
    assert(rgb_at(&target, 57, 57) == 0xFF0000);

    free(target_buf);
    free(buf);
    puts("compositor_test: invisible_windows passed");
}

static void test_null_args(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    uint32_t *target_buf = alloc_surface_buf(100, 100);
    struct o0_surface target = make_surface(target_buf, 100, 100);

    o0_status st = o0_compositor_compose(NULL, &wm, 0);
    assert(st == O0_EINVAL);

    st = o0_compositor_compose(&target, NULL, 0);
    assert(st == O0_EINVAL);

    free(target_buf);
    puts("compositor_test: null_args passed");
}

static void test_empty_window_manager(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    uint32_t *target_buf = alloc_surface_buf(100, 100);
    struct o0_surface target = make_surface(target_buf, 100, 100);

    o0_status st = o0_compositor_compose(&target, &wm, 0x123456);
    assert(st == O0_OK);

    /* Should be filled with background color */
    assert(rgb_at(&target, 0, 0) == 0x123456);
    assert(rgb_at(&target, 99, 99) == 0x123456);

    free(target_buf);
    puts("compositor_test: empty_window_manager passed");
}

int main(void) {
    test_basic_compose();
    test_overlapping_windows();
    test_clipping();
    test_off_screen_windows();
    test_invisible_windows();
    test_null_args();
    test_empty_window_manager();
    puts("compositor_test: all checks passed");
    return 0;
}