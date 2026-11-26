#include <assert.h>
#include <stdio.h>
#include "ozero/virtio_ring.h"
static void check(uint16_t n, size_t expected_used, size_t expected_total) {
    size_t used=0,total=0;
    assert(o0_virtio_legacy_ring_layout(n,&used,&total));
    assert(used==expected_used);
    assert(total==expected_total);
}
int main(void) {
    check(256u,8192u,10246u);
    check(32u,4096u,4358u);
    assert(!o0_virtio_legacy_ring_layout(0u,(size_t[1]){0},(size_t[1]){0}));
    puts("virtio legacy ring layout tests: ok");
    return 0;
}
