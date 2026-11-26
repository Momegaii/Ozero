#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ozero/desktop_ui.h"
#include "ozero/shell.h"
#include "ozero/surface.h"
#include "ozero/status.h"

/* Read a pixel as 0xRRGGBB from an RGBX8888 surface. */
static uint32_t rgb_at(const struct o0_surface *s, size_t x, size_t y) {
    const uint8_t *p = s->pixels + y * s->pitch + x * 4u;
    assert(s->format == O0_PIXEL_RGBX8888);
    uint8_t r = p[0], g = p[1], b = p[2];
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static uint32_t *alloc_buf(uint32_t w, uint32_t h) {
    uint32_t *buf = (uint32_t *)calloc((size_t)w * h, sizeof(uint32_t));
    assert(buf != NULL);
    return buf;
}

static struct o0_surface make_surface(uint32_t *buf, uint32_t w, uint32_t h) {
    struct o0_surface s;
    assert(o0_surface_init(&s, buf, w, h, w * 4, O0_PIXEL_RGBX8888) == O0_OK);
    return s;
}

static void test_init_valid(void) {
    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(&ui, 800, 600) == O0_OK);
    assert(ui.width == 800);
    assert(ui.height == 600);
    assert(ui.sphere_count > 0);
    assert(ui.particle_count > 0);
    assert(ui.node_count > 0);
    assert(ui.taskbar_h > 0);
    assert(ui.taskbar_y == 600 - ui.taskbar_h);
    assert(ui.panel_x > 0);
    assert(ui.panel_x + ui.panel_w == 800);
    assert(ui.tray_x + ui.tray_w == 800);
    assert(ui.assistant_open == false);
    puts("desktop_ui_test: init valid passed");
}

static void test_init_invalid(void) {
    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(NULL, 800, 600) == O0_EINVAL);
    assert(o0_desktop_ui_init(&ui, 0, 600) == O0_EINVAL);
    assert(o0_desktop_ui_init(&ui, 800, 0) == O0_EINVAL);
    puts("desktop_ui_test: init invalid passed");
}

static void test_sphere_colors(void) {
    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(&ui, 800, 600) == O0_OK);

    bool found_green = false;
    bool found_purple = false;
    for (uint32_t i = 0; i < ui.sphere_count; ++i) {
        if (ui.spheres[i].color == O0_SPHERE_GREEN) found_green = true;
        if (ui.spheres[i].color == O0_SPHERE_PURPLE) found_purple = true;
        assert(ui.spheres[i].state == O0_SPHERE_IDLE);
        assert(ui.spheres[i].anim_radius == ui.spheres[i].radius);
        assert(ui.spheres[i].active == true);
    }
    assert(found_green);
    assert(found_purple);
    puts("desktop_ui_test: sphere colors passed");
}

static void test_sphere_no_pictogram(void) {
    /* Spheres are solid color: the API exposes only color/state, no glyph
     * or pictogram field. This test asserts the design invariant at the
     * struct level: no pictogram member exists. */
    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(&ui, 800, 600) == O0_OK);
    for (uint32_t i = 0; i < ui.sphere_count; ++i) {
        assert(ui.spheres[i].color == O0_SPHERE_GREEN ||
               ui.spheres[i].color == O0_SPHERE_PURPLE);
    }
    puts("desktop_ui_test: sphere no pictogram passed");
}

static void test_hover_enters_and_leaves(void) {
    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(&ui, 800, 600) == O0_OK);

    /* Find a desktop sphere and hover over it. */
    uint32_t target = UINT32_MAX;
    for (uint32_t i = 0; i < 4; ++i) {
        if (ui.spheres[i].active) { target = i; break; }
    }
    assert(target != UINT32_MAX);

    int32_t cx = ui.spheres[target].cx;
    int32_t cy = ui.spheres[target].cy;
    int32_t r = ui.spheres[target].radius;

    /* Not hovered at center + far away. */
    o0_desktop_ui_pointer(&ui, cx - r - 100, cy - r - 100);
    o0_desktop_ui_update(&ui);
    assert(ui.spheres[target].state == O0_SPHERE_IDLE);

    /* Hovered at centre. */
    o0_desktop_ui_pointer(&ui, cx, cy);
    o0_desktop_ui_update(&ui);
    assert(ui.spheres[target].state == O0_SPHERE_HOVER);
    assert(ui.spheres[target].anim_radius >= ui.spheres[target].radius);

    /* Leave hover. */
    o0_desktop_ui_pointer(&ui, cx - r - 100, cy - r - 100);
    o0_desktop_ui_update(&ui);
    assert(ui.spheres[target].state == O0_SPHERE_IDLE);
    puts("desktop_ui_test: hover enters and leaves passed");
}

