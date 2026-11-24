/* Stage 6.5: deterministic back-to-front software composition. */
#include "ozero/compositor.h"
o0_status o0_compositor_compose(struct o0_surface *dst,const struct o0_window_manager *wm,uint32_t bg) {
    if(!dst||!wm)return O0_EINVAL;
    o0_status st=o0_surface_fill(dst,bg); if(st!=O0_OK)return st;
    for(uint32_t i=0;i<wm->count;i++){
        const struct o0_window *w=&wm->windows[i];
        if(!w->visible||!w->surface)continue;
        st=o0_surface_blit(dst,w->x,w->y,w->surface,0,0,
            w->width>INT32_MAX?INT32_MAX:(int32_t)w->width,
            w->height>INT32_MAX?INT32_MAX:(int32_t)w->height);
        if(st!=O0_OK)return st;
    }
    return O0_OK;
}
