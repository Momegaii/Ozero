/* Stage 6.4: bounded z-order registry and geometry operations. */
#include "ozero/window.h"
#include "ozero/input.h"

static int find(struct o0_window_manager *wm,o0_window_id id) {
    for(uint32_t i=0;i<wm->count;i++) if(wm->windows[i].id==id) return (int)i;
    return -1;
}
static void focus_top_visible(struct o0_window_manager *wm) {
    wm->focused=0;
    for(uint32_t i=wm->count;i>0;i--) {
        struct o0_window *win=&wm->windows[i-1];
        win->focused=false;
        if(win->visible && wm->focused==0) {
            win->focused=true;
            wm->focused=win->id;
        }
    }
}
void o0_wm_init(struct o0_window_manager *wm) {
    if(!wm)return;
    wm->count=0; wm->next_id=1; wm->focused=0;
}
o0_status o0_wm_create(struct o0_window_manager *wm,struct o0_surface *surface,int32_t x,int32_t y,o0_window_id *out) {
    if(!wm||!surface||!out||!surface->pixels||!surface->width||!surface->height||
       surface->width>UINT32_MAX||surface->height>UINT32_MAX) return O0_EINVAL;
    if(wm->count==O0_WINDOW_MAX) return O0_ENOMEM;
    o0_window_id id=wm->next_id++;
    if(!id) id=wm->next_id++;
    struct o0_window *win=&wm->windows[wm->count++];
    *win=(struct o0_window){.id=id,.surface=surface,.x=x,.y=y,
        .width=(uint32_t)surface->width,.height=(uint32_t)surface->height,
        .visible=true,.focused=false,.event_fn=0,.event_ctx=0};
    *out=id;
    return o0_wm_focus(wm,id);
}
o0_status o0_wm_destroy(struct o0_window_manager *wm,o0_window_id id) {
    if(!wm)return O0_EINVAL;
    int i=find(wm,id);
    if(i<0)return O0_ENOENT;
    bool was_focused=wm->focused==id;
    for(uint32_t n=(uint32_t)i;n+1<wm->count;n++)wm->windows[n]=wm->windows[n+1];
    --wm->count;
    if(was_focused)focus_top_visible(wm);
    return O0_OK;
}
o0_status o0_wm_move(struct o0_window_manager *wm,o0_window_id id,int32_t x,int32_t y) {
    if(!wm)return O0_EINVAL;
    int i=find(wm,id);
    if(i<0)return O0_ENOENT;
    wm->windows[i].x=x; wm->windows[i].y=y;
    return O0_OK;
}
o0_status o0_wm_resize(struct o0_window_manager *wm,o0_window_id id,uint32_t width,uint32_t height) {
    if(!wm||width<32u||height<32u)return O0_EINVAL;
    int i=find(wm,id);
    if(i<0)return O0_ENOENT;
    struct o0_window *win=&wm->windows[i];
    if(!win->surface||width>win->surface->width||height>win->surface->height)return O0_ERANGE;
    win->width=width; win->height=height;
    return O0_OK;
}
o0_status o0_wm_set_visible(struct o0_window_manager *wm,o0_window_id id,bool visible) {
    if(!wm)return O0_EINVAL;
    int i=find(wm,id);
    if(i<0)return O0_ENOENT;
    wm->windows[i].visible=visible;
    if(!visible && wm->focused==id)focus_top_visible(wm);
    else if(visible && wm->focused==0)return o0_wm_focus(wm,id);
    return O0_OK;
}
o0_status o0_wm_focus(struct o0_window_manager *wm,o0_window_id id) {
    if(!wm)return O0_EINVAL;
    int i=find(wm,id);
    if(i<0)return O0_ENOENT;
    if(!wm->windows[i].visible)return O0_EINVAL;
    for(uint32_t n=0;n<wm->count;n++)wm->windows[n].focused=false;
    struct o0_window win=wm->windows[i];
    for(uint32_t n=(uint32_t)i;n+1<wm->count;n++)wm->windows[n]=wm->windows[n+1];
    wm->windows[wm->count-1]=win;
    wm->windows[wm->count-1].focused=true;
    wm->focused=id;
    return O0_OK;
}
struct o0_window *o0_wm_hit_test(struct o0_window_manager *wm,int32_t x,int32_t y) {
    if(!wm)return 0;
    for(uint32_t n=wm->count;n>0;n--) {
        struct o0_window *win=&wm->windows[n-1];
        if(win->visible&&x>=win->x&&y>=win->y&&
           (int64_t)x<(int64_t)win->x+win->width&&
           (int64_t)y<(int64_t)win->y+win->height)return win;
    }
    return 0;
}
void o0_window_set_event_handler(struct o0_window *win,o0_window_event_fn fn,void *ctx) {
    if(!win)return;
    win->event_fn=fn; win->event_ctx=ctx;
}
