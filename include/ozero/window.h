/* Stage 6.4: bounded in-kernel window registry. */
#ifndef O0_WINDOW_H
#define O0_WINDOW_H
#include <stdbool.h>
#include <stdint.h>
#include "ozero/status.h"
#include "ozero/surface.h"
#define O0_WINDOW_MAX 16u
typedef uint32_t o0_window_id;

struct o0_window;  /* forward declaration */

struct o0_input_event;  /* forward declaration from input.h */

/* Per-window event callback.
 * Return true if the event was handled, false to let it propagate. */
typedef bool (*o0_window_event_fn)(struct o0_window *win, const struct o0_input_event *ev, void *ctx);

struct o0_window {
    o0_window_id id;
    struct o0_surface *surface;
    int32_t x,y;
    uint32_t width,height;
    bool visible,focused;
    o0_window_event_fn event_fn;
    void *event_ctx;
};
struct o0_window_manager {
    struct o0_window windows[O0_WINDOW_MAX];
    uint32_t count,next_id,focused;
};
void o0_wm_init(struct o0_window_manager *wm);
o0_status o0_wm_create(struct o0_window_manager *wm, struct o0_surface *surface,
                       int32_t x,int32_t y,o0_window_id *out);
o0_status o0_wm_destroy(struct o0_window_manager *wm,o0_window_id id);
o0_status o0_wm_move(struct o0_window_manager *wm,o0_window_id id,int32_t x,int32_t y);
o0_status o0_wm_focus(struct o0_window_manager *wm,o0_window_id id);
struct o0_window *o0_wm_hit_test(struct o0_window_manager *wm,int32_t x,int32_t y);
void o0_window_set_event_handler(struct o0_window *win, o0_window_event_fn fn, void *ctx);
#endif
