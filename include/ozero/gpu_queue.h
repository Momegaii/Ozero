/* Ozero kernel - GPU contexts, queues, submission and fences.
 *
 * GPU-independent policy layer. It owns context and queue lifetime, submission
 * ordering, fence sequence semantics, timeout handling and recovery rules. Every
 * vendor detail - ring registers, command encoding, completion signalling - stays
 * behind `struct o0_gpu_engine_ops`; nothing here may assume a page size, an engine
 * set or a Haswell ring layout. See docs/stage5-gpu.md sections 7 and 8.
 */
#ifndef O0_GPU_QUEUE_H
#define O0_GPU_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#include "ozero/gpu.h"
#include "ozero/status.h"
#include "ozero/sync.h"

/* Bounded tables. Both limits fail deterministically (O0_ENOSPC / O0_EBUSY) instead of
 * growing implicitly; a dynamic structure is a later decision, not an assumption. */
#define O0_GPU_MAX_QUEUES 8u
#define O0_GPU_MAX_FENCES 32u

/* Monotonic time source for bounded waits. Injected so the wait policy is deterministic
 * on the host and the GPU core does not depend on the scheduler. */
struct o0_gpu_clock {
    uint64_t (*now_ns)(void *ctx);
    void (*sleep_ns)(void *ctx, uint64_t ns);
    void *ctx;
};

/* Fence states. A fence is terminal once it leaves PENDING; the state records why. */
enum o0_gpu_fence_state {
    O0_GPU_FENCE_PENDING = 0,
    O0_GPU_FENCE_SIGNALED,
    O0_GPU_FENCE_FAILED,   /* backend error or timeout; see o0_gpu_fence.error */
    O0_GPU_FENCE_CANCELLED /* cancellation requested; hardware may still consume it */
};

struct o0_gpu_fence {
    uint64_t seqno;              /* 1-based; 0 is never a valid fence */
    enum o0_gpu_fence_state state;
    o0_status error;             /* meaningful when state is FAILED */
    bool consumed;               /* backend reported the hardware no longer reads it */
    struct o0_gpu_memory *memory; /* pinned backing, released when consumed */
};

/* One command buffer. `memory` is pinned for as long as the fence is outstanding, so
 * the backing pages cannot be freed or evicted while the device may still read them. */
struct o0_gpu_cmd {
    struct o0_gpu_memory *memory;
    uint64_t gpu_va;      /* batch address as the backend addresses it */
    uint32_t size_dwords;
    uint32_t used_dwords;
};

enum o0_gpu_queue_state {
    O0_GPU_QUEUE_ACTIVE = 0,
    O0_GPU_QUEUE_FAULTED,   /* timeout or backend error; submissions are refused */
    O0_GPU_QUEUE_CANCELLED  /* all outstanding work cancelled */
};

/* Backend engine contract.
 *
 * validate: reject a command buffer whose contents or referenced GPU addresses are not
 *   valid in the address space this engine binding represents. Called before submit.
 * submit: place the command buffer in the hardware stream and return only after the
 *   stream position for `seqno` is recorded.
 * poll: report whether the backend has completion evidence for `seqno`. Read-pointer
 *   progress alone is not completion: the backend must only report true when it has
 *   evidence that the submission finished.
 * retired: report whether the device has stopped reading the commands for `seqno`. This
 *   is resource-release evidence only. A retired submission without completion evidence
 *   can never be reported as successful.
 * cancel: stop reporting success for `seqno`. The backend must not release resources
 *   the device may still read; it reports consumption through poll.
 * reset: restore the engine to a known state. Backends return O0_ENOTSUP when the reset
 *   sequence is not verified for the target; the engine then stays quarantined.
 * is_quarantined: true when ownership is uncertain and no submission may be accepted.
 */
struct o0_gpu_engine_ops {
    const char *name;
    o0_status (*validate)(void *engine, const struct o0_gpu_cmd *cmd);
    o0_status (*submit)(void *engine, const struct o0_gpu_cmd *cmd, uint64_t seqno);
    o0_status (*poll)(void *engine, uint64_t seqno, bool *complete);
    o0_status (*retired)(void *engine, uint64_t seqno, bool *retired);
    o0_status (*cancel)(void *engine, uint64_t seqno);
    o0_status (*reset)(void *engine);
    bool (*is_quarantined)(void *engine);
};

