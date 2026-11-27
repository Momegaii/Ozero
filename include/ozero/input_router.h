/* Stage 6.8: input router from queue to window manager. */
#ifndef O0_INPUT_ROUTER_H
#define O0_INPUT_ROUTER_H

#include <stdbool.h>
#include <stdint.h>
#include "ozero/input.h"
#include "ozero/window.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Input router state. */
struct o0_input_router {
    struct o0_input_queue *queue;
    struct o0_window_manager *wm;
    struct o0_shell *shell;
};

/* Initialize the input router. */
void o0_input_router_init(struct o0_input_router *router,
                          struct o0_input_queue *queue,
                          struct o0_window_manager *wm,
                          struct o0_shell *shell);

/* Process all pending input events.
 * Returns the number of events processed. */
uint32_t o0_input_router_poll(struct o0_input_router *router);

#ifdef __cplusplus
}
#endif

#endif /* O0_INPUT_ROUTER_H */