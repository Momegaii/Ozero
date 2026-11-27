/* Stage 6.7: custom O0 desktop UI.
 *
 * A dark, minimal 2D desktop rendered in software onto the backbuffer.
 * Spheres are solid color with no pictograms: neon green for application
 * launchers, purple for AI/special functions (O0 assistant). Subtle green
 * particles drift on a black background, connected by a node network of
 * thin lines between spheres. A bottom taskbar, right-side information
 * panel, system tray, and integrated O0 assistant interface complete the
 * shell.
 *
 * All drawing uses o0_surface_* primitives; no GPU, no external toolkit,
 * no ELF/X11/Wayland assumptions. Animation is frame-driven (16ms budget
 * from desktop_main), using integer math only.
 *
 * Ozero defines its own native execution architecture and does not default
 * to Linux conventions.
 */
#ifndef O0_DESKTOP_UI_H
#define O0_DESKTOP_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "ozero/shell.h"
#include "ozero/surface.h"
#include "ozero/status.h"
#include "ozero/types.h"

/* ---- sphere colors (Ozero-native, RGBX8888 packed as 0xRRGGBB) ---- */

#define O0_DESKTOP_BG_COLOR   0x000000u   /* black */
#define O0_SPHERE_GREEN       0x00FF00u   /* neon green launcher */
#define O0_SPHERE_PURPLE      0x800080u   /* purple AI/special */
#define O0_SPHERE_GREEN_BRIGHT 0x33FFFFu  /* brighter neon for hover */
#define O0_PARTICLE_DIM       0x002000u   /* faint green particle */
#define O0_PARTICLE_BRIGHT    0x008000u   /* brighter green particle */
#define O0_NODE_DIM           0x001000u   /* faint node line */
#define O0_NODE_BRIGHT        0x00FF00u   /* bright node line */
#define O0_PANEL_COLOR        0x0A0A0Au   /* near-black panel */
#define O0_TASKBAR_COLOR      0x050505u   /* near-black taskbar */
#define O0_TRAY_COLOR         0x080808u   /* system tray */
#define O0_ASSISTANT_COLOR    0x800080u   /* assistant panel accent */

/* ---- cursor colors ---- */

#define O0_CURSOR_GLOW        0x00FF00u   /* neon green cursor glow */
#define O0_CURSOR_CORE        0xFFFFFFu   /* white cursor core */
#define O0_CURSOR_OUTLINE     0x000000u   /* black cursor outline */

/* ---- sphere states ---- */

#define O0_SPHERE_IDLE      0u
#define O0_SPHERE_HOVER     1u
#define O0_SPHERE_ACTIVE    2u
#define O0_SPHERE_FEEDBACK  3u

/* ---- sphere types ---- */

#define O0_SPHERE_LAUNCHER  0u   /* application launcher */
#define O0_SPHERE_ASSISTANT 1u   /* O0 assistant (purple) */

/* ---- cursor types ---- */

#define O0_CURSOR_ARROW     0u   /* default selection cursor */
#define O0_CURSOR_BUSY      1u   /* spinning busy cursor */
#define O0_CURSOR_IBEAM     2u   /* text insertion cursor */

/* ---- limits ---- */

#define O0_DESKTOP_MAX_SPHERES 16u
#define O0_DESKTOP_MAX_PARTICLES 64u
#define O0_DESKTOP_MAX_NODES  32u

/* A sphere launcher or assistant. Solid color, no pictogram inside. */
struct o0_sphere {
    int32_t cx, cy;           /* centre position */
    int32_t radius;           /* base radius */
    int32_t anim_radius;      /* current animated radius */
    uint32_t color;           /* base color (neon green or purple) */
    uint8_t type;             /* O0_SPHERE_LAUNCHER or O0_SPHERE_ASSISTANT */
    uint8_t state;            /* O0_SPHERE_* */
    uint32_t anim_phase;      /* animation counter */
    bool active;              /* whether this sphere is in use */
    uint8_t reserved[2];
};

/* A drifting green particle. */
struct o0_particle {
    int32_t x, y;             /* position */
    int32_t vx, vy;           /* velocity (sub-pixel, fixed-point * 16) */
    uint8_t life;             /* 0..255, fades to 0 */
    bool active;
    uint8_t reserved[3];
};

/* A node link between two sphere indices. */
struct o0_node {
    uint32_t a, b;            /* sphere indices */
    uint8_t brightness;       /* 0..255 */
    bool active;
};

/* The desktop UI state. Allocated once at init; no dynamic allocation
 * during the render loop. */
struct o0_desktop_ui {
    struct o0_sphere spheres[O0_DESKTOP_MAX_SPHERES];
    uint32_t sphere_count;
    struct o0_particle particles[O0_DESKTOP_MAX_PARTICLES];
    uint32_t particle_count;
    struct o0_node nodes[O0_DESKTOP_MAX_NODES];
    uint32_t node_count;
    int32_t pointer_x, pointer_y;
    uint32_t frame;
    uint32_t width, height;
    /* UI area rects */
    int32_t taskbar_x, taskbar_y, taskbar_w, taskbar_h;
    int32_t panel_x, panel_y, panel_w, panel_h;
    int32_t tray_x, tray_y, tray_w, tray_h;
    bool assistant_open;
    /* Keyboard focus: index of the currently focused sphere (UINT32_MAX = none).
     * Drawn as a focus ring around the sphere; changed by the input layer. */
    uint32_t focus_index;
    /* Cursor type: O0_CURSOR_ARROW, O0_CURSOR_BUSY, or O0_CURSOR_IBEAM.
     * Changed by the input layer based on what's under the pointer. */
    uint8_t cursor_type;
    uint8_t reserved_cursor[3];
};

/* Initialise the desktop UI for a given screen size. Places spheres,
 * particles, nodes and UI areas deterministically. */
o0_status o0_desktop_ui_init(struct o0_desktop_ui *ui, uint32_t width,
                             uint32_t height);

/* Update animations and input (hover). Call once per frame. */
void o0_desktop_ui_update(struct o0_desktop_ui *ui);

/* Render the full desktop onto the backbuffer surface. */
o0_status o0_desktop_ui_render(struct o0_desktop_ui *ui,
                                struct o0_surface *backbuffer);

/* Set the pointer position (from the shell/input router). */
void o0_desktop_ui_pointer(struct o0_desktop_ui *ui, int32_t x, int32_t y);

/* Activate a sphere by index (click), or at desktop pointer coordinates. */
o0_status o0_desktop_ui_activate(struct o0_desktop_ui *ui, uint32_t index);
o0_status o0_desktop_ui_activate_at(struct o0_desktop_ui *ui, int32_t x, int32_t y);

/* Set the cursor type (arrow/busy/ibeam). */
void o0_desktop_ui_cursor_type(struct o0_desktop_ui *ui, uint8_t type);

#endif /* O0_DESKTOP_UI_H */