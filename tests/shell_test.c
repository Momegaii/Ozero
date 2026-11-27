#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ozero/shell.h"
#include "ozero/window.h"
#include "ozero/surface.h"
#include "ozero/status.h"

static uint32_t *alloc_surface_buf(uint32_t width, uint32_t height) {
    uint32_t *buf = (uint32_t *)calloc(width * height, sizeof(uint32_t));
    assert(buf != NULL);
    return buf;
}

static struct o0_surface make_surface(uint32_t *buf, uint32_t w, uint32_t h) {
    struct o0_surface s;
    o0_status st = o0_surface_init(&s, buf, w, h, w * 4, O0_PIXEL_RGBX8888);
    assert(st == O0_OK);
    return s;
}

static void test_shell_init(void) {
    struct o0_shell shell;
    o0_status st = o0_shell_init(&shell, 800, 600);
    assert(st == O0_OK);
    assert(shell.width == 800);
    assert(shell.height == 600);
    assert(shell.background == 0x18202b);
    assert(shell.accent == 0x4b91d1);
    assert(shell.pointer_x == 400);
    assert(shell.pointer_y == 300);
    assert(shell.initialized == true);
    assert(shell.windows.count == 0);

    /* Invalid args */
    st = o0_shell_init(NULL, 800, 600);
    assert(st == O0_EINVAL);
    st = o0_shell_init(&shell, 0, 600);
    assert(st == O0_EINVAL);
    st = o0_shell_init(&shell, 800, 0);
    assert(st == O0_EINVAL);

    puts("shell_test: shell_init passed");
}

static void test_pointer_clamping(void) {
    struct o0_shell shell;
    o0_shell_init(&shell, 800, 600);

    /* Start at center */
    assert(shell.pointer_x == 400);
    assert(shell.pointer_y == 300);

    /* Move within bounds */
    o0_shell_pointer(&shell, 100, 50);
    assert(shell.pointer_x == 500);
    assert(shell.pointer_y == 350);

    /* Move to left edge */
    o0_shell_pointer(&shell, -500, 0);
    assert(shell.pointer_x == 0);
    assert(shell.pointer_y == 350);

    /* Move to right edge */
    o0_shell_pointer(&shell, 1000, 0);
    assert(shell.pointer_x == 799);
    assert(shell.pointer_y == 350);

    /* Move to top edge */
    o0_shell_pointer(&shell, 0, -500);
    assert(shell.pointer_x == 799);
    assert(shell.pointer_y == 0);

    /* Move to bottom edge */
    o0_shell_pointer(&shell, 0, 1000);
    assert(shell.pointer_x == 799);
    assert(shell.pointer_y == 599);

    /* Test uninitialized shell */
    struct o0_shell uninit = {0};
    o0_shell_pointer(&uninit, 10, 10);
    /* Should not crash, just return early */

    puts("shell_test: pointer_clamping passed");
}

static void test_pointer_target(void) {
    struct o0_shell shell;
    o0_shell_init(&shell, 800, 600);

    /* No windows */
    assert(o0_shell_pointer_target(&shell) == NULL);

    /* Add a window */
    uint32_t *buf1 = alloc_surface_buf(100, 100);
    struct o0_surface s1 = make_surface(buf1, 100, 100);
    o0_surface_fill(&s1, 0xFF0000);
    o0_window_id id1;
    o0_status st = o0_wm_create(&shell.windows, &s1, 100, 100, &id1);
    assert(st == O0_OK);

    /* Move pointer to window */
    shell.pointer_x = 150;
    shell.pointer_y = 150;
    struct o0_window *target = o0_shell_pointer_target(&shell);
    assert(target != NULL);
    assert(target->id == id1);

    /* Move pointer outside window */
    shell.pointer_x = 50;
    shell.pointer_y = 50;
    target = o0_shell_pointer_target(&shell);
    assert(target == NULL);

    /* Add overlapping window on top */
    uint32_t *buf2 = alloc_surface_buf(100, 100);
    struct o0_surface s2 = make_surface(buf2, 100, 100);
    o0_surface_fill(&s2, 0x00FF00);
    o0_window_id id2;
    st = o0_wm_create(&shell.windows, &s2, 150, 150, &id2);
    assert(st == O0_OK);

    /* Pointer at overlap - should hit top window */
    shell.pointer_x = 175;
    shell.pointer_y = 175;
    target = o0_shell_pointer_target(&shell);
    assert(target != NULL);
    assert(target->id == id2);

    /* Move top window away */
    o0_wm_move(&shell.windows, id2, 300, 300);
    target = o0_shell_pointer_target(&shell);
    assert(target != NULL);
    assert(target->id == id1);

    /* Test invisible window */
    shell.windows.windows[1].visible = false;
    shell.pointer_x = 175;
    shell.pointer_y = 175;
    target = o0_shell_pointer_target(&shell);
    assert(target != NULL);
    assert(target->id == id1);

    /* Test uninitialized shell */
    struct o0_shell uninit = {0};
    assert(o0_shell_pointer_target(&uninit) == NULL);

    free(buf1);
    free(buf2);
    puts("shell_test: pointer_target passed");
}

static void test_window_management_through_shell(void) {
    struct o0_shell shell;
    o0_shell_init(&shell, 800, 600);

    /* Create window through window manager */
    uint32_t *buf1 = alloc_surface_buf(200, 200);
    struct o0_surface s1 = make_surface(buf1, 200, 200);
    o0_surface_fill(&s1, 0xFF0000);
    o0_window_id id1;
    o0_status st = o0_wm_create(&shell.windows, &s1, 100, 100, &id1);
    assert(st == O0_OK);
    assert(shell.windows.count == 1);

    /* Create another window */
    uint32_t *buf2 = alloc_surface_buf(200, 200);
    struct o0_surface s2 = make_surface(buf2, 200, 200);
    o0_surface_fill(&s2, 0x00FF00);
    o0_window_id id2;
    st = o0_wm_create(&shell.windows, &s2, 300, 300, &id2);
    assert(st == O0_OK);
    assert(shell.windows.count == 2);

    /* Focus first window */
    st = o0_wm_focus(&shell.windows, id1);
    assert(st == O0_OK);
    assert(shell.windows.focused == id1);

    /* Destroy window */
    st = o0_wm_destroy(&shell.windows, id1);
    assert(st == O0_OK);
    assert(shell.windows.count == 1);
    assert(shell.windows.focused == id2); /* focus falls back to remaining visible window */

    free(buf1);
    free(buf2);
    puts("shell_test: window_management passed");
}

static void test_shell_colors(void) {
    struct o0_shell shell;
    o0_shell_init(&shell, 800, 600);

    /* Check default colors match expected values */
    assert(shell.background == 0x18202b);
    assert(shell.accent == 0x4b91d1);

    /* Colors should be modifiable */
    shell.background = 0x000000;
    shell.accent = 0xFFFFFF;
    assert(shell.background == 0x000000);
    assert(shell.accent == 0xFFFFFF);

    puts("shell_test: shell_colors passed");
}

static void test_null_shell(void) {
    /* All functions should handle NULL gracefully */
    o0_shell_pointer(NULL, 10, 10);
    assert(o0_shell_pointer_target(NULL) == NULL);

    puts("shell_test: null_shell passed");
}


static uint32_t routed_key_events;
static bool routed_window_event(struct o0_window *win,const struct o0_input_event *ev,void *ctx) {
    (void)win; (void)ctx;
    if(ev->type==O0_INPUT_KEY)++routed_key_events;
    return true;
}
static void push_pointer(struct o0_input_queue *q,int16_t dx,int16_t dy,uint8_t buttons) {
    struct o0_input_event ev={.type=O0_INPUT_POINTER,.dx=dx,.dy=dy,.buttons=buttons};
    assert(o0_input_push(q,&ev)==O0_OK);
}
static void test_pointer_drag_resize_and_keyboard_routing(void) {
    struct o0_shell shell;
    struct o0_input_queue queue;
    o0_input_init(&queue);
    assert(o0_shell_init(&shell,800,600)==O0_OK);
    uint32_t *pixels=alloc_surface_buf(200,200);
    struct o0_surface surface=make_surface(pixels,200,200);
    o0_window_id id;
    assert(o0_wm_create(&shell.windows,&surface,100,100,&id)==O0_OK);
    struct o0_window *win=&shell.windows.windows[0];
    o0_window_set_event_handler(win,routed_window_event,NULL);
    o0_shell_set_input_queue(&shell,&queue);

    /* Move to title bar, press, drag, and release. */
    push_pointer(&queue,-250,-180,0);
    push_pointer(&queue,0,0,1);
    push_pointer(&queue,30,40,1);
    push_pointer(&queue,0,0,0);
    assert(o0_shell_poll_input(&shell)==4);
    assert(shell.windows.windows[0].x==130);
    assert(shell.windows.windows[0].y==140);

    /* Grab the bottom-right resize grip and shrink the window. */
    push_pointer(&queue,149,179,0);
    push_pointer(&queue,0,0,1);
    push_pointer(&queue,-50,-60,1);
    push_pointer(&queue,0,0,0);
    assert(o0_shell_poll_input(&shell)==4);
    assert(shell.windows.windows[0].width==150);
    assert(shell.windows.windows[0].height==140);

    struct o0_input_event key={.type=O0_INPUT_KEY,.pressed=1,.code=0x1c};
    assert(o0_input_push(&queue,&key)==O0_OK);
    assert(o0_shell_poll_input(&shell)==1);
    assert(routed_key_events==1);
    free(pixels);
    puts("shell_test: pointer drag/resize and keyboard routing passed");
}

int main(void) {
    test_shell_init();
    test_pointer_clamping();
    test_pointer_target();
    test_window_management_through_shell();
    test_shell_colors();
    test_null_shell();
    test_pointer_drag_resize_and_keyboard_routing();
    puts("shell_test: all checks passed");
    return 0;
}