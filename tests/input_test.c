#include <assert.h>
#include "ozero/input.h"

int main(void) {
    struct o0_input_queue q;
    struct o0_input_event in={.type=O0_INPUT_KEY,.pressed=1,.code=30,.timestamp=7};
    struct o0_input_event out={0};
    o0_input_init(&q);
    assert(!o0_input_pop(&q,&out));
    assert(o0_input_push(&q,&in)==O0_OK);
    assert(o0_input_pop(&q,&out));
    assert(out.type==in.type && out.pressed==in.pressed &&
           out.code==in.code && out.timestamp==in.timestamp);
    for (unsigned i=0;i<O0_INPUT_CAPACITY-1u;i++) {
        in.code=(uint16_t)i;
        assert(o0_input_push(&q,&in)==O0_OK);
    }
    assert(o0_input_push(&q,&in)==O0_EBUSY);
    assert(q.dropped==1);
    for (unsigned i=0;i<O0_INPUT_CAPACITY-1u;i++) {
        assert(o0_input_pop(&q,&out));
        assert(out.code==(uint16_t)i);
    }
    assert(!o0_input_pop(&q,&out));
    in.type=99;
    assert(o0_input_push(&q,&in)==O0_EINVAL);
    return 0;
}
