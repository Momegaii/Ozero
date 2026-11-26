#include "ozero/virtio_queue.h"

#include <string.h>

bool o0_virtio_queue_size_choose(uint16_t device_max, uint16_t driver_max,
                                 uint16_t *out) {
    if (out == NULL || device_max == 0u || driver_max == 0u) return false;
    if ((device_max & (uint16_t)(device_max - 1u)) != 0u) return false;
    uint16_t size = device_max < driver_max ? device_max : driver_max;
    while ((size & (uint16_t)(size - 1u)) != 0u) {
        size = (uint16_t)(size & (uint16_t)(size - 1u));
    }
    if (size > O0_VIRTIO_QUEUE_MAX_SIZE) size = O0_VIRTIO_QUEUE_MAX_SIZE;
    if (size < O0_VIRTIO_QUEUE_MIN_SIZE) return false;
    *out = size;
    return true;
}

bool o0_virtio_notify_offset(uint16_t queue_notify_off, uint32_t multiplier,
                             uint32_t region_length, uint32_t *out) {
    if (out == NULL) return false;
    uint64_t offset = (uint64_t)queue_notify_off * (uint64_t)multiplier;
    if (offset > UINT32_MAX) return false;
    if (offset + 2u > (uint64_t)region_length) return false;
    *out = (uint32_t)offset;
    return true;
}

bool o0_virtio_queue_address_ok(uint64_t addr, uint64_t bytes, uint64_t addr_mask,
                                size_t alignment) {
    if (addr == 0u || bytes == 0u || alignment == 0u) return false;
    if (addr % (uint64_t)alignment != 0u) return false;
    if ((addr & ~addr_mask) != 0u) return false;
    if (bytes - 1u > UINT64_MAX - addr) return false;
    return ((addr + bytes - 1u) & ~addr_mask) == 0u;
}

o0_status o0_virtio_queue_configure(const struct o0_virtio_regs *regs,
                                    const struct o0_virtio_queue_plan *plan,
                                    struct o0_virtio_queue_info *out) {
    if (regs == NULL || regs->iface == NULL || plan == NULL || out == NULL) {
        return O0_EINVAL;
    }
    if (plan->size == 0u || (plan->size & (uint16_t)(plan->size - 1u)) != 0u ||
        plan->size < O0_VIRTIO_QUEUE_MIN_SIZE) {
        return O0_EINVAL;
    }

    uint16_t num_queues = 0;
    if (!o0_virtio_reg_read16(regs, O0_VIRTIO_COMMON_NUM_QUEUES, &num_queues)) {
        return O0_EIO;
    }
    if (num_queues == 0u || plan->index >= num_queues) return O0_ERANGE;

    if (!o0_virtio_reg_write16(regs, O0_VIRTIO_COMMON_QUEUE_SELECT, plan->index)) {
        return O0_EIO;
    }
    uint16_t device_max = 0;
    if (!o0_virtio_reg_read16(regs, O0_VIRTIO_COMMON_QUEUE_SIZE, &device_max)) {
        return O0_EIO;
    }
    if (device_max == 0u) return O0_ENOTSUP;
    if ((device_max & (uint16_t)(device_max - 1u)) != 0u) return O0_ENOTSUP;
    if (plan->size > device_max) return O0_ERANGE;

    uint16_t enabled = 0xFFFFu;
    if (!o0_virtio_reg_read16(regs, O0_VIRTIO_COMMON_QUEUE_ENABLE, &enabled)) {
        return O0_EIO;
    }
    if (enabled != 0u) return O0_EBUSY; /* reset the device before reconfiguring */

    size_t avail_off = 0, used_off = 0, total = 0;
    if (!o0_virtio_split_ring_layout(plan->size, &avail_off, &used_off, &total)) {
        return O0_EINVAL;
    }
    uint64_t desc_bytes = (uint64_t)plan->size * 16u;
    uint64_t avail_bytes = (uint64_t)used_off - (uint64_t)avail_off;
    uint64_t used_bytes = (uint64_t)total - (uint64_t)used_off;
    if (!o0_virtio_queue_address_ok(plan->desc_phys, desc_bytes, plan->addr_mask, 16u) ||
        !o0_virtio_queue_address_ok(plan->avail_phys, avail_bytes, plan->addr_mask, 2u) ||
        !o0_virtio_queue_address_ok(plan->used_phys, used_bytes, plan->addr_mask, 4u)) {
        return O0_ERANGE;
    }

    if (!o0_virtio_reg_write64(regs, O0_VIRTIO_COMMON_QUEUE_DESC, plan->desc_phys) ||
        !o0_virtio_reg_write64(regs, O0_VIRTIO_COMMON_QUEUE_DRIVER, plan->avail_phys) ||
        !o0_virtio_reg_write64(regs, O0_VIRTIO_COMMON_QUEUE_DEVICE, plan->used_phys)) {
        return O0_EIO;
    }
    o0_virtio_reg_write_barrier(regs);
    if (!o0_virtio_reg_write16(regs, O0_VIRTIO_COMMON_QUEUE_ENABLE, 1u)) return O0_EIO;

    uint16_t observed = 0;
    if (!o0_virtio_reg_read16(regs, O0_VIRTIO_COMMON_QUEUE_ENABLE, &observed) ||
        observed != 1u) {
        return O0_EIO;
    }
    uint16_t notify_off = 0;
    if (!o0_virtio_reg_read16(regs, O0_VIRTIO_COMMON_QUEUE_NOTIFY_OFF, &notify_off)) {
        return O0_EIO;
    }
    out->index = plan->index;
    out->size = plan->size;
    out->notify_off = notify_off;
    return O0_OK;
}

