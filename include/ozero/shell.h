/* Stage 6.7: minimal native desktop shell state and layout. */
#ifndef O0_SHELL_H
#define O0_SHELL_H
#include <stdint.h>
#include "ozero/status.h"
#include "ozero/window.h"
struct o0_shell {
    struct o0_window_manager windows;
    uint32_t width,height,background,accent;
    int32_t pointer_x,pointer_y;
    bool initialized;
};
o0_status o0_shell_init(struct o0_shell *shell,uint32_t width,uint32_t height);
void o0_shell_pointer(struct o0_shell *shell,int32_t dx,int32_t dy);
o0_window *o0_shell_pointer_target(struct o0_shell *shell);
#endif
