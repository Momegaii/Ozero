/* Ozero kernel - GPU contexts, queues, submission policy and fences.
 *
 * No vendor register, opcode or page-table format appears here. The backend contract
 * (struct o0_gpu_engine_ops) is the only path to hardware. The policy implemented here:
 *
 *   - a queue submits in order and completes in order; seqnos are monotonic per queue
 *   - a command buffer's memory is pinned until the backend confirms consumption, so a
 *     cancelled or faulted submission can never release pages the device may still read
 *   - a bounded wait faults the queue on timeout; a faulted queue refuses new work and
 *     can only be recovered through a backend reset that reports success
 *   - a context cannot be destroyed while queues are attached, and a queue cannot be
 *     destroyed while any fence is outstanding or unconsumed
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/gpu.h"
#include "ozero/gpu_queue.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/sync.h"
#include "string.h"

static uint32_t next_context_id = 1u;

static struct o0_gpu_fence *find_fence(struct o0_gpu_queue *queue, uint64_t seqno) {
    for (uint32_t i = 0; i < queue->fence_count; ++i) {
        if (queue->fences[i].seqno == seqno) {
            return &queue->fences[i];
        }
    }
    return NULL;
}

static void drop_fence(struct o0_gpu_queue *queue, uint32_t index) {
    if (queue->fences[index].memory != NULL) {
        (void)o0_gpu_memory_unpin(queue->fences[index].memory);
    }
    for (uint32_t i = index + 1u; i < queue->fence_count; ++i) {
        queue->fences[i - 1u] = queue->fences[i];
    }
    queue->fence_count--;
}

/* Release resources for submissions the device has stopped reading, and record completion
 * evidence. The two are separate: the read pointer passing a submission only means the
 * commands were fetched, so it must never turn a pending fence into a successful one. */
static void queue_reap_locked(struct o0_gpu_queue *queue) {
    for (uint32_t i = 0; i < queue->fence_count;) {
        struct o0_gpu_fence *fence = &queue->fences[i];
        bool complete = false;
        bool retired = false;
        if (queue->ops->poll(queue->engine, fence->seqno, &complete) == O0_OK && complete) {
            if (fence->state == O0_GPU_FENCE_PENDING) {
                fence->state = O0_GPU_FENCE_SIGNALED;
            }
            if (fence->seqno > queue->signaled_seqno) {
                queue->signaled_seqno = fence->seqno;
            }
            fence->consumed = true;
        }
        if (!fence->consumed &&
            queue->ops->retired(queue->engine, fence->seqno, &retired) == O0_OK && retired) {
            fence->consumed = true;
        }
        if (fence->consumed) {
            if (fence->seqno > queue->retired_seqno) {
                queue->retired_seqno = fence->seqno;
            }
            drop_fence(queue, i);
            continue;
        }
        ++i;
    }
}

static void queue_fault_locked(struct o0_gpu_queue *queue, o0_status reason) {
    queue->state = O0_GPU_QUEUE_FAULTED;
    for (uint32_t i = 0; i < queue->fence_count; ++i) {
        struct o0_gpu_fence *fence = &queue->fences[i];
        if (fence->state == O0_GPU_FENCE_PENDING) {
            fence->state = O0_GPU_FENCE_FAILED;
            fence->error = reason;
        }
        /* Best effort: tell the backend to stop reporting success. The pin is kept
         * until the backend confirms the device no longer reads the commands. */
        (void)queue->ops->cancel(queue->engine, fence->seqno);
    }
}

/* ---- Contexts ---- */

o0_status o0_gpu_context_create(struct o0_gpu_vm *vm, struct o0_gpu_context **out) {
    if (vm == NULL || out == NULL) {
        return O0_EINVAL;
    }
    struct o0_gpu_context *ctx = o0_kalloc(sizeof(*ctx), 0);
    if (ctx == NULL) {
        return O0_ENOMEM;
    }
    memset(ctx, 0, sizeof(*ctx));
    o0_spin_init(&ctx->lock);
    ctx->id = next_context_id++;
    if (next_context_id == 0u) {
        next_context_id = 1u;
    }
    ctx->vm = vm;
    *out = ctx;
    return O0_OK;
}