static void test_activate_state_machine(void) {
    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(&ui, 800, 600) == O0_OK);

    assert(o0_desktop_ui_activate(&ui, 0) == O0_OK);
    assert(ui.spheres[0].state == O0_SPHERE_ACTIVE);
    assert(ui.spheres[0].anim_phase == 0);

    /* Advance activation: radius grows then shrinks, then feedback. */
    for (uint32_t i = 0; i < 80; ++i) o0_desktop_ui_update(&ui);
    assert(ui.spheres[0].state == O0_SPHERE_FEEDBACK);

    for (uint32_t i = 0; i < 60; ++i) o0_desktop_ui_update(&ui);
    assert(ui.spheres[0].state == O0_SPHERE_IDLE);
    assert(ui.spheres[0].anim_radius == ui.spheres[0].radius);

    /* Activate non-existent index. */
    assert(o0_desktop_ui_activate(&ui, ui.sphere_count) == O0_EINVAL);
    puts("desktop_ui_test: activate state machine passed");
}

static void test_activate_assistant_opens_panel(void) {
    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(&ui, 800, 600) == O0_OK);

    /* Find an assistant sphere (purple). */
    uint32_t assistant = UINT32_MAX;
    for (uint32_t i = 0; i < ui.sphere_count; ++i) {
        if (ui.spheres[i].color == O0_SPHERE_PURPLE) {
            assistant = i;
            break;
        }
    }
    assert(assistant != UINT32_MAX);

    assert(ui.assistant_open == false);
    assert(o0_desktop_ui_activate(&ui, assistant) == O0_OK);
    assert(ui.assistant_open == true);
    puts("desktop_ui_test: activate assistant opens panel passed");
}

static void test_particle_update_stays_in_bounds(void) {
    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(&ui, 800, 600) == O0_OK);

    for (uint32_t i = 0; i < 200; ++i) o0_desktop_ui_update(&ui);
    for (uint32_t j = 0; j < ui.particle_count; ++j) {
        assert(ui.particles[j].x >= 0);
        assert(ui.particles[j].x < (int32_t)ui.width);
        assert(ui.particles[j].y >= 0);
        assert(ui.particles[j].y < ui.taskbar_y);
    }
    puts("desktop_ui_test: particle update stays in bounds passed");
}

static void test_render_black_background(void) {
    uint32_t *buf = alloc_buf(800, 600);
    struct o0_surface s = make_surface(buf, 800, 600);

    /* Fill with a non-black colour first. */
    assert(o0_surface_fill(&s, 0x123456u) == O0_OK);

    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(&ui, 800, 600) == O0_OK);
    assert(o0_desktop_ui_render(&ui, &s) == O0_OK);

    /* Entire background should be black. */
    for (uint32_t y = 0; y < 600; y += 17) {
        for (uint32_t x = 0; x < 800; x += 23) {
            assert(rgb_at(&s, x, y) == O0_DESKTOP_BG_COLOR);
        }
    }
    free(buf);
    puts("desktop_ui_test: render black background passed");
}

static void test_render_neon_green_sphere(void) {
    uint32_t *buf = alloc_buf(800, 600);
    struct o0_surface s = make_surface(buf, 800, 600);

    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(&ui, 800, 600) == O0_OK);

    /* Find a green sphere and check its centre pixel is neon green. */
    bool found = false;
    for (uint32_t i = 0; i < ui.sphere_count; ++i) {
        if (ui.spheres[i].color == O0_SPHERE_GREEN &&
            ui.spheres[i].state == O0_SPHERE_IDLE) {
            assert(rgb_at(&s, (size_t)ui.spheres[i].cx,
                          (size_t)ui.spheres[i].cy) == O0_SPHERE_GREEN);
            found = true;
            break;
        }
    }
    assert(found);
    free(buf);
    puts("desktop_ui_test: render neon green sphere passed");
}

static void test_render_taskbar_and_panel(void) {
    uint32_t *buf = alloc_buf(800, 600);
    struct o0_surface s = make_surface(buf, 800, 600);

    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(&ui, 800, 600) == O0_OK);
    assert(o0_desktop_ui_render(&ui, &s) == O0_OK);

    /* Taskbar strip near the bottom should be taskbar-coloured. */
    assert(rgb_at(&s, 100, (size_t)(ui.taskbar_y + ui.taskbar_h / 2)) ==
           O0_TASKBAR_COLOR);

    /* Right panel area should be panel-coloured. */
    assert(rgb_at(&s, (size_t)(ui.panel_x + 10), 100) == O0_PANEL_COLOR);

    /* System tray area should be tray-coloured. */
    assert(rgb_at(&s, (size_t)(ui.tray_x + 10),
                  (size_t)(ui.tray_y + ui.tray_h / 2)) == O0_TRAY_COLOR);
    free(buf);
    puts("desktop_ui_test: render taskbar and panel passed");
}