struct o0_gpu_queue {
    struct o0_gpu_context *context;
    const struct o0_gpu_engine_ops *ops;
    void *engine;
    enum o0_gpu_queue_state state;
    uint64_t next_seqno;
    uint64_t signaled_seqno; /* highest seqno with completion evidence */
    uint64_t retired_seqno;  /* highest seqno the device stopped reading */
    struct o0_gpu_fence fences[O0_GPU_MAX_FENCES];
    uint32_t fence_count;
    o0_spinlock_t lock;
};

struct o0_gpu_context {
    uint32_t id;
    struct o0_gpu_vm *vm; /* address-space state owned by the context */
    uint32_t queue_count;
    bool destroyed;
    o0_spinlock_t lock;
};

/* ---- Contexts ---- */

o0_status o0_gpu_context_create(struct o0_gpu_vm *vm, struct o0_gpu_context **out);
/* Fails with O0_EBUSY while queues are still attached; the context is not freed. */
o0_status o0_gpu_context_destroy(struct o0_gpu_context *ctx);
uint32_t o0_gpu_context_queue_count(const struct o0_gpu_context *ctx);
struct o0_gpu_vm *o0_gpu_context_vm(const struct o0_gpu_context *ctx);

/* ---- Queues ---- */

/* A queue is bound to one context and one engine. `engine` is opaque to this layer and
 * is handed back to the backend ops; the caller keeps it alive for the queue lifetime. */
o0_status o0_gpu_queue_create(struct o0_gpu_context *ctx,
                              const struct o0_gpu_engine_ops *ops, void *engine,
                              struct o0_gpu_queue **out);
/* Fails with O0_EBUSY while any fence is still outstanding or unconsumed. */
o0_status o0_gpu_queue_destroy(struct o0_gpu_queue *queue);
enum o0_gpu_queue_state o0_gpu_queue_state(const struct o0_gpu_queue *queue);

/* Validate, pin and submit. Returns a fresh 1-based seqno on success. Refused with
 * O0_EIO once the queue is faulted or cancelled. */
o0_status o0_gpu_queue_submit(struct o0_gpu_queue *queue, const struct o0_gpu_cmd *cmd,
                              uint64_t *out_seqno);

/* Completion report from the backend's hardware path (interrupt or polling owner).
 * Fences up to and including `seqno` are signalled in submission order. */
o0_status o0_gpu_queue_signal(struct o0_gpu_queue *queue, uint64_t seqno);

/* Bounded wait. Polls the backend, sleeps through the injected clock, and faults the
 * queue with O0_ETIMEDOUT when the deadline passes. A submission the device consumed
 * without completion evidence returns O0_EIO, never O0_OK. */
o0_status o0_gpu_queue_wait(struct o0_gpu_queue *queue, uint64_t seqno, uint64_t timeout_ns,
                            const struct o0_gpu_clock *clock);

/* Cancel one outstanding fence. The fence reports CANCELLED immediately, but stays
 * pinned until the backend confirms the device no longer reads its commands. */
o0_status o0_gpu_queue_cancel(struct o0_gpu_queue *queue, uint64_t seqno);

/* Attempt recovery after a fault. Only a backend with a verified reset sequence can
 * succeed; otherwise the queue stays FAULTED and the engine stays quarantined. */
o0_status o0_gpu_queue_recover(struct o0_gpu_queue *queue);

/* Drop fences the backend has confirmed consumed. Called implicitly by submit and
 * destroy; exposed so an interrupt handler can reap without waiting. */
o0_status o0_gpu_queue_reap(struct o0_gpu_queue *queue);

o0_status o0_gpu_fence_query(const struct o0_gpu_queue *queue, uint64_t seqno,
                             struct o0_gpu_fence *out);

#endif /* O0_GPU_QUEUE_H */