o0_status o0_gpu_context_destroy(struct o0_gpu_context *ctx) {
    if (ctx == NULL) {
        return O0_EINVAL;
    }
    o0_spin_lock(&ctx->lock);
    if (ctx->queue_count != 0u) {
        o0_spin_unlock(&ctx->lock);
        return O0_EBUSY;
    }
    ctx->destroyed = true;
    o0_spin_unlock(&ctx->lock);
    o0_kfree(ctx, sizeof(*ctx));
    return O0_OK;
}

uint32_t o0_gpu_context_queue_count(const struct o0_gpu_context *ctx) {
    return ctx != NULL ? ctx->queue_count : 0u;
}

struct o0_gpu_vm *o0_gpu_context_vm(const struct o0_gpu_context *ctx) {
    return ctx != NULL ? ctx->vm : NULL;
}

/* ---- Queues ---- */

o0_status o0_gpu_queue_create(struct o0_gpu_context *ctx,
                              const struct o0_gpu_engine_ops *ops, void *engine,
                              struct o0_gpu_queue **out) {
    if (ctx == NULL || ops == NULL || engine == NULL || out == NULL) {
        return O0_EINVAL;
    }
    /* validate is mandatory: it is what enforces address-space isolation between
     * contexts. A backend without it cannot safely accept commands. */
    if (ops->validate == NULL || ops->submit == NULL || ops->poll == NULL ||
        ops->retired == NULL || ops->cancel == NULL || ops->reset == NULL ||
        ops->is_quarantined == NULL) {
        return O0_EINVAL;
    }
    if (ctx->destroyed) {
        return O0_EINVAL;
    }

    o0_spin_lock(&ctx->lock);
    if (ctx->queue_count >= O0_GPU_MAX_QUEUES) {
        o0_spin_unlock(&ctx->lock);
        return O0_ENOSPC;
    }
    struct o0_gpu_queue *queue = o0_kalloc(sizeof(*queue), 0);
    if (queue == NULL) {
        o0_spin_unlock(&ctx->lock);
        return O0_ENOMEM;
    }
    memset(queue, 0, sizeof(*queue));
    o0_spin_init(&queue->lock);
    queue->context = ctx;
    queue->ops = ops;
    queue->engine = engine;
    queue->state = O0_GPU_QUEUE_ACTIVE;
    ctx->queue_count++;
    o0_spin_unlock(&ctx->lock);

    *out = queue;
    return O0_OK;
}

o0_status o0_gpu_queue_destroy(struct o0_gpu_queue *queue) {
    if (queue == NULL || queue->context == NULL) {
        return O0_EINVAL;
    }
    o0_spin_lock(&queue->lock);
    queue_reap_locked(queue);
    if (queue->fence_count != 0u) {
        o0_spin_unlock(&queue->lock);
        return O0_EBUSY;
    }
    o0_spin_unlock(&queue->lock);

    o0_spin_lock(&queue->context->lock);
    if (queue->context->queue_count > 0u) {
        queue->context->queue_count--;
    }
    o0_spin_unlock(&queue->context->lock);

    o0_kfree(queue, sizeof(*queue));
    return O0_OK;
}

enum o0_gpu_queue_state o0_gpu_queue_state(const struct o0_gpu_queue *queue) {
    return queue != NULL ? queue->state : O0_GPU_QUEUE_FAULTED;
}

