/* Stage 6.5: deterministic back-to-front software composition. */
#include <limits.h>
#include "ozero/compositor.h"
#include "ozero/surface.h"

#define O0_FOCUS_BORDER_COLOR 0x00FF00u   /* neon green focus border */
#define O0_FOCUS_BORDER_WIDTH 2u

static void draw_focus_border(struct o0_surface *dst, const struct o0_window *win) {
    if (!dst || !win || !win->surface) return;
    int32_t x = win->x, y = win->y;
    uint32_t w = win->width, h = win->height;
    if (x < 0) { w = (uint32_t)((int64_t)w + x); x = 0; }
    if (y < 0) { h = (uint32_t)((int64_t)h + y); y = 0; }
    if (x + (int32_t)w > (int32_t)dst->width) w = (uint32_t)((int64_t)dst->width - x);
    if (y + (int32_t)h > (int32_t)dst->height) h = (uint32_t)((int64_t)dst->height - y);
    if (w == 0 || h == 0) return;

    uint32_t bw = O0_FOCUS_BORDER_WIDTH;
    /* Top edge. */
    for (uint32_t i = 0; i < w; i += bw) {
        o0_surface_put_pixel(dst, x + (int32_t)i, y, O0_FOCUS_BORDER_COLOR);
    }
    /* Bottom edge. */
    for (uint32_t i = 0; i < w; i += bw) {
        o0_surface_put_pixel(dst, x + (int32_t)i, y + (int32_t)h - 1, O0_FOCUS_BORDER_COLOR);
    }
    /* Left edge. */
    for (uint32_t i = 0; i < h; i += bw) {
        o0_surface_put_pixel(dst, x, y + (int32_t)i, O0_FOCUS_BORDER_COLOR);
    }
    /* Right edge. */
    for (uint32_t i = 0; i < h; i += bw) {
        o0_surface_put_pixel(dst, x + (int32_t)w - 1, y + (int32_t)i, O0_FOCUS_BORDER_COLOR);
    }
}

o0_status o0_compositor_compose_windows(struct o0_surface *dst,const struct o0_window_manager *wm) {
    if(!dst||!wm)return O0_EINVAL;
    for(uint32_t i=0;i<wm->count;i++) {
        const struct o0_window *win=&wm->windows[i];
        if(!win->visible||!win->surface)continue;
        if(win->width>INT32_MAX||win->height>INT32_MAX)continue;
        o0_status st=o0_surface_blit(dst,win->x,win->y,win->surface,0,0,
                                     (int32_t)win->width,(int32_t)win->height);
        if(st!=O0_OK)return st;
        /* Draw a focus border around the focused window. */
        if (wm->focused == win->id) {
            draw_focus_border(dst, win);
        }
    }
    return O0_OK;
}
o0_status o0_compositor_compose(struct o0_surface *dst,const struct o0_window_manager *wm,uint32_t bg) {
    if(!dst||!wm)return O0_EINVAL;
    o0_status st=o0_surface_fill(dst,bg);
    if(st!=O0_OK)return st;
    return o0_compositor_compose_windows(dst,wm);
}
