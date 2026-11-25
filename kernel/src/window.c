/* Stage 6.4: compact z-order registry; array order is back-to-front. */
#include "ozero/window.h"
#include "ozero/input.h"

static int find(struct o0_window_manager *w,o0_window_id id) {
    for(uint32_t i=0;i<w->count;i++) if(w->windows[i].id==id) return (int)i;
    return -1;
}
void o0_wm_init(struct o0_window_manager *w) {
    if(!w) return;
    w->count=0; w->next_id=1; w->focused=0;
}
o0_status o0_wm_create(struct o0_window_manager *w,struct o0_surface *s,int32_t x,int32_t y,o0_window_id *out) {
    if(!w||!s||!out||!s->pixels||!s->width||!s->height||s->width>UINT32_MAX||s->height>UINT32_MAX) return O0_EINVAL;
    if(w->count==O0_WINDOW_MAX) return O0_ENOMEM;
    o0_window_id id=w->next_id++;
    if(!id) id=w->next_id++;
    struct o0_window *v=&w->windows[w->count++];
    *v=(struct o0_window){.id=id,.surface=s,.x=x,.y=y,.width=(uint32_t)s->width,.height=(uint32_t)s->height,.visible=true,.focused=false,.event_fn=0,.event_ctx=0};
    *out=id;
    return o0_wm_focus(w,id);
}
o0_status o0_wm_destroy(struct o0_window_manager *w,o0_window_id id) {
    if(!w) return O0_EINVAL;
    int i=find(w,id);
    if(i<0) return O0_ENOENT;
    for(uint32_t n=(uint32_t)i;n+1<w->count;n++) w->windows[n]=w->windows[n+1];
    --w->count;
    if(w->focused==id) w->focused=0;
    return O0_OK;
}
o0_status o0_wm_move(struct o0_window_manager *w,o0_window_id id,int32_t x,int32_t y) {
    if(!w) return O0_EINVAL;
    int i=find(w,id);
    if(i<0) return O0_ENOENT;
    w->windows[i].x=x; w->windows[i].y=y;
    return O0_OK;
}
o0_status o0_wm_focus(struct o0_window_manager *w,o0_window_id id) {
    if(!w) return O0_EINVAL;
    int i=find(w,id);
    if(i<0) return O0_ENOENT;
    for(uint32_t n=0;n<w->count;n++) w->windows[n].focused=false;
    struct o0_window v=w->windows[i];
    for(uint32_t n=(uint32_t)i;n+1<w->count;n++) w->windows[n]=w->windows[n+1];
    w->windows[w->count-1]=v;
    w->windows[w->count-1].focused=true;
    w->focused=id;
    return O0_OK;
}
struct o0_window *o0_wm_hit_test(struct o0_window_manager *w,int32_t x,int32_t y) {
    if(!w) return 0;
    for(uint32_t n=w->count;n>0;n--) {
        struct o0_window *v=&w->windows[n-1];
        if(v->visible && x>=v->x && y>=v->y &&
           (int64_t)x<(int64_t)v->x+v->width && (int64_t)y<(int64_t)v->y+v->height) return v;
    }
    return 0;
}
void o0_window_set_event_handler(struct o0_window *win, o0_window_event_fn fn, void *ctx) {
    if (!win) return;
    win->event_fn = fn;
    win->event_ctx = ctx;
}