o0_status o0_gpu_queue_submit(struct o0_gpu_queue *queue, const struct o0_gpu_cmd *cmd,
                              uint64_t *out_seqno) {
    if (queue == NULL || cmd == NULL || out_seqno == NULL || cmd->memory == NULL) {
        return O0_EINVAL;
    }
    if (cmd->size_dwords == 0u || cmd->used_dwords == 0u ||
        cmd->used_dwords > cmd->size_dwords) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(cmd->gpu_va, 4u)) {
        return O0_EINVAL;
    }
    if ((uint64_t)cmd->size_dwords * 4u > cmd->memory->size) {
        return O0_ERANGE;
    }
    if (cmd->memory->residency != O0_GPU_RESIDENT) {
        return O0_EBUSY;
    }

    o0_spin_lock(&queue->lock);
    if (queue->state == O0_GPU_QUEUE_FAULTED) {
        o0_spin_unlock(&queue->lock);
        return O0_EIO;
    }
    if (queue->ops->is_quarantined(queue->engine)) {
        queue->state = O0_GPU_QUEUE_FAULTED;
        o0_spin_unlock(&queue->lock);
        return O0_EIO;
    }
    queue_reap_locked(queue);
    if (queue->fence_count >= O0_GPU_MAX_FENCES) {
        o0_spin_unlock(&queue->lock);
        return O0_ENOSPC;
    }
    o0_spin_unlock(&queue->lock);

    if (o0_gpu_memory_pin(cmd->memory) != O0_OK) {
        return O0_EBUSY;
    }
    o0_status status = queue->ops->validate(queue->engine, cmd);
    if (status != O0_OK) {
        (void)o0_gpu_memory_unpin(cmd->memory);
        return status;
    }

    o0_spin_lock(&queue->lock);
    if (queue->state == O0_GPU_QUEUE_FAULTED) {
        o0_spin_unlock(&queue->lock);
        (void)o0_gpu_memory_unpin(cmd->memory);
        return O0_EIO;
    }
    uint64_t seqno = queue->next_seqno + 1u;
    if (seqno == 0u) {
        seqno = 1u;
    }
    status = queue->ops->submit(queue->engine, cmd, seqno);
    if (status != O0_OK) {
        /* A failed submission leaves the stream position unknown: fault the queue
         * rather than pretending the ring is still usable. */
        queue_fault_locked(queue, status);
        o0_spin_unlock(&queue->lock);
        (void)o0_gpu_memory_unpin(cmd->memory);
        return status;
    }
    queue->next_seqno = seqno;
    queue->state = O0_GPU_QUEUE_ACTIVE;
    struct o0_gpu_fence *fence = &queue->fences[queue->fence_count++];
    memset(fence, 0, sizeof(*fence));
    fence->seqno = seqno;
    fence->state = O0_GPU_FENCE_PENDING;
    fence->error = O0_OK;
    fence->memory = cmd->memory;
    o0_spin_unlock(&queue->lock);

    *out_seqno = seqno;
    return O0_OK;
}

o0_status o0_gpu_queue_signal(struct o0_gpu_queue *queue, uint64_t seqno) {
    if (queue == NULL || seqno == 0u) {
        return O0_EINVAL;
    }
    o0_spin_lock(&queue->lock);
    bool found = false;
    for (uint32_t i = 0; i < queue->fence_count; ++i) {
        struct o0_gpu_fence *fence = &queue->fences[i];
        if (fence->seqno > seqno) {
            continue;
        }
        found = true;
        if (fence->state == O0_GPU_FENCE_PENDING) {
            fence->state = O0_GPU_FENCE_SIGNALED;
        }
    }
    if (found && seqno > queue->signaled_seqno) {
        queue->signaled_seqno = seqno;
    }
    o0_spin_unlock(&queue->lock);
    return found ? O0_OK : O0_ENOENT;
}

static o0_status fence_result(const struct o0_gpu_fence *fence) {
    switch (fence->state) {
    case O0_GPU_FENCE_SIGNALED:
        return O0_OK;
    case O0_GPU_FENCE_FAILED:
        return fence->error != O0_OK ? fence->error : O0_EIO;
    case O0_GPU_FENCE_CANCELLED:
        return O0_EIO;
    case O0_GPU_FENCE_PENDING:
    default:
        return O0_EAGAIN;
    }
}

