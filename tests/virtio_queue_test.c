#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/virtio_queue.h"
#include "virtio_mock.h"

#define COMMON_LEN O0_VIRTIO_COMMON_MIN_LENGTH
#define DMA_MASK ((UINT64_C(1) << 44) - 1u)

static void test_size_choose(void) {
    uint16_t size = 0;
    assert(o0_virtio_queue_size_choose(256u, 8u, &size) && size == 8u);
    assert(o0_virtio_queue_size_choose(8u, 256u, &size) && size == 8u);
    assert(o0_virtio_queue_size_choose(1024u, 256u, &size) && size == 256u);
    /* Driver maximum below the minimum queue size cannot be honoured. */
    assert(!o0_virtio_queue_size_choose(256u, 2u, &size));
    /* A device that advertises zero or a non-power-of-two queue is rejected. */
    assert(!o0_virtio_queue_size_choose(0u, 8u, &size));
    assert(!o0_virtio_queue_size_choose(6u, 8u, &size));
    assert(!o0_virtio_queue_size_choose(2u, 8u, &size));
    assert(!o0_virtio_queue_size_choose(256u, 8u, NULL));
}

static void test_notify_offset(void) {
    uint32_t off = 0xFFFFFFFFu;
    assert(o0_virtio_notify_offset(0u, 4u, 64u, &off) && off == 0u);
    assert(o0_virtio_notify_offset(3u, 4u, 64u, &off) && off == 12u);
    /* The last valid offset still leaves room for the 16-bit queue index. */
    assert(o0_virtio_notify_offset(15u, 4u, 64u, &off) && off == 60u);
    assert(!o0_virtio_notify_offset(16u, 4u, 64u, &off));
    assert(!o0_virtio_notify_offset(3u, 4u, 13u, &off));
    /* Multiplication overflow is refused, not wrapped. */
    assert(!o0_virtio_notify_offset(0xFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, &off));
    assert(!o0_virtio_notify_offset(0u, 4u, 64u, NULL));
}

static void test_address_ok(void) {
    assert(o0_virtio_queue_address_ok(0x100000u, 128u, DMA_MASK, 16u));
    assert(!o0_virtio_queue_address_ok(0u, 128u, DMA_MASK, 16u));
    assert(!o0_virtio_queue_address_ok(0x100008u, 128u, DMA_MASK, 16u));
    /* Entirely outside the device address mask. */
    assert(!o0_virtio_queue_address_ok(UINT64_C(1) << 45, 128u, DMA_MASK, 16u));
    /* Starts inside the mask but crosses its end. */
    assert(!o0_virtio_queue_address_ok(DMA_MASK - 7u, 16u, DMA_MASK, 8u));
    /* Wrap-around. */
    assert(!o0_virtio_queue_address_ok(UINT64_MAX - 3u, 16u, UINT64_MAX, 4u));
    assert(!o0_virtio_queue_address_ok(0x100000u, 0u, DMA_MASK, 16u));
    assert(!o0_virtio_queue_address_ok(0x100000u, 128u, DMA_MASK, 0u));
}

static void vmock_init(struct vmock *m) {
    memset(m, 0, sizeof(*m));
    m->features[1] = 0x1u;
    m->num_queues = 2u;
    m->queue[0].size = 256u;
    m->queue[0].notify_off = 0u;
    m->queue[1].size = 64u;
    m->queue[1].notify_off = 1u;
}

static struct o0_virtio_queue_plan plan_for(uint16_t index, uint16_t size) {
    struct o0_virtio_queue_plan plan = {
        .index = index,
        .size = size,
        .desc_phys = 0x100000u,
        .avail_phys = 0x100080u,
        .used_phys = 0x100094u,
        .addr_mask = DMA_MASK
    };
    return plan;
}

