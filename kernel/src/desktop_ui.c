/* Stage 6.7: custom O0 desktop UI implementation.
 *
 * Advanced dark desktop with neon-green sphere launchers,
 * purple assistant spheres, green particles with trails, a node network,
 * a bottom taskbar, a right-side panel, a system tray, an integrated O0
 * assistant interface, keyboard focus rings, and a visible mouse cursor.
 *
 * All drawing uses o0_surface_* primitives; no GPU, no external toolkit,
 * no ELF/X11/Wayland assumptions. Animation is frame-driven (16ms budget
 * from desktop_main), using integer math only.
 *
 * Ozero defines its own native execution architecture and does not default
 * to Linux conventions.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ozero/desktop_ui.h"
#include "ozero/shell.h"
#include "ozero/surface.h"
#include "ozero/status.h"

/* ---- fixed-point helpers (16 fractional bits) ---- */

#define O0_FP_ONE 16384
#define O0_FP_TO_INT(v) (((v) + (O0_FP_ONE / 2)) / O0_FP_ONE)

/* ---- colour helpers (0xRRGGBB packed) ---- */

static uint8_t color_r(uint32_t c) { return (uint8_t)(c >> 16); }
static uint8_t color_g(uint32_t c) { return (uint8_t)(c >> 8); }
static uint8_t color_b(uint32_t c) { return (uint8_t)c; }

/* Brighten a colour by a factor (256 = no change, 512 = double). */
static uint32_t color_brighten(uint32_t c, uint32_t factor) {
    uint32_t r = ((uint32_t)color_r(c) * factor) >> 8;
    uint32_t g = ((uint32_t)color_g(c) * factor) >> 8;
    uint32_t b = ((uint32_t)color_b(c) * factor) >> 8;
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return (r << 16) | (g << 8) | b;
}

/* ---- sphere helpers ---- */

static int32_t sphere_dist2(const struct o0_sphere *s, int32_t px, int32_t py) {
    int32_t dx = px - s->cx;
    int32_t dy = py - s->cy;
    return dx * dx + dy * dy;
}

static bool sphere_hit(const struct o0_sphere *s, int32_t px, int32_t py,
                       int32_t margin) {
    int32_t r = s->anim_radius + margin;
    return sphere_dist2(s, px, py) <= r * r;
}

/* ---- desktop UI state (single instance) ---- */

static struct o0_desktop_ui g_ui;

/* ---- init ---- */

