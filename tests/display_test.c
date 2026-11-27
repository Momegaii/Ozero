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

    /* Eight equal bars; band 0 centre at x=50,y=300 is unstriped, band 7
     * centre at x=750,y=300 is striped. Values are 8:8:8 shifted pixels. */
    m=valid_mode();
    assert(o0_display_bar_center_expected(&m, 0) == 0x00182048u);
    assert(o0_display_bar_center_expected(&m, 7) == 0x00a5a5a5u);
    assert(o0_display_bar_center_expected(&m, 8) == 0u);
    assert(o0_display_bar_center_expected(NULL, 0) == 0u);
    uint32_t checksum = o0_display_expected_checksum(&m);
    assert(checksum != 0u);
    assert(o0_display_expected_checksum(&m) == checksum);
    m.width=1024;
    assert(o0_display_expected_checksum(&m) != checksum);
    assert(o0_display_pattern_pixel(NULL, 0, 0) == 0u);
    assert(o0_display_pattern_pixel(&m, m.width, 0) == 0u);
    return 0;
}