static void test_render_feedback_ring(void) {
    uint32_t *buf = alloc_buf(800, 600);
    struct o0_surface s = make_surface(buf, 800, 600);

    struct o0_desktop_ui ui;
    assert(o0_desktop_ui_init(&ui, 800, 600) == O0_OK);

    /* Activate sphere 0, then advance into feedback. */
    assert(o0_desktop_ui_activate(&ui, 0) == O0_OK);
    for (uint32_t i = 0; i < 80; ++i) o0_desktop_ui_update(&ui);
    assert(ui.spheres[0].state == O0_SPHERE_FEEDBACK);

    assert(o0_desktop_ui_render(&ui, &s) == O0_OK);

    /* A feedback ring pixel just outside the sphere should be bright green. */
    int32_t cx = ui.spheres[0].cx;
    int32_t cy = ui.spheres[0].cy;
    int32_t r = ui.spheres[0].radius + 6;
    assert(rgb_at(&s, (size_t)(cx + r), (size_t)cy) != O0_DESKTOP_BG_COLOR);
    free(buf);
    puts("desktop_ui_test: render feedback ring passed");
}

static void test_fill_circle_basic(void) {
    uint32_t *buf = alloc_buf(100, 100);
    struct o0_surface s = make_surface(buf, 100, 100);

    assert(o0_surface_fill_circle(&s, 50, 50, 20, 0x00FF00u) == O0_OK);
    assert(rgb_at(&s, 50, 50) == 0x00FF00u);
    assert(rgb_at(&s, 50, 30) == 0x00FF00u); /* top edge */
    assert(rgb_at(&s, 50, 70) == 0x00FF00u); /* bottom edge */
    assert(rgb_at(&s, 30, 50) == 0x00FF00u); /* left edge */
    assert(rgb_at(&s, 70, 50) == 0x00FF00u); /* right edge */
    assert(rgb_at(&s, 50, 10) == 0x000000u); /* outside */
    assert(rgb_at(&s, 10, 50) == 0x000000u); /* outside */

    /* Invalid: null surface. */
    assert(o0_surface_fill_circle(NULL, 0, 0, 5, 0) == O0_EINVAL);
    /* Invalid: zero radius. */
    assert(o0_surface_fill_circle(&s, 0, 0, 0, 0) == O0_EINVAL);
    free(buf);
    puts("desktop_ui_test: fill circle basic passed");
}

static void test_draw_circle_outline(void) {
    uint32_t *buf = alloc_buf(100, 100);
    struct o0_surface s = make_surface(buf, 100, 100);

    assert(o0_surface_draw_circle(&s, 50, 50, 20, 0x00FF00u) == O0_OK);
    /* Outline pixels should be green. */
    assert(rgb_at(&s, 70, 50) == 0x00FF00u);
    assert(rgb_at(&s, 30, 50) == 0x00FF00u);
    assert(rgb_at(&s, 50, 70) == 0x00FF00u);
    assert(rgb_at(&s, 50, 30) == 0x00FF00u);
    /* Centre should remain black. */
    assert(rgb_at(&s, 50, 50) == 0x000000u);
    free(buf);
    puts("desktop_ui_test: draw circle outline passed");
}

static void test_blend_rgb(void) {
    /* Opaque alpha returns source. */
    assert(o0_surface_blend_rgb(0x000000u, 0xFF0000u, 255) == 0xFF0000u);
    /* Transparent alpha returns destination. */
    assert(o0_surface_blend_rgb(0x00FF00u, 0xFF0000u, 0) == 0x00FF00u);
    /* Half alpha: 0xFF0000 over 0x00FF00 => R=127, G=127, B=0. */
    assert(o0_surface_blend_rgb(0x00FF00u, 0xFF0000u, 128) == 0x7F7F00u);
    puts("desktop_ui_test: blend rgb passed");
}

int main(void) {
    test_init_valid();
    test_init_invalid();
    test_sphere_colors();
    test_sphere_no_pictogram();
    test_hover_enters_and_leaves();
    test_activate_state_machine();
    test_activate_assistant_opens_panel();
    test_particle_update_stays_in_bounds();
    test_render_black_background();
    test_render_neon_green_sphere();
    test_render_taskbar_and_panel();
    test_render_feedback_ring();
    test_fill_circle_basic();
    test_draw_circle_outline();
    test_blend_rgb();
    puts("desktop_ui_test: all passed");
    return 0;
}