o0_status o0_desktop_ui_init(struct o0_desktop_ui *ui, uint32_t width,
                             uint32_t height) {
    if (ui == NULL || width == 0 || height == 0) {
        return O0_EINVAL;
    }
    memset(ui, 0, sizeof(*ui));
    ui->width = width;
    ui->height = height;

    /* UI area geometry. */
    ui->taskbar_h = (int32_t)(height / 10);
    if (ui->taskbar_h < 40) ui->taskbar_h = 40;
    ui->taskbar_y = (int32_t)height - ui->taskbar_h;
    ui->taskbar_x = 0;
    ui->taskbar_w = (int32_t)width;

    ui->panel_w = (int32_t)(width / 5);
    if (ui->panel_w < 120) ui->panel_w = 120;
    ui->panel_x = (int32_t)width - ui->panel_w;
    ui->panel_y = 0;
    ui->panel_h = (int32_t)height - ui->taskbar_h;

    ui->tray_w = 160;
    ui->tray_h = 40;
    ui->tray_x = (int32_t)width - ui->tray_w;
    ui->tray_y = (int32_t)height - ui->tray_h;

    /* ---- desktop spheres: arranged in a circle around screen centre ---- */
    int32_t cx = (int32_t)(width / 2);
    int32_t cy = (int32_t)((height - ui->taskbar_h) / 2);
    int32_t orbit_r = (int32_t)((height - ui->taskbar_h) / 4);
    if (orbit_r < 60) orbit_r = 60;
    int32_t sr = 26;
    static const uint8_t sphere_types[] = {
        O0_SPHERE_LAUNCHER, O0_SPHERE_LAUNCHER,
        O0_SPHERE_LAUNCHER, O0_SPHERE_ASSISTANT,
    };
    /* Four cardinal positions: right, up, left, down */
    static const int32_t sphere_dx[] = {1, 0, -1, 0};
    static const int32_t sphere_dy[] = {0, -1, 0, 1};
    uint32_t n = 0;
    for (uint32_t i = 0; i < 4 && n < O0_DESKTOP_MAX_SPHERES; ++i) {
        struct o0_sphere *s = &ui->spheres[n++];
        s->cx = cx + sphere_dx[i] * orbit_r;
        s->cy = cy + sphere_dy[i] * orbit_r;
        s->radius = sr;
        s->anim_radius = sr;
        s->color = (sphere_types[i] == O0_SPHERE_ASSISTANT)
                       ? O0_SPHERE_PURPLE
                       : O0_SPHERE_GREEN;
        s->type = sphere_types[i];
        s->state = O0_SPHERE_IDLE;
        s->anim_phase = 0;
        s->active = true;
    }
    ui->sphere_count = n;

    /* ---- taskbar spheres: evenly spaced along the bottom ---- */
    int32_t tb_y = ui->taskbar_y + ui->taskbar_h / 2;
    int32_t tb_r = 18;
    static const uint8_t tb_types[] = {
        O0_SPHERE_LAUNCHER, O0_SPHERE_LAUNCHER,
        O0_SPHERE_LAUNCHER, O0_SPHERE_ASSISTANT,
    };
    int32_t tb_start = (int32_t)(width / 6);
    int32_t tb_step = (int32_t)((width * 2 / 3) / 4);
    for (uint32_t i = 0; i < 4 && n < O0_DESKTOP_MAX_SPHERES; ++i) {
        struct o0_sphere *s = &ui->spheres[n++];
        s->cx = tb_start + (int32_t)i * tb_step;
        s->cy = tb_y;
        s->radius = tb_r;
        s->anim_radius = tb_r;
        s->color = (tb_types[i] == O0_SPHERE_ASSISTANT)
                       ? O0_SPHERE_PURPLE
                       : O0_SPHERE_GREEN;
        s->type = tb_types[i];
        s->state = O0_SPHERE_IDLE;
        s->anim_phase = 0;
        s->active = true;
    }
    ui->sphere_count = n;

    /* ---- nodes: connect desktop spheres (indices 0-3) ---- */
    uint32_t node_n = 0;
    for (uint32_t i = 0; i < 4 && node_n < O0_DESKTOP_MAX_NODES; ++i) {
        for (uint32_t j = i + 1; j < 4 && node_n < O0_DESKTOP_MAX_NODES; ++j) {
            ui->nodes[node_n].a = i;
            ui->nodes[node_n].b = j;
            ui->nodes[node_n].brightness = 64;
            ui->nodes[node_n].active = true;
            ++node_n;
        }
    }
    ui->node_count = node_n;

    /* ---- particles: scattered across the upper screen area ---- */
    uint32_t pn = 0;
    for (uint32_t i = 0; i < 32 && pn < O0_DESKTOP_MAX_PARTICLES; ++i) {
        struct o0_particle *p = &ui->particles[pn++];
        p->x = (int32_t)((i * 777) % (int32_t)width);
        p->y = (int32_t)((i * 131) % (ui->taskbar_y));
        p->vx = (int32_t)(((i * 37) % 32) - 16); /* -16..15 fixed-point */
        p->vy = (int32_t)(((i * 53) % 32) - 16);
        p->life = (uint8_t)(128 + (i % 128));
        p->active = true;
    }
    ui->particle_count = pn;

    ui->pointer_x = (int32_t)(width / 2);
    ui->pointer_y = (int32_t)(height / 2);
    ui->frame = 0;
    ui->assistant_open = false;
    ui->focus_index = UINT32_MAX;

    return O0_OK;
}