static bool split_ring_total(uint16_t n, size_t *total) {
    size_t avail_off = 0;
    size_t used_off = 0;
    return o0_virtio_split_ring_layout(n, &avail_off, &used_off, total);
}

void o0_virtio_vq_reinit(struct o0_virtio_vq *vq) {
    if (vq == NULL || vq->desc == NULL || vq->size == 0u) return;
    size_t total = 0;
    if (!split_ring_total(vq->size, &total)) return;
    memset(vq->desc, 0, total);
    vq->next_avail = 0;
    vq->last_used = 0;
    vq->free_head = 0;
    vq->num_free = vq->size;
    for (uint16_t i = 0; i < vq->size; ++i) {
        vq->free_next[i] = (uint16_t)(i + 1u);
    }
    vq->free_next[vq->size - 1u] = UINT16_MAX;
}

bool o0_virtio_vq_init(struct o0_virtio_vq *vq, void *base, uint16_t size) {
    if (vq == NULL || base == NULL || size < O0_VIRTIO_QUEUE_MIN_SIZE ||
        size > O0_VIRTIO_QUEUE_MAX_SIZE ||
        (size & (uint16_t)(size - 1u)) != 0u) {
        return false;
    }
    size_t avail_off = 0, used_off = 0, total = 0;
    if (!o0_virtio_split_ring_layout(size, &avail_off, &used_off, &total)) return false;
    memset(vq, 0, sizeof(*vq));
    uint8_t *p = (uint8_t *)base;
    vq->desc = (struct o0_vring_desc *)p;
    vq->avail = (struct o0_vring_avail *)(p + avail_off);
    vq->used = (struct o0_vring_used *)(p + used_off);
    vq->size = size;
    o0_virtio_vq_reinit(vq);
    return true;
}

bool o0_virtio_vq_alloc(struct o0_virtio_vq *vq, uint16_t count, uint16_t *head) {
    if (vq == NULL || head == NULL || count == 0u || count > vq->num_free) return false;
    uint16_t first = vq->free_head;
    uint16_t last = first;
    for (uint16_t i = 1; i < count; ++i) {
        last = vq->free_next[last];
        if (last >= vq->size) return false;
    }
    vq->free_head = vq->free_next[last];
    vq->num_free = (uint16_t)(vq->num_free - count);
    *head = first;
    return true;
}

void o0_virtio_vq_set(struct o0_virtio_vq *vq, uint16_t idx, uint64_t addr,
                      uint32_t len, uint16_t flags, uint16_t next) {
    if (vq == NULL || vq->desc == NULL || idx >= vq->size) return;
    vq->desc[idx].addr = addr;
    vq->desc[idx].len = len;
    vq->desc[idx].flags = flags;
    vq->desc[idx].next = next;
}

uint16_t o0_virtio_vq_outstanding(const struct o0_virtio_vq *vq) {
    if (vq == NULL) return 0u;
    return (uint16_t)(vq->next_avail - vq->last_used);
}

bool o0_virtio_vq_publish(struct o0_virtio_vq *vq, uint16_t head) {
    if (vq == NULL || vq->avail == NULL || head >= vq->size) return false;
    if (o0_virtio_vq_outstanding(vq) >= vq->size) return false;
    vq->avail->ring[vq->next_avail % vq->size] = head;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    vq->next_avail = (uint16_t)(vq->next_avail + 1u);
    vq->avail->idx = vq->next_avail;
    return true;
}

void o0_virtio_vq_release(struct o0_virtio_vq *vq, uint16_t head, uint16_t count) {
    if (vq == NULL || vq->desc == NULL || head >= vq->size || count == 0u) return;
    if ((uint32_t)vq->num_free + count > vq->size) return;
    uint16_t last = head;
    for (uint16_t i = 1; i < count; ++i) {
        uint16_t next = vq->desc[last].next;
        if (next >= vq->size) return;
        last = next;
    }
    vq->free_next[last] = vq->free_head;
    vq->free_head = head;
    vq->num_free = (uint16_t)(vq->num_free + count);
}

enum o0_virtio_vq_poll o0_virtio_vq_poll(struct o0_virtio_vq *vq,
                                         uint16_t expected_head, uint32_t *out_len) {
    if (vq == NULL || vq->used == NULL || out_len == NULL) return O0_VIRTIO_VQ_BAD_ID;
    uint16_t pending = (uint16_t)(vq->used->idx - vq->last_used);
    if (pending == 0u) return O0_VIRTIO_VQ_IDLE;
    if (pending > vq->size) return O0_VIRTIO_VQ_OVERRUN;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    uint16_t slot = (uint16_t)(vq->last_used % vq->size);
    uint32_t id = vq->used->ring[slot].id;
    uint32_t len = vq->used->ring[slot].len;
    vq->last_used = (uint16_t)(vq->last_used + 1u);
    if (id != (uint32_t)expected_head) return O0_VIRTIO_VQ_BAD_ID;
    *out_len = len;
    return O0_VIRTIO_VQ_DONE;
}

o0_status o0_virtio_notify(const struct o0_virtio_regs *notify_regs,
                           uint16_t queue_index, uint16_t notify_off,
                           uint32_t multiplier) {
    if (notify_regs == NULL || notify_regs->iface == NULL) return O0_EINVAL;
    if (notify_regs->length > UINT32_MAX) return O0_ERANGE;
    uint32_t offset = 0;
    if (!o0_virtio_notify_offset(notify_off, multiplier,
                                 (uint32_t)notify_regs->length, &offset)) {
        return O0_ERANGE;
    }
    /* Every descriptor and available-ring write must be visible before the
     * device is told to look at the queue. */
    o0_virtio_reg_write_barrier(notify_regs);
    if (!o0_virtio_reg_write16(notify_regs, offset, queue_index)) return O0_EIO;
    return O0_OK;
}
