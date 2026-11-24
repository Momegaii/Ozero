/* Stage 6.6: capability-driven renderer boundary. */
#ifndef O0_GFX_BACKEND_H
#define O0_GFX_BACKEND_H
#include <stdbool.h>
#include <stdint.h>
#include "ozero/status.h"
#include "ozero/surface.h"
enum o0_gfx_feature { O0_GFX_FEATURE_FILL=1u, O0_GFX_FEATURE_BLIT=2u, O0_GFX_FEATURE_ACCELERATED=4u };
struct o0_gfx_backend {
    const char *name;
    uint32_t features;
    o0_status (*fill)(struct o0_surface *,uint32_t);
    o0_status (*blit)(struct o0_surface *,int32_t,int32_t,const struct o0_surface *,int32_t,int32_t,int32_t,int32_t);
    bool hardware;
};
const struct o0_gfx_backend *o0_gfx_software_backend(void);
o0_status o0_gfx_backend_validate(const struct o0_gfx_backend *backend);
#endif