/* ---- update ---- */

void o0_desktop_ui_update(struct o0_desktop_ui *ui) {
    if (ui == NULL) return;
    ++ui->frame;

    /* ---- sphere hover detection and animation ---- */
    for (uint32_t i = 0; i < ui->sphere_count; ++i) {
        struct o0_sphere *s = &ui->spheres[i];
        if (!s->active) continue;

        bool hovered = sphere_hit(s, ui->pointer_x, ui->pointer_y,
                                  s->radius / 2);

        switch (s->state) {
        case O0_SPHERE_IDLE:
            if (hovered) {
                s->state = O0_SPHERE_HOVER;
                s->anim_phase = 0;
            }
            s->anim_radius = s->radius;
            break;
        case O0_SPHERE_HOVER:
            if (!hovered) {
                s->state = O0_SPHERE_IDLE;
                s->anim_phase = 0;
                s->anim_radius = s->radius;
            } else {
                /* Enlarged radius: base + up to 8 pixels, sinusoidal-ish. */
                uint32_t phase = s->anim_phase;
                int32_t pulse = (int32_t)((phase % 60) * 8 / 60);
                s->anim_radius = s->radius + pulse;
                ++s->anim_phase;
            }
            break;
        case O0_SPHERE_ACTIVE:
            /* Expanding halo: radius grows then shrinks back. */
            if (s->anim_phase < 40) {
                s->anim_radius = s->radius + (int32_t)(s->anim_phase * 3);
                ++s->anim_phase;
            } else if (s->anim_phase < 80) {
                s->anim_radius = s->radius + (int32_t)((80 - s->anim_phase) * 3);
                ++s->anim_phase;
            } else {
                s->state = O0_SPHERE_FEEDBACK;
                s->anim_phase = 0;
                s->anim_radius = s->radius;
            }
            break;
        case O0_SPHERE_FEEDBACK:
            if (s->anim_phase < 60) {
                ++s->anim_phase;
            } else {
                s->state = O0_SPHERE_IDLE;
                s->anim_phase = 0;
                s->anim_radius = s->radius;
            }
            break;
        }
    }

    /* ---- particle update ---- */
    for (uint32_t i = 0; i < ui->particle_count; ++i) {
        struct o0_particle *p = &ui->particles[i];
        if (!p->active) continue;
        p->x += O0_FP_TO_INT(p->vx);
        p->y += O0_FP_TO_INT(p->vy);
        if (p->x < 0) { p->x = 0; p->vx = -p->vx; }
        if (p->x >= (int32_t)ui->width) {
            p->x = (int32_t)ui->width - 1;
            p->vx = -p->vx;
        }
        if (p->y < 0) { p->y = 0; p->vy = -p->vy; }
        if (p->y >= ui->taskbar_y) {
            p->y = ui->taskbar_y - 1;
            p->vy = -p->vy;
        }
        if (p->life > 0) --p->life;
        if (p->life == 0) {
            /* Respawn. */
            p->x = (int32_t)((ui->frame * 777 + i * 13) % (int32_t)ui->width);
            p->y = (int32_t)((ui->frame * 131 + i * 7) % ui->taskbar_y);
            p->life = (uint8_t)(128 + (ui->frame + i) % 128);
        }
    }
}

void o0_desktop_ui_pointer(struct o0_desktop_ui *ui, int32_t x, int32_t y) {
    if (ui == NULL) return;
    ui->pointer_x = x;
    ui->pointer_y = y;
}

o0_status o0_desktop_ui_activate(struct o0_desktop_ui *ui, uint32_t index) {
    if (ui == NULL || index >= ui->sphere_count) return O0_EINVAL;
    struct o0_sphere *s = &ui->spheres[index];
    if (!s->active) return O0_EINVAL;
    s->state = O0_SPHERE_ACTIVE;
    s->anim_phase = 0;
    ui->focus_index = index;
    if (s->type == O0_SPHERE_ASSISTANT) {
        ui->assistant_open = true;
    }
    return O0_OK;
}

