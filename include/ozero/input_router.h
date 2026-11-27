/* Stage 6.8: input routing and pointer-driven window management. */
#ifndef O0_INPUT_ROUTER_H
#define O0_INPUT_ROUTER_H
#include <stdbool.h>
#include <stdint.h>
#include "ozero/input.h"
#include "ozero/window.h"
#ifdef __cplusplus
extern "C" {
#endif
struct o0_shell;
struct o0_input_router {
    struct o0_input_queue *queue;
    struct o0_window_manager *wm;
    struct o0_shell *shell;
    o0_window_id pointer_window;
    int32_t drag_pointer_x,drag_pointer_y;
    int32_t drag_window_x,drag_window_y;
    uint8_t last_buttons;
    uint8_t drag_mode; /* 0 idle, 1 move, 2 resize */
};
void o0_input_router_init(struct o0_input_router *router,struct o0_input_queue *queue,
                          struct o0_window_manager *wm,struct o0_shell *shell);
uint32_t o0_input_router_poll(struct o0_input_router *router);
#ifdef __cplusplus
}
#endif
#endif
