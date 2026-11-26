/* Stage 6.7: custom O0 desktop UI implementation.
 *
 * Software-rendered dark desktop with neon-green sphere launchers,
 * purple assistant spheres, green particles, a node network, a bottom
 * taskbar, a right-side panel, a system tray, and an integrated O0
 * assistant interface. All drawing uses o0_surface_* primitives; no GPU,
 * no external toolkit, no ELF/X11/Wayland assumptions.
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

    /* ---- desktop spheres (launchers above the taskbar) ---- */
    int32_t sx = (int32_t)(width / 6);
    int32_t sy = (int32_t)(height / 4);
    int32_t sr = 24;
    static const uint8_t sphere_types[] = {
        O0_SPHERE_LAUNCHER, O0_SPHERE_LAUNCHER,
        O0_SPHERE_LAUNCHER, O0_SPHERE_ASSISTANT,
    };
    static const int32_t sphere_dx[] = {0, 1, 2, 3};
    static const int32_t sphere_dy[] = {0, 1, 0, 1};
    uint32_t n = 0;
    for (uint32_t i = 0; i < 4 && n < O0_DESKTOP_MAX_SPHERES; ++i) {
        struct o0_sphere *s = &ui->spheres[n++];
        s->cx = sx + sphere_dx[i] * (sr * 3);
        s->cy = sy + sphere_dy[i] * (sr * 3);
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

    /* ---- taskbar spheres (along the bottom) ---- */
    int32_t tb_x = (int32_t)(width / 4);
    int32_t tb_y = ui->taskbar_y + ui->taskbar_h / 2;
    int32_t tb_r = 16;
    static const uint8_t tb_types[] = {
        O0_SPHERE_LAUNCHER, O0_SPHERE_LAUNCHER,
        O0_SPHERE_LAUNCHER, O0_SPHERE_ASSISTANT,
    };
    for (uint32_t i = 0; i < 4 && n < O0_DESKTOP_MAX_SPHERES; ++i) {
        struct o0_sphere *s = &ui->spheres[n++];
        s->cx = tb_x + (int32_t)i * (tb_r * 4);
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

    /* ---- nodes: connect desktop spheres to neighbours ---- */
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

    /* ---- particles: scattered across the screen ---- */
    uint32_t pn = 0;
    for (uint32_t i = 0; i < 24 && pn < O0_DESKTOP_MAX_PARTICLES; ++i) {
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
            uint8_t *p = s->pixels + (size_t)y0 * s->pitch +
                         (size_t)x0 * 4u;
            pixel_write(p, s->format, line_color);
        }
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -ay) { err -= ay; x0 += sx; }
        if (e2 < ax) { err += ax; y0 += sy; }
    }
}

static void ui_draw_sphere(struct o0_desktop_ui *ui, struct o0_surface *s,
                           const struct o0_sphere *sph) {
    if (!sph->active) return;

    uint32_t sph_color = sph->color;
    if (sph->state == O0_SPHERE_HOVER) {
        sph_color = color_brighten(sph->color, 400); /* ~1.5x brighter */
    }

    /* Solid sphere body. */
    o0_surface_fill_circle(s, sph->cx, sph->cy, sph->anim_radius, sph_color);

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
        /* Alpha by life. Dim for low life, bright for high. */
        uint8_t alpha = (uint8_t)((uint32_t)p->life * 255u / 200u);
        if (alpha > 255u) alpha = 255u;
        uint32_t base = (p->life > 100) ? O0_PARTICLE_BRIGHT : O0_PARTICLE_DIM;
        uint32_t blended = o0_surface_blend_rgb(
            O0_DESKTOP_BG_COLOR, base, alpha);
        uint8_t *px = s->pixels + (size_t)p->y * s->pitch +
                      (size_t)p->x * 4u;
        pixel_write(px, s->format, blended);
    }
}

static void ui_draw_panel(struct o0_desktop_ui *ui, struct o0_surface *s) {
    /* Right-side information panel. */
    ui_fill_rect(s, ui->panel_x, ui->panel_y, ui->panel_w, ui->panel_h,
                 O0_PANEL_COLOR);
    /* Thin border. */
    for (int32_t y = ui->panel_y; y < ui->panel_y + ui->panel_h; ++y) {
        if (y >= 0 && y < (int32_t)s->height) {
            uint8_t *p = s->pixels + (size_t)y * s->pitch +
                         (size_t)ui->panel_x * 4u;
            pixel_write(p, s->format, O0_NODE_BRIGHT);
        }
    }
}

static void ui_draw_taskbar(struct o0_desktop_ui *ui,
                            struct o0_surface *s) {
    ui_fill_rect(s, ui->taskbar_x, ui->taskbar_y, ui->taskbar_w,
                 ui->taskbar_h, O0_TASKBAR_COLOR);
    /* Top edge highlight. */
    for (int32_t x = ui->taskbar_x; x < ui->taskbar_x + ui->taskbar_w; ++x) {
        if (x >= 0 && x < (int32_t)s->width) {
            uint8_t *p = s->pixels + (size_t)ui->taskbar_y * s->pitch +
                         (size_t)x * 4u;
            pixel_write(p, s->format, O0_NODE_BRIGHT);
        }
    }
}

static void ui_draw_tray(struct o0_desktop_ui *ui, struct o0_surface *s) {
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

/* ---- main render ---- */

o0_status o0_desktop_ui_render(struct o0_desktop_ui *ui,
                                struct o0_surface *backbuffer) {
    if (ui == NULL || backbuffer == NULL) {
        return O0_EINVAL;
    }

    /* 1. Background: black. */
    o0_surface_fill(backbuffer, O0_DESKTOP_BG_COLOR);

    /* 2. Particles (drawn first so spheres overlay them). */
    ui_draw_particles(ui, backbuffer);

    /* 3. Node network between desktop spheres. */
    for (uint32_t i = 0; i < ui->node_count; ++i) {
        ui_draw_node(ui, backbuffer, &ui->nodes[i]);
    }

    /* 4. Spheres (desktop + taskbar). */
    for (uint32_t i = 0; i < ui->sphere_count; ++i) {
        ui_draw_sphere(ui, backbuffer, &ui->spheres[i]);
    }

    /* 5. Taskbar strip. */
    ui_draw_taskbar(ui, backbuffer);

    /* 6. Right-side panel. */
    ui_draw_panel(ui, backbuffer);

    /* 7. System tray. */
    ui_draw_tray(ui, backbuffer);

    /* 8. Assistant panel (if open). */
    ui_draw_assistant(ui, backbuffer);

    return O0_OK;
}

/* ---- expose the global instance for the desktop thread ---- */

struct o0_desktop_ui *o0_desktop_ui_global(void) {
    return &g_ui;
}
