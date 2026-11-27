/* Stage 6.5: deterministic back-to-front software composition. */
#include <limits.h>
#include "ozero/compositor.h"
#include "ozero/surface.h"

#define O0_FOCUS_BORDER_COLOR 0x00FF00u   /* neon green focus border */
#define O0_FOCUS_BORDER_WIDTH 2u

static void draw_focus_border(struct o0_surface *dst, const struct o0_window *win) {
    if (!dst || !win || !win->surface) return;

    /* Clip in 64-bit space before narrowing to the surface API's int32_t
     * coordinates. This also handles windows positioned near INT32 limits. */
    int64_t left = win->x;
    int64_t top = win->y;
    int64_t right = left + (int64_t)win->width;
    int64_t bottom = top + (int64_t)win->height;
    int64_t max_x = dst->width < (size_t)INT32_MAX
        ? (int64_t)dst->width : (int64_t)INT32_MAX;
    int64_t max_y = dst->height < (size_t)INT32_MAX
        ? (int64_t)dst->height : (int64_t)INT32_MAX;

    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > max_x) right = max_x;
    if (bottom > max_y) bottom = max_y;
    if (left >= right || top >= bottom) return;

    int32_t x = (int32_t)left;
    int32_t y = (int32_t)top;
    int32_t width = (int32_t)(right - left);
    int32_t height = (int32_t)(bottom - top);
    int32_t border = O0_FOCUS_BORDER_WIDTH;
    if (border > width) border = width;
    if (border > height) border = height;

    /* Draw a continuous two-pixel border, clipped to the destination. */
    (void)o0_surface_fill_rect(dst, x, y, width, border,
                               O0_FOCUS_BORDER_COLOR);
    (void)o0_surface_fill_rect(dst, x, y + height - border, width, border,
                               O0_FOCUS_BORDER_COLOR);
    (void)o0_surface_fill_rect(dst, x, y, border, height,
                               O0_FOCUS_BORDER_COLOR);
    (void)o0_surface_fill_rect(dst, x + width - border, y, border, height,
                               O0_FOCUS_BORDER_COLOR);
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
