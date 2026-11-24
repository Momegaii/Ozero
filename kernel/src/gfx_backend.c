/* Stage 6.6: software backend is the mandatory baseline. */
#include "ozero/gfx_backend.h"
static o0_status sw_fill(struct o0_surface *s,uint32_t c){return o0_surface_fill(s,c);}
static o0_status sw_blit(struct o0_surface *d,int32_t x,int32_t y,const struct o0_surface *s,int32_t sx,int32_t sy,int32_t w,int32_t h){return o0_surface_blit(d,x,y,s,sx,sy,w,h);}
static const struct o0_gfx_backend software={"software",O0_GFX_FEATURE_FILL|O0_GFX_FEATURE_BLIT,sw_fill,sw_blit,false};
const struct o0_gfx_backend *o0_gfx_software_backend(void){return &software;}
o0_status o0_gfx_backend_validate(const struct o0_gfx_backend *b){
    if(!b||!b->name||!b->fill||!b->blit)return O0_EINVAL;
    if(b->hardware && !(b->features&O0_GFX_FEATURE_ACCELERATED))return O0_EINVAL;
    return O0_OK;
}
