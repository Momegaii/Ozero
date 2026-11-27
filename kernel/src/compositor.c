/* Stage 6.5: deterministic back-to-front software composition. */
#include <limits.h>
#include "ozero/compositor.h"
o0_status o0_compositor_compose_windows(struct o0_surface *dst,const struct o0_window_manager *wm) {
    if(!dst||!wm)return O0_EINVAL;
    for(uint32_t i=0;i<wm->count;i++) {
        const struct o0_window *win=&wm->windows[i];
        if(!win->visible||!win->surface)continue;
        if(win->width>INT32_MAX||win->height>INT32_MAX)continue;
        o0_status st=o0_surface_blit(dst,win->x,win->y,win->surface,0,0,
                                     (int32_t)win->width,(int32_t)win->height);
        if(st!=O0_OK)return st;
    }
    return O0_OK;
}
o0_status o0_compositor_compose(struct o0_surface *dst,const struct o0_window_manager *wm,uint32_t bg) {
    if(!dst||!wm)return O0_EINVAL;
    o0_status st=o0_surface_fill(dst,bg);
    if(st!=O0_OK)return st;
    return o0_compositor_compose_windows(dst,wm);
}