static void test_queue_configure(void) {
    struct vmock m;
    vmock_init(&m);
    struct o0_virtio_regs regs = {&vmock_iface, &m, COMMON_LEN};
    struct o0_virtio_queue_info info;
    struct o0_virtio_queue_plan plan = plan_for(0u, 8u);

    assert(o0_virtio_queue_configure(&regs, &plan, &info) == O0_OK);
    assert(info.index == 0u && info.size == 8u && info.notify_off == 0u);
    assert(m.queue[0].desc == plan.desc_phys);
    assert(m.queue[0].driver == plan.avail_phys);
    assert(m.queue[0].device == plan.used_phys);
    assert(m.queue[0].enable == 1u);
    assert(m.queue_select == 0u);

    /* An enabled queue must be reset before it can be reconfigured. */
    assert(o0_virtio_queue_configure(&regs, &plan, &info) == O0_EBUSY);

    /* Queue index bounds come from num_queues, not from a driver constant. */
    vmock_init(&m);
    plan = plan_for(2u, 8u);
    assert(o0_virtio_queue_configure(&regs, &plan, &info) == O0_ERANGE);
    assert(m.queue[0].enable == 0u && m.queue[1].enable == 0u);

    m.num_queues = 0u;
    plan = plan_for(0u, 8u);
    assert(o0_virtio_queue_configure(&regs, &plan, &info) == O0_ERANGE);

    /* The driver cannot ask for more descriptors than the device offers. */
    vmock_init(&m);
    plan = plan_for(0u, 512u);
    assert(o0_virtio_queue_configure(&regs, &plan, &info) == O0_ERANGE);
    assert(m.queue[0].enable == 0u);

    /* A non-power-of-two device maximum is not usable. */
    vmock_init(&m);
    m.queue[0].size = 6u;
    plan = plan_for(0u, 4u);
    assert(o0_virtio_queue_configure(&regs, &plan, &info) == O0_ENOTSUP);

    /* Addresses outside the device DMA mask are rejected before programming. */
    vmock_init(&m);
    plan = plan_for(0u, 8u);
    plan.desc_phys = UINT64_C(1) << 45;
    assert(o0_virtio_queue_configure(&regs, &plan, &info) == O0_ERANGE);
    assert(m.queue[0].desc == 0u && m.queue[0].enable == 0u);

    /* Misaligned descriptor table. */
    plan = plan_for(0u, 8u);
    plan.desc_phys = 0x100008u;
    assert(o0_virtio_queue_configure(&regs, &plan, &info) == O0_ERANGE);
    assert(m.queue[0].enable == 0u);

    /* Misaligned used ring. */
    plan = plan_for(0u, 8u);
    plan.used_phys = 0x100096u;
    assert(o0_virtio_queue_configure(&regs, &plan, &info) == O0_ERANGE);

    /* A device that refuses to latch queue_enable is reported, not trusted. */
    vmock_init(&m);
    m.refuse_enable = true;
    plan = plan_for(0u, 8u);
    assert(o0_virtio_queue_configure(&regs, &plan, &info) == O0_EIO);

    /* A zero-sized plan is an argument error. */
    vmock_init(&m);
    plan = plan_for(0u, 0u);
    assert(o0_virtio_queue_configure(&regs, &plan, &info) == O0_EINVAL);
}

static uint8_t ring_mem[4096];

