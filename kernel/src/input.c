/* Stage 6.2: single-producer/single-consumer bounded event queue. */
#include "ozero/input.h"
void o0_input_init(struct o0_input_queue *q) {
    if (!q) return;
    q->head=0; q->tail=0; q->dropped=0;
}
o0_status o0_input_push(struct o0_input_queue *q, const struct o0_input_event *e) {
    if (!q || !e || (e->type != O0_INPUT_KEY && e->type != O0_INPUT_POINTER))
        return O0_EINVAL;
    uint32_t head=q->head, next=(head+1u)%O0_INPUT_CAPACITY;
    if (next==q->tail) { ++q->dropped; return O0_EBUSY; }
    q->events[head]=*e;
    __atomic_store_n(&q->head,next,__ATOMIC_RELEASE);
    return O0_OK;
}
bool o0_input_pop(struct o0_input_queue *q, struct o0_input_event *e) {
    if (!q || !e) return false;
    uint32_t tail=q->tail;
    if (tail==__atomic_load_n(&q->head,__ATOMIC_ACQUIRE)) return false;
    *e=q->events[tail];
    __atomic_store_n(&q->tail,(tail+1u)%O0_INPUT_CAPACITY,__ATOMIC_RELEASE);
    return true;
}
