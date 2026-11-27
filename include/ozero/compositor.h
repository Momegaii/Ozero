/* Stage 6.5: software compositor. */
#ifndef O0_COMPOSITOR_H
#define O0_COMPOSITOR_H
#include "ozero/window.h"
o0_status o0_compositor_compose(struct o0_surface *target,const struct o0_window_manager *wm,uint32_t background);
/* Composite windows over an already-rendered desktop without clearing it. */
o0_status o0_compositor_compose_windows(struct o0_surface *target,const struct o0_window_manager *wm);
#endif
