/* Stage 6.8: queue-to-window routing, focus, drag and bounded resize. */
#include "ozero/input_router.h"
#include "ozero/window.h"
#include "ozero/shell.h"

void o0_input_router_init(struct o0_input_router *router,struct o0_input_queue *queue,
                          struct o0_window_manager *wm,struct o0_shell *shell) {
    if(!router)return;
    *router=(struct o0_input_router){.queue=queue,.wm=wm,.shell=shell};
}
static struct o0_window *window_by_id(struct o0_window_manager *wm,o0_window_id id) {
    if(!wm||!id)return 0;
    for(uint32_t i=0;i<wm->count;i++)if(wm->windows[i].id==id)return &wm->windows[i];
    return 0;
}
static void deliver_key_event(struct o0_input_router *router,const struct o0_input_event *ev) {
    if(!router->wm)return;
    for(uint32_t i=0;i<router->wm->count;i++) {
        struct o0_window *win=&router->wm->windows[i];
        if(win->focused&&win->visible&&win->event_fn) {
            (void)win->event_fn(win,ev,win->event_ctx);
            return;
        }
    }
}
static void deliver_pointer_event(struct o0_input_router *router,const struct o0_input_event *ev) {
    if(!router->shell||!router->wm)return;
    o0_shell_pointer(router->shell,ev->dx,ev->dy);
    router->shell->pointer_buttons=ev->buttons;
    int32_t px=router->shell->pointer_x,py=router->shell->pointer_y;
    bool left=(ev->buttons&1u)!=0;
    bool pressed=left&&(router->last_buttons&1u)==0;
    bool released=!left&&(router->last_buttons&1u)!=0;
    if(pressed) {
        struct o0_window *target=o0_wm_hit_test(router->wm,px,py);
        router->pointer_window=0;
        router->drag_mode=0;
        if(target) {
            o0_window_id id=target->id;
            (void)o0_wm_focus(router->wm,id);
            target=window_by_id(router->wm,id);
            if(target) {
                router->pointer_window=id;
                router->drag_pointer_x=px; router->drag_pointer_y=py;
                router->drag_window_x=target->x; router->drag_window_y=target->y;
                int64_t right=(int64_t)target->x+target->width;
                int64_t bottom=(int64_t)target->y+target->height;
                if((int64_t)px>=right-16&&(int64_t)py>=bottom-16)router->drag_mode=2;
                else if((int64_t)py<=(int64_t)target->y+24)router->drag_mode=1;
            }
        }
    }
    if(left&&router->drag_mode!=0) {
        struct o0_window *win=window_by_id(router->wm,router->pointer_window);
        if(win) {
            if(router->drag_mode==1) {
                int64_t x=(int64_t)router->drag_window_x+px-router->drag_pointer_x;
                int64_t y=(int64_t)router->drag_window_y+py-router->drag_pointer_y;
                if(x<INT32_MIN)x=INT32_MIN;if(x>INT32_MAX)x=INT32_MAX;
                if(y<INT32_MIN)y=INT32_MIN;if(y>INT32_MAX)y=INT32_MAX;
                (void)o0_wm_move(router->wm,win->id,(int32_t)x,(int32_t)y);
            } else if(router->drag_mode==2) {
                int64_t w=(int64_t)px-win->x+1,h=(int64_t)py-win->y+1;
                if(w<32)w=32;if(h<32)h=32;
                if(w>win->surface->width)w=win->surface->width;
                if(h>win->surface->height)h=win->surface->height;
                (void)o0_wm_resize(router->wm,win->id,(uint32_t)w,(uint32_t)h);
            }
        } else router->drag_mode=0;
    }
    struct o0_window *target=o0_wm_hit_test(router->wm,px,py);
    if(target&&target->event_fn)(void)target->event_fn(target,ev,target->event_ctx);
    if(released){router->drag_mode=0;router->pointer_window=0;}
    router->last_buttons=ev->buttons;
}
uint32_t o0_input_router_poll(struct o0_input_router *router) {
    if(!router||!router->queue)return 0;
    uint32_t processed=0;
    struct o0_input_event ev;
    while(o0_input_pop(router->queue,&ev)) {
        ++processed;
        if(ev.type==O0_INPUT_KEY)deliver_key_event(router,&ev);
        else if(ev.type==O0_INPUT_POINTER)deliver_pointer_event(router,&ev);
    }
    return processed;
}