static void test_vring(void) {
    memset(ring_mem, 0, sizeof(ring_mem));
    struct o0_virtio_vq vq;
    assert(o0_virtio_vq_init(&vq, ring_mem, 8u));
    assert(vq.desc == (struct o0_vring_desc *)ring_mem);
    assert((uint8_t *)vq.avail == ring_mem + 128u);
    assert((uint8_t *)vq.used == ring_mem + 148u);
    assert(vq.num_free == 8u && vq.free_head == 0u);

    /* Rejected shapes. */
    struct o0_virtio_vq bad;
    assert(!o0_virtio_vq_init(&bad, NULL, 8u));
    assert(!o0_virtio_vq_init(&bad, ring_mem, 0u));
    assert(!o0_virtio_vq_init(&bad, ring_mem, 2u));
    assert(!o0_virtio_vq_init(&bad, ring_mem, 6u));
    assert(!o0_virtio_vq_init(&bad, ring_mem, O0_VIRTIO_QUEUE_MAX_SIZE * 2u));

    uint16_t head = 0xFFFFu;
    assert(o0_virtio_vq_alloc(&vq, 3u, &head) && head == 0u);
    o0_virtio_vq_set(&vq, 0u, 0x1000u, 16u, O0_VRING_DESC_F_NEXT, 1u);
    o0_virtio_vq_set(&vq, 1u, 0x2000u, 512u,
                     O0_VRING_DESC_F_NEXT | O0_VRING_DESC_F_WRITE, 2u);
    o0_virtio_vq_set(&vq, 2u, 0x3000u, 1u, O0_VRING_DESC_F_WRITE, 0u);
    assert(vq.num_free == 5u);
    assert(vq.desc[0].addr == 0x1000u && vq.desc[1].len == 512u);

    assert(o0_virtio_vq_publish(&vq, head));
    assert(vq.avail->idx == 1u && vq.avail->ring[0] == 0u);
    assert(o0_virtio_vq_outstanding(&vq) == 1u);

    uint32_t len = 0;
    assert(o0_virtio_vq_poll(&vq, 0u, &len) == O0_VIRTIO_VQ_IDLE);

    /* Device completion: status byte plus data length. */
    vq.used->ring[0].id = 0u;
    vq.used->ring[0].len = 513u;
    vq.used->idx = 1u;
    assert(o0_virtio_vq_poll(&vq, 0u, &len) == O0_VIRTIO_VQ_DONE);
    assert(len == 513u);
    assert(o0_virtio_vq_outstanding(&vq) == 0u);
    o0_virtio_vq_release(&vq, head, 3u);
    assert(vq.num_free == 8u);

    /* A completion that names a descriptor we never submitted is rejected. */
    assert(o0_virtio_vq_alloc(&vq, 3u, &head) && head == 0u);
    assert(o0_virtio_vq_publish(&vq, head));
    vq.used->ring[1].id = 5u;
    vq.used->ring[1].len = 1u;
    vq.used->idx = 2u;
    assert(o0_virtio_vq_poll(&vq, 0u, &len) == O0_VIRTIO_VQ_BAD_ID);
    /* The chain is still owned by the driver; it must not be reused blindly. */
    assert(o0_virtio_vq_outstanding(&vq) == 0u);

    /* The used ring running past the queue size is reported, not walked. */
    vq.used->idx = 20u;
    assert(o0_virtio_vq_poll(&vq, 0u, &len) == O0_VIRTIO_VQ_OVERRUN);

    /* Reset path: bookkeeping and rings are re-initialised only after the
     * device is quiescent. */
    o0_virtio_vq_reinit(&vq);
    assert(vq.avail->idx == 0u && vq.used->idx == 0u);
    assert(vq.num_free == 8u && vq.next_avail == 0u && vq.last_used == 0u);

    /* A full ring refuses further publication instead of overwriting entries
     * the device has not consumed. */
    for (uint16_t i = 0; i < 8u; ++i) {
        uint16_t h = 0;
        assert(o0_virtio_vq_alloc(&vq, 1u, &h) && h == i);
        assert(o0_virtio_vq_publish(&vq, h));
    }
    assert(vq.num_free == 0u);
    assert(!o0_virtio_vq_alloc(&vq, 1u, &head));
    assert(!o0_virtio_vq_publish(&vq, 0u));
    assert(o0_virtio_vq_outstanding(&vq) == 8u);
}

static void test_notify(void) {
    struct vmock_notify n;
    memset(&n, 0, sizeof(n));
    n.length = 64u;
    struct o0_virtio_regs regs = {&vmock_notify_iface, &n, 64u};

    assert(o0_virtio_notify(&regs, 3u, 2u, 4u) == O0_OK);
    assert(n.bytes[8] == 3u && n.bytes[9] == 0u);
    assert(n.writes == 1u);

    /* Offset 16 * 4 = 64 leaves no room for the index. */
    assert(o0_virtio_notify(&regs, 0u, 16u, 4u) == O0_ERANGE);
    assert(n.writes == 1u);

    /* Multiplication overflow. */
    assert(o0_virtio_notify(&regs, 0u, 0xFFFFu, 0xFFFFFFFFu) == O0_ERANGE);
    assert(n.writes == 1u);

    /* A zero multiplier places every queue at offset zero. */
    assert(o0_virtio_notify(&regs, 7u, 5u, 0u) == O0_OK);
    assert(n.bytes[0] == 7u && n.bytes[1] == 0u);

    /* The region length comes from the mapping, not from the capability. */
    struct o0_virtio_regs small = {&vmock_notify_iface, &n, 12u};
    assert(o0_virtio_notify(&small, 1u, 5u, 2u) == O0_OK); /* offset 10 */
    assert(o0_virtio_notify(&small, 1u, 6u, 2u) == O0_ERANGE); /* offset 12 */

    assert(o0_virtio_notify(NULL, 0u, 0u, 0u) == O0_EINVAL);
}

int main(void) {
    test_size_choose();
    test_notify_offset();
    test_address_ok();
    test_queue_configure();
    test_vring();
    test_notify();
    puts("virtio modern queue, notification and ring tests: PASS");
    return 0;
}