o0_status o0_gpu_queue_wait(struct o0_gpu_queue *queue, uint64_t seqno, uint64_t timeout_ns,
                            const struct o0_gpu_clock *clock) {
    if (queue == NULL || clock == NULL || clock->now_ns == NULL || seqno == 0u) {
        return O0_EINVAL;
    }
    uint64_t deadline = clock->now_ns(clock->ctx) + timeout_ns;
    if (deadline < timeout_ns) {
        deadline = UINT64_MAX;
    }

    for (;;) {
        o0_spin_lock(&queue->lock);
        queue_reap_locked(queue);
        struct o0_gpu_fence *fence = find_fence(queue, seqno);
        if (fence == NULL) {
            o0_status result;
            if (seqno <= queue->signaled_seqno) {
                result = O0_OK;
            } else if (seqno <= queue->retired_seqno) {
                /* The device fetched the commands but reported no completion. */
                result = O0_EIO;
            } else {
                result = O0_ENOENT;
            }
            o0_spin_unlock(&queue->lock);
            return result;
        }
        o0_status result = fence_result(fence);
        if (result != O0_EAGAIN) {
            o0_spin_unlock(&queue->lock);
            return result;
        }
        o0_spin_unlock(&queue->lock);

        uint64_t now = clock->now_ns(clock->ctx);
        if (now >= deadline) {
            o0_spin_lock(&queue->lock);
            queue_fault_locked(queue, O0_ETIMEDOUT);
            o0_spin_unlock(&queue->lock);
            return O0_ETIMEDOUT;
        }
        if (clock->sleep_ns == NULL) {
            return O0_EINVAL;
        }
        uint64_t remaining = deadline - now;
        clock->sleep_ns(clock->ctx, remaining < 1000000ull ? remaining : 1000000ull);
    }
}

o0_status o0_gpu_queue_cancel(struct o0_gpu_queue *queue, uint64_t seqno) {
    if (queue == NULL || seqno == 0u) {
        return O0_EINVAL;
    }
    o0_spin_lock(&queue->lock);
    struct o0_gpu_fence *fence = find_fence(queue, seqno);
    if (fence == NULL) {
        o0_spin_unlock(&queue->lock);
        return O0_ENOENT;
    }
    if (fence->state != O0_GPU_FENCE_PENDING) {
        o0_spin_unlock(&queue->lock);
        return O0_EINVAL;
    }
    fence->state = O0_GPU_FENCE_CANCELLED;
    bool all_cancelled = true;
    for (uint32_t i = 0; i < queue->fence_count; ++i) {
        if (queue->fences[i].state != O0_GPU_FENCE_CANCELLED) {
            all_cancelled = false;
            break;
        }
    }
    if (all_cancelled) {
        queue->state = O0_GPU_QUEUE_CANCELLED;
    }
    o0_spin_unlock(&queue->lock);

    /* The commands may already be in the stream; the backend reports when the device
     * stops reading them. Cancellation never claims the work did not execute. */
    (void)queue->ops->cancel(queue->engine, seqno);
    return O0_OK;
}

o0_status o0_gpu_queue_recover(struct o0_gpu_queue *queue) {
    if (queue == NULL) {
        return O0_EINVAL;
    }
    o0_spin_lock(&queue->lock);
    if (queue->state != O0_GPU_QUEUE_FAULTED) {
        o0_spin_unlock(&queue->lock);
        return O0_OK;
    }
    o0_spin_unlock(&queue->lock);

    o0_status status = queue->ops->reset(queue->engine);
    if (status != O0_OK) {
        return status; /* No verified reset: the queue stays faulted and quarantined. */
    }

    o0_spin_lock(&queue->lock);
    for (uint32_t i = 0; i < queue->fence_count; ++i) {
        queue->fences[i].consumed = true;
        if (queue->fences[i].state == O0_GPU_FENCE_PENDING) {
            queue->fences[i].state = O0_GPU_FENCE_FAILED;
            queue->fences[i].error = O0_EIO;
        }
    }
    queue_reap_locked(queue);
    queue->state = O0_GPU_QUEUE_ACTIVE;
    o0_spin_unlock(&queue->lock);
    return O0_OK;
}

o0_status o0_gpu_queue_reap(struct o0_gpu_queue *queue) {
    if (queue == NULL) {
        return O0_EINVAL;
    }
    o0_spin_lock(&queue->lock);
    queue_reap_locked(queue);
    o0_spin_unlock(&queue->lock);
    return O0_OK;
}

o0_status o0_gpu_fence_query(const struct o0_gpu_queue *queue, uint64_t seqno,
                             struct o0_gpu_fence *out) {
    if (queue == NULL || out == NULL || seqno == 0u) {
        return O0_EINVAL;
    }
    for (uint32_t i = 0; i < queue->fence_count; ++i) {
        if (queue->fences[i].seqno == seqno) {
            *out = queue->fences[i];
            return O0_OK;
        }
    }
    return O0_ENOENT;
}
