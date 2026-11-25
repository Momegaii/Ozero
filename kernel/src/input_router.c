/* Stage 6.8: input router from queue to window manager. */
#include "ozero/input_router.h"
#include "ozero/input.h"
#include "ozero/window.h"
#include "ozero/shell.h"

void o0_input_router_init(struct o0_input_router *router,
                          struct o0_input_queue *queue,
                          struct o0_window_manager *wm,
                          struct o0_shell *shell) {
    if (!router) return;
    router->queue = queue;
    router->wm = wm;
    router->shell = shell;
}

static void deliver_key_event(struct o0_input_router *router, const struct o0_input_event *ev) {
    if (!router->wm) return;

    /* Find focused window */
    for (uint32_t i = 0; i < router->wm->count; i++) {
        struct o0_window *win = &router->wm->windows[i];
        if (win->focused && win->event_fn) {
            win->event_fn(win, ev, win->event_ctx);
            return;
        }
    }
}

static void deliver_pointer_event(struct o0_input_router *router, const struct o0_input_event *ev) {
    if (!router->shell || !router->wm) return;

    /* Update pointer position */
    o0_shell_pointer(router->shell, ev->dx, ev->dy);

    /* Check for left button press to change focus */
    bool left_down = (ev->buttons & 1u) != 0;

    /* Find window under pointer */
    struct o0_window *target = o0_shell_pointer_target(router->shell);

    if (left_down && target) {
        /* Focus the clicked window */
        o0_wm_focus(router->wm, target->id);
    }

    /* Deliver pointer event to window under cursor */
    if (target && target->event_fn) {
        target->event_fn(target, ev, target->event_ctx);
    }
}

uint32_t o0_input_router_poll(struct o0_input_router *router) {
    if (!router || !router->queue) return 0;

    uint32_t processed = 0;
    struct o0_input_event ev;

    while (o0_input_pop(router->queue, &ev)) {
        processed++;
        if (ev.type == O0_INPUT_KEY) {
            deliver_key_event(router, &ev);
        } else if (ev.type == O0_INPUT_POINTER) {
            deliver_pointer_event(router, &ev);
        }
    }

    return processed;
}