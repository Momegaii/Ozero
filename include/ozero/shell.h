/* Stage 6.7: minimal native desktop shell state and layout. */
#ifndef O0_SHELL_H
#define O0_SHELL_H
#include <stdbool.h>
#include <stdint.h>
#include "ozero/status.h"
#include "ozero/window.h"
#include "ozero/input.h"
#include "ozero/input_router.h"
struct o0_shell {
    struct o0_window_manager windows;
    uint32_t width,height,background,accent;
    int32_t pointer_x,pointer_y;
    bool initialized;
    struct o0_input_router input_router;
};
o0_status o0_shell_init(struct o0_shell *shell,uint32_t width,uint32_t height);
void o0_shell_pointer(struct o0_shell *shell,int32_t dx,int32_t dy);
struct o0_window *o0_shell_pointer_target(struct o0_shell *shell);

/* Process pending input events. Returns number of events processed. */
uint32_t o0_shell_poll_input(struct o0_shell *shell);

/* Connect the shell to an input queue for event routing. */
void o0_shell_set_input_queue(struct o0_shell *shell, struct o0_input_queue *queue);
#endif