/* ---- rendering helpers ---- */

static void ui_fill_rect(struct o0_surface *s, int32_t x, int32_t y,
                         int32_t w, int32_t h, uint32_t rgb) {
    (void)o0_surface_fill_rect(s, x, y, w, h, rgb);
}

static void ui_draw_node(struct o0_desktop_ui *ui, struct o0_surface *s,
                         const struct o0_node *node) {
    const struct o0_sphere *sa = &ui->spheres[node->a];
    const struct o0_sphere *sb = &ui->spheres[node->b];
    if (!sa->active || !sb->active) return;

    /* Draw a line between sphere centres using Bresenham. */
    int32_t x0 = sa->cx, y0 = sa->cy;
    int32_t x1 = sb->cx, y1 = sb->cy;
    int32_t dx = x1 - x0;
    int32_t dy = y1 - y0;
    int32_t ax = dx < 0 ? -dx : dx;
    int32_t ay = dy < 0 ? -dy : dy;
    int32_t sx = dx < 0 ? -1 : 1;
    int32_t sy = dy < 0 ? -1 : 1;
    int32_t err = ax - ay;

    uint32_t line_color = O0_NODE_DIM;
    /* Brighten if either endpoint is hovered or active. */
    for (uint32_t i = 0; i < ui->sphere_count; ++i) {
        if ((i == node->a || i == node->b) &&
            (ui->spheres[i].state == O0_SPHERE_HOVER ||
             ui->spheres[i].state == O0_SPHERE_ACTIVE)) {
            line_color = O0_NODE_BRIGHT;
            break;
        }
    }

    for (;;) {
        if (x0 >= 0 && x0 < (int32_t)s->width && y0 >= 0 &&
            y0 < (int32_t)s->height) {
            o0_surface_put_pixel(s, x0, y0, line_color);
        }
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -ay) { err -= ay; x0 += sx; }
        if (e2 < ax) { err += ax; y0 += sy; }
    }
}

static void ui_draw_sphere(struct o0_surface *s,
                           const struct o0_sphere *sph) {
    if (!sph->active) return;

    uint32_t sph_color = sph->color;
    if (sph->state == O0_SPHERE_HOVER) {
        sph_color = color_brighten(sph->color, 400); /* ~1.5x brighter */
    }

    /* Outer glow rings (drawn first, behind the sphere body).
     * Three concentric rings create a soft neon glow. */
    if (sph->state == O0_SPHERE_HOVER ||
        sph->state == O0_SPHERE_ACTIVE ||
        sph->state == O0_SPHERE_FEEDBACK) {
        uint32_t glow_outer = color_brighten(sph->color, 120);
        uint32_t glow_mid   = color_brighten(sph->color, 220);
        uint32_t glow_inner = color_brighten(sph->color, 350);
        o0_surface_draw_circle(s, sph->cx, sph->cy,
                               sph->anim_radius + 12, glow_outer);
        o0_surface_draw_circle(s, sph->cx, sph->cy,
                               sph->anim_radius + 8, glow_mid);
        o0_surface_draw_circle(s, sph->cx, sph->cy,
                               sph->anim_radius + 4, glow_inner);
    }

    /* Solid sphere body. */
    o0_surface_fill_circle(s, sph->cx, sph->cy, sph->anim_radius, sph_color);

    /* Inner highlight (top-left quadrant) for a subtle 3D effect. */
    if (sph->anim_radius > 4) {
        int32_t hl_r = sph->anim_radius / 3;
        if (hl_r > 0) {
            o0_surface_fill_circle(s,
                sph->cx - sph->anim_radius / 3,
                sph->cy - sph->anim_radius / 3,
                hl_r, color_brighten(sph_color, 160));
        }
    }

    /* Halo for active spheres. */
    if (sph->state == O0_SPHERE_ACTIVE) {
        int32_t halo_r = sph->anim_radius + 4;
        uint32_t halo_color = color_brighten(sph->color, 600);
        o0_surface_draw_circle(s, sph->cx, sph->cy, halo_r, halo_color);
        if (halo_r > sph->radius + 2) {
            o0_surface_draw_circle(s, sph->cx, sph->cy,
                                   sph->radius + 2, sph->color);
        }
    }

    /* Feedback ring. */
    if (sph->state == O0_SPHERE_FEEDBACK) {
        uint32_t ring_color = color_brighten(sph->color, 300);
        o0_surface_draw_circle(s, sph->cx, sph->cy,
                               sph->anim_radius + 6, ring_color);
    }
}

