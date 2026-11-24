#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ozero/surface.h"

static uint32_t rgb_at(const struct o0_surface *s, size_t x, size_t y) {
    const uint8_t *p=s->pixels+y*s->pitch+x*4u;
    uint8_t r=s->format==O0_PIXEL_RGBX8888?p[0]:p[2];
    uint8_t g=p[1];
    uint8_t b=s->format==O0_PIXEL_RGBX8888?p[2]:p[0];
    return ((uint32_t)r<<16)|((uint32_t)g<<8)|b;
}

int main(void) {
    uint8_t a[4*4*4], b[4*4*4];
    memset(a,0,sizeof(a)); memset(b,0,sizeof(b));
    struct o0_surface s, d;
    assert(o0_surface_init(&s,a,4,4,16,O0_PIXEL_RGBX8888)==O0_OK);
    assert(o0_surface_init(&d,b,4,4,16,O0_PIXEL_BGRX8888)==O0_OK);
    assert(o0_surface_init(&s,a,4,4,15,O0_PIXEL_RGBX8888)==O0_ERANGE);
    assert(o0_surface_init(&s,a,4,4,16,(enum o0_pixel_format)99)==O0_ENOTSUP);
    assert(o0_surface_init(NULL,a,4,4,16,O0_PIXEL_RGBX8888)==O0_EINVAL);

    assert(o0_surface_fill(&s,0x102030)==O0_OK);
    assert(rgb_at(&s,0,0)==0x102030 && rgb_at(&s,3,3)==0x102030);
    assert(a[3]==0xff);
    assert(o0_surface_fill_rect(&s,-1,-1,3,3,0xa0b0c0)==O0_OK);
    assert(rgb_at(&s,0,0)==0xa0b0c0);
    assert(rgb_at(&s,1,1)==0xa0b0c0);
    assert(rgb_at(&s,2,2)==0x102030);
    assert(o0_surface_fill_rect(&s,9,9,2,2,0xffffff)==O0_OK);
    assert(o0_surface_fill_rect(&s,0,0,0,2,0xffffff)==O0_EINVAL);

    assert(o0_surface_blit(&d,1,1,&s,0,0,3,3)==O0_OK);
    assert(rgb_at(&d,1,1)==0xa0b0c0);
    assert(rgb_at(&d,3,3)==0x102030);
    assert(b[(1*4+1)*4]==0xc0); /* B,G,R,X in destination memory. */

    /* Clipping both source and destination must preserve the surviving overlap. */
    assert(o0_surface_fill(&d,0)==O0_OK);
    assert(o0_surface_blit(&d,-1,-1,&s,1,1,4,4)==O0_OK);
    assert(rgb_at(&d,0,0)==rgb_at(&s,2,2));
    assert(rgb_at(&d,2,2)==0);

    /* Same-buffer overlapping copy behaves like memmove. */
    assert(o0_surface_fill(&s,0)==O0_OK);
    assert(o0_surface_fill_rect(&s,0,0,4,1,0x010101)==O0_OK);
    assert(o0_surface_fill_rect(&s,0,0,1,1,0xabcdef)==O0_OK);
    assert(o0_surface_blit(&s,1,0,&s,0,0,3,1)==O0_OK);
    assert(rgb_at(&s,1,0)==0xabcdef);
    assert(rgb_at(&s,2,0)==0x010101);

    puts("surface tests: 6 checks passed");
    return 0;
}
