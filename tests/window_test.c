#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void test_create_destroy(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    uint32_t *buf = alloc_surface_buf(100, 100);
    struct o0_surface s = make_surface(buf, 100, 100);

    o0_window_id id;
    o0_status st = o0_wm_create(&wm, &s, 10, 10, &id);
    assert(st == O0_OK);
    assert(id == 1);
    assert(wm.count == 1);
    assert(wm.windows[0].id == 1);
    assert(wm.windows[0].x == 10);
    assert(wm.windows[0].y == 10);
    assert(wm.windows[0].width == 100);
    assert(wm.windows[0].height == 100);
    assert(wm.windows[0].visible == true);
    assert(wm.windows[0].focused == true);

    /* destroy */
    st = o0_wm_destroy(&wm, id);
    assert(st == O0_OK);
    assert(wm.count == 0);
    assert(wm.focused == 0);

    /* destroy non-existent */
    st = o0_wm_destroy(&wm, 999);
    assert(st == O0_ENOENT);

    free(buf);
    puts("window_test: create/destroy passed");
}

static void test_move(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    uint32_t *buf = alloc_surface_buf(100, 100);
    struct o0_surface s = make_surface(buf, 100, 100);

    o0_window_id id;
    o0_status st = o0_wm_create(&wm, &s, 10, 10, &id);
    assert(st == O0_OK);

    st = o0_wm_move(&wm, id, 50, 60);
    assert(st == O0_OK);
    assert(wm.windows[0].x == 50);
    assert(wm.windows[0].y == 60);

    /* move non-existent */
    st = o0_wm_move(&wm, 999, 0, 0);
    assert(st == O0_ENOENT);

    free(buf);
    puts("window_test: move passed");
}

static void test_focus(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    uint32_t *buf1 = alloc_surface_buf(100, 100);
    struct o0_surface s1 = make_surface(buf1, 100, 100);
    uint32_t *buf2 = alloc_surface_buf(100, 100);
    struct o0_surface s2 = make_surface(buf2, 100, 100);

    o0_window_id id1, id2;
    o0_status st = o0_wm_create(&wm, &s1, 10, 10, &id1);
    assert(st == O0_OK);
    st = o0_wm_create(&wm, &s2, 20, 20, &id2);
    assert(st == O0_OK);

    /* id2 should be focused (last created) */
    assert(wm.focused == id2);
    assert(wm.windows[0].focused == false);
    assert(wm.windows[1].focused == true);

    /* focus id1 - should move it to front */
    st = o0_wm_focus(&wm, id1);
    assert(st == O0_OK);
    assert(wm.focused == id1);
    assert(wm.windows[0].focused == false); /* id2 now at index 0 */
    assert(wm.windows[1].focused == true);  /* id1 now at index 1 */

    /* focus non-existent */
    st = o0_wm_focus(&wm, 999);
    assert(st == O0_ENOENT);

    free(buf1);
    free(buf2);
    puts("window_test: focus passed");
}

static void test_hit_test(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    uint32_t *buf1 = alloc_surface_buf(100, 100);
    struct o0_surface s1 = make_surface(buf1, 100, 100);
    uint32_t *buf2 = alloc_surface_buf(100, 100);
    struct o0_surface s2 = make_surface(buf2, 100, 100);

    o0_window_id id1, id2;
    o0_status st = o0_wm_create(&wm, &s1, 10, 10, &id1);
    assert(st == O0_OK);
    st = o0_wm_create(&wm, &s2, 20, 20, &id2);
    assert(st == O0_OK);

    /* hit test on id2 (top window) */
    struct o0_window *hit = o0_wm_hit_test(&wm, 25, 25);
    assert(hit != NULL);
    assert(hit->id == id2);

    /* hit test on id1 (bottom window) where id2 doesn't cover */
    hit = o0_wm_hit_test(&wm, 15, 15);
    assert(hit != NULL);
    assert(hit->id == id1);

    /* hit test outside all windows */
    hit = o0_wm_hit_test(&wm, 200, 200);
    assert(hit == NULL);

    /* hit test with invisible window */
    wm.windows[1].visible = false;
    hit = o0_wm_hit_test(&wm, 25, 25);
    assert(hit != NULL);
    assert(hit->id == id1);

    free(buf1);
    free(buf2);
    puts("window_test: hit_test passed");
}

static void test_max_windows(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    /* Fill up to O0_WINDOW_MAX */
    for (uint32_t i = 0; i < O0_WINDOW_MAX; i++) {
        uint32_t *buf = alloc_surface_buf(10, 10);
        struct o0_surface s = make_surface(buf, 10, 10);
        o0_window_id id;
        o0_status st = o0_wm_create(&wm, &s, 0, 0, &id);
        assert(st == O0_OK);
        free(buf);
    }
    assert(wm.count == O0_WINDOW_MAX);

    /* One more should fail */
    uint32_t *buf = alloc_surface_buf(10, 10);
    struct o0_surface s = make_surface(buf, 10, 10);
    o0_window_id id;
    o0_status st = o0_wm_create(&wm, &s, 0, 0, &id);
    assert(st == O0_ENOMEM);
    free(buf);

    puts("window_test: max_windows passed");
}

static void test_null_args(void) {
    struct o0_window_manager wm;
    o0_wm_init(&wm);

    uint32_t *buf = alloc_surface_buf(100, 100);
    struct o0_surface s = make_surface(buf, 100, 100);
    o0_window_id id;

    o0_status st = o0_wm_create(NULL, &s, 0, 0, &id);
    assert(st == O0_EINVAL);
    st = o0_wm_create(&wm, NULL, 0, 0, &id);
    assert(st == O0_EINVAL);
    st = o0_wm_create(&wm, &s, 0, 0, NULL);
    assert(st == O0_EINVAL);

    /* Surface with invalid pixels */
    struct o0_surface bad_s = {0};
    st = o0_wm_create(&wm, &bad_s, 0, 0, &id);
    assert(st == O0_EINVAL);

    /* Surface with zero width/height */
    bad_s = s;
    bad_s.width = 0;
    st = o0_wm_create(&wm, &bad_s, 0, 0, &id);
    assert(st == O0_EINVAL);

    st = o0_wm_destroy(NULL, 1);
    assert(st == O0_EINVAL);

    st = o0_wm_move(NULL, 1, 0, 0);
    assert(st == O0_EINVAL);

    st = o0_wm_focus(NULL, 1);
    assert(st == O0_EINVAL);

    assert(o0_wm_hit_test(NULL, 0, 0) == NULL);

    free(buf);
    puts("window_test: null_args passed");
}

int main(void) {
    test_create_destroy();
    test_move();
    test_focus();
    test_hit_test();
    test_max_windows();
    test_null_args();
    puts("window_test: all checks passed");
    return 0;
}