static void ui_draw_particles(struct o0_desktop_ui *ui,
                              struct o0_surface *s) {
    for (uint32_t i = 0; i < ui->particle_count; ++i) {
        struct o0_particle *p = &ui->particles[i];
        if (!p->active || p->life == 0) continue;
        if (p->x < 0 || p->x >= (int32_t)s->width ||
            p->y < 0 || p->y >= (int32_t)s->height) continue;

        /* Trail: a dim dot behind the particle's direction of motion. */
        int32_t tx = p->x - O0_FP_TO_INT(p->vx);
        int32_t ty = p->y - O0_FP_TO_INT(p->vy);
        if (tx >= 0 && tx < (int32_t)s->width && ty >= 0 &&
            ty < (int32_t)s->height) {
            o0_surface_put_pixel(s, tx, ty, O0_PARTICLE_DIM);
        }

        /* Particle body: brighter for high life, dim for low. */
        uint8_t alpha = (uint8_t)((uint32_t)p->life * 255u / 200u);
        uint32_t base = (p->life > 100) ? O0_PARTICLE_BRIGHT : O0_PARTICLE_DIM;
        uint32_t blended = o0_surface_blend_rgb(
            O0_DESKTOP_BG_COLOR, base, alpha);
        o0_surface_put_pixel(s, p->x, p->y, blended);
    }
}

static void ui_draw_panel(struct o0_desktop_ui *ui, struct o0_surface *s) {
    /* Right-side information panel base. */
    ui_fill_rect(s, ui->panel_x, ui->panel_y, ui->panel_w, ui->panel_h,
                 O0_PANEL_COLOR);

    /* Left border: bright neon. */
    for (int32_t y = ui->panel_y; y < ui->panel_y + ui->panel_h; y += 3) {
        o0_surface_put_pixel(s, ui->panel_x, y, O0_NODE_BRIGHT);
    }
    /* Inner left border: subtle. */
    for (int32_t y = ui->panel_y; y < ui->panel_y + ui->panel_h; y += 6) {
        o0_surface_put_pixel(s, ui->panel_x + 1, y, 0x002000u);
    }

    /* Top border: bright neon. */
    for (int32_t x = ui->panel_x; x < ui->panel_x + ui->panel_w; x += 3) {
        o0_surface_put_pixel(s, x, ui->panel_y, O0_NODE_BRIGHT);
    }
    /* Bottom border: subtle. */
    for (int32_t x = ui->panel_x; x < ui->panel_x + ui->panel_w; x += 6) {
        o0_surface_put_pixel(s, x, ui->panel_y + ui->panel_h - 1,
                             0x001000u);
    }
}

static void ui_draw_taskbar(struct o0_desktop_ui *ui,
                            struct o0_surface *s) {
    /* Main taskbar fill. */
    ui_fill_rect(s, ui->taskbar_x, ui->taskbar_y, ui->taskbar_w,
                 ui->taskbar_h, O0_TASKBAR_COLOR);

    /* Top edge: bright neon highlight (dashed). */
    for (int32_t x = ui->taskbar_x; x < ui->taskbar_x + ui->taskbar_w; x += 4) {
        o0_surface_put_pixel(s, x, ui->taskbar_y, O0_NODE_BRIGHT);
    }
    /* Inner top highlight: subtle. */
    for (int32_t x = ui->taskbar_x; x < ui->taskbar_x + ui->taskbar_w; x += 8) {
        o0_surface_put_pixel(s, x, ui->taskbar_y + 2, 0x002000u);
    }
    /* Bottom edge: subtle. */
    for (int32_t x = ui->taskbar_x; x < ui->taskbar_x + ui->taskbar_w; x += 4) {
        o0_surface_put_pixel(s, x, ui->taskbar_y + ui->taskbar_h - 1,
                             0x001000u);
    }
}

static void ui_draw_tray(struct o0_desktop_ui *ui, struct o0_surface *s) {
    /* System tray base. */
    ui_fill_rect(s, ui->tray_x, ui->tray_y, ui->tray_w, ui->tray_h,
                 O0_TRAY_COLOR);
    /* Clock placeholder: a small green dot pulsing. */
    uint32_t pulse = (ui->frame % 60 < 30) ? O0_SPHERE_GREEN
                                           : O0_PARTICLE_DIM;
    int32_t cx = ui->tray_x + 24;
    int32_t cy = ui->tray_y + ui->tray_h / 2;
    o0_surface_fill_circle(s, cx, cy, 6, pulse);
}

static void ui_draw_assistant(struct o0_desktop_ui *ui,
                              struct o0_surface *s) {
    if (!ui->assistant_open) return;
    /* Assistant panel: purple-tinted rectangle on the right side,
     * overlapping the panel area. */
    int32_t aw = 200, ah = 120;
    int32_t ax = ui->panel_x + (ui->panel_w - aw) / 2;
    int32_t ay = ui->panel_y + (ui->panel_h - ah) / 2;
    if (ax < 0) ax = 0;
    if (ay < 0) ay = 0;
    ui_fill_rect(s, ax, ay, aw, ah, O0_ASSISTANT_COLOR);
    /* Purple assistant sphere in the panel. */
    o0_surface_fill_circle(s, ax + aw / 2, ay + ah / 2, 20, O0_SPHERE_PURPLE);
}

static void ui_draw_focus_ring(struct o0_desktop_ui *ui,
                               struct o0_surface *s) {
    if (ui->focus_index >= ui->sphere_count) return;
    const struct o0_sphere *sph = &ui->spheres[ui->focus_index];
    if (!sph->active) return;

    uint32_t ring_color = color_brighten(sph->color, 500);
    int32_t ring_r = sph->anim_radius + 10;
    /* Pulsing focus ring: radius varies slightly. */
    int32_t pulse = (int32_t)((ui->frame % 30) * 2 / 30);
    o0_surface_draw_circle(s, sph->cx, sph->cy, ring_r + pulse, ring_color);
}

