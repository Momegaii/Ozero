#include <assert.h>
#include <stdint.h>
#include "ozero/display.h"

static struct o0_display_mode valid_mode(void) {
    return (struct o0_display_mode){
        .address=(volatile void *)(uintptr_t)0x100000,
        .width=800,.height=600,.pitch=3200,.bytes=1920000,
        .bpp=32,.memory_model=1,
        .red_size=8,.red_shift=16,
        .green_size=8,.green_shift=8,
        .blue_size=8,.blue_shift=0
    };
}

int main(void) {
    struct o0_display_mode m=valid_mode();
    assert(o0_display_mode_validate(&m)==O0_OK);
    m.address=0;
    assert(o0_display_mode_validate(&m)==O0_EINVAL);
    m=valid_mode(); m.pitch=100;
    assert(o0_display_mode_validate(&m)==O0_ERANGE);
    m=valid_mode(); m.bytes=100;
    assert(o0_display_mode_validate(&m)==O0_ERANGE);
    m=valid_mode(); m.height=UINT64_MAX;
    assert(o0_display_mode_validate(&m)==O0_EOVERFLOW);
    m=valid_mode(); m.green_shift=16;
    assert(o0_display_mode_validate(&m)==O0_EINVAL);
    m=valid_mode(); m.memory_model=0;
    assert(o0_display_mode_validate(&m)==O0_ENOTSUP);
    m=valid_mode(); m.bpp=16;
    assert(o0_display_mode_validate(&m)==O0_ENOTSUP);
    return 0;
}
