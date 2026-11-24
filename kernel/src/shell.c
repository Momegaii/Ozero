/* Stage 6.7: shell policy/state; drawing is delegated to compositor and surfaces. */
#include "ozero/shell.h"
o0_status o0_shell_init(struct o0_shell *s,uint32_t w,uint32_t h){
    if(!s||!w||!h)return O0_EINVAL;
    s->width=w;s->height=h;s->background=0x18202bu;s->accent=0x4b91d1u;
    s->pointer_x=(int32_t)(w/2u);s->pointer_y=(int32_t)(h/2u);
    o0_wm_init(&s->windows);s->initialized=true;return O0_OK;
}
void o0_shell_pointer(struct o0_shell *s,int32_t dx,int32_t dy){
    if(!s||!s->initialized)return;
    int64_t x=(int64_t)s->pointer_x+dx,y=(int64_t)s->pointer_y+dy;
    if(x<0)x=0;
    if(y<0)y=0;
    if(x>=s->width)x=(int64_t)s->width-1;
    if(y>=s->height)y=(int64_t)s->height-1;
    s->pointer_x=(int32_t)x;s->pointer_y=(int32_t)y;
}
struct o0_window *o0_shell_pointer_target(struct o0_shell *s){
    if(!s||!s->initialized)return 0;
    return o0_wm_hit_test(&s->windows,s->pointer_x,s->pointer_y);
}