static void ui_draw_cursor(struct o0_desktop_ui *ui, struct o0_surface *s) {
    int32_t cx = ui->pointer_x;
    int32_t cy = ui->pointer_y;
    uint32_t frame = ui->frame;

    /* Clamp cursor to screen bounds. */
    if (cx < 0) cx = 0;
    if (cx >= (int32_t)s->width) cx = (int32_t)s->width - 1;
    if (cy < 0) cy = 0;
    if (cy >= (int32_t)s->height) cy = (int32_t)s->height - 1;

    /* Outer glow: bright neon green, pulses slowly. */
    uint32_t glow = (frame % 40 < 20) ? O0_SPHERE_GREEN_BRIGHT : O0_SPHERE_GREEN;
    o0_surface_fill_circle(s, cx, cy, 10, glow);

    /* Mid ring: solid bright green. */
    o0_surface_draw_circle(s, cx, cy, 7, O0_SPHERE_GREEN);

    /* Cursor core: white filled circle. */
    o0_surface_fill_circle(s, cx, cy, 3, 0xFFFFFFu);

    /* Arrow shaft pointing up-right (thick, bright white). */
    for (int32_t i = 1; i <= 14; ++i) {
        for (int32_t dj = -1; dj <= 1; ++dj) {
            int32_t px = cx + i;
            int32_t py = cy - i + dj;
            if (px >= 0 && px < (int32_t)s->width && py >= 0 &&
                py < (int32_t)s->height) {
                o0_surface_put_pixel(s, px, py, 0xFFFFFFu);
            }
        }
    }

    /* Arrow head (filled triangle tip). */
    for (int32_t i = 12; i <= 16; ++i) {
        for (int32_t dj = -2; dj <= 2; ++dj) {
            int32_t px = cx + i;
            int32_t py = cy - i + dj;
            if (px >= 0 && px < (int32_t)s->width && py >= 0 &&
                py < (int32_t)s->height) {
                o0_surface_put_pixel(s, px, py, 0xFFFFFFu);
            }
        }
    }

    /* Black outline on right/bottom edge of arrow for contrast. */
    for (int32_t i = 1; i <= 16; ++i) {
        int32_t px = cx + i + 1;
        int32_t py = cy - i;
        if (px >= 0 && px < (int32_t)s->width && py >= 0 &&
            py < (int32_t)s->height) {
            o0_surface_put_pixel(s, px, py, 0x000000u);
        }
        px = cx + i;
        py = cy - i - 1;
        if (px >= 0 && px < (int32_t)s->width && py >= 0 &&
            py < (int32_t)s->height) {
            o0_surface_put_pixel(s, px, py, 0x000000u);
        }
    }
}

/* ---- main render ---- */

o0_status o0_desktop_ui_render(struct o0_desktop_ui *ui,
                                struct o0_surface *backbuffer) {
    if (ui == NULL || backbuffer == NULL) {
        return O0_EINVAL;
    }

    /* 1. Background: black. */
    o0_surface_fill(backbuffer, O0_DESKTOP_BG_COLOR);

    /* 2. UI area bases (taskbar, panel, tray) drawn first so spheres and
     * particles sit on top. */
    ui_draw_taskbar(ui, backbuffer);
    ui_draw_panel(ui, backbuffer);
    ui_draw_tray(ui, backbuffer);

    /* 3. Particles (drawn before spheres so spheres overlay them). */
    ui_draw_particles(ui, backbuffer);

    /* 4. Node network between desktop spheres. */
    for (uint32_t i = 0; i < ui->node_count; ++i) {
        ui_draw_node(ui, backbuffer, &ui->nodes[i]);
    }

    /* 5. Spheres (desktop + taskbar) with glow rings. */
    for (uint32_t i = 0; i < ui->sphere_count; ++i) {
        ui_draw_sphere(backbuffer, &ui->spheres[i]);
    }

    /* 6. Focus ring around the keyboard-focused sphere. */
    ui_draw_focus_ring(ui, backbuffer);

    /* 7. Assistant panel (if open). */
    ui_draw_assistant(ui, backbuffer);

    /* 8. Mouse cursor on top of everything. */
    ui_draw_cursor(ui, backbuffer);

    return O0_OK;
}

/* ---- expose the global instance for the desktop thread ---- */

struct o0_desktop_ui *o0_desktop_ui_global(void) {
    return &g_ui;
}

/* Resolve a pointer click against the live animated hit radius. */
o0_status o0_desktop_ui_activate_at(struct o0_desktop_ui *ui, int32_t x, int32_t y) {
    if (ui == NULL) return O0_EINVAL;
    for (uint32_t i = 0; i < ui->sphere_count; ++i) {
        const struct o0_sphere *s = &ui->spheres[i];
        if (s->active && sphere_hit(s, x, y, s->radius / 2))
            return o0_desktop_ui_activate(ui, i);
    }
    return O0_ENOENT;
}
