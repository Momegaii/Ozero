/* Ozero kernel - thread and process control blocks (internal).
 *
 * Kernel threads only in Stage 3: every thread runs in ring 0 on its own kernel
 * stack. User-mode entry, syscall and TLS programming belong to later stages and
 * are tracked in docs/decisions/0002-stage3-thread-model.md.
 */
#ifndef O0_THREAD_INTERNAL_H
#define O0_THREAD_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/mm.h"
#include "ozero/thread.h"
#include "ozero/types.h"

#define O0_MAX_THREADS 32u
#define O0_MAX_PROCESSES 8u
#define O0_THREAD_NAME_MAX 24u
#define O0_PROCESS_NAME_MAX 32u
#define O0_THREAD_STACK_PAGES 4u
#define O0_THREAD_PRIORITY_IDLE (-1000)

enum o0_thread_state {
    O0_THREAD_UNUSED = 0,
    O0_THREAD_READY = 1,
    O0_THREAD_RUNNING = 2,
    O0_THREAD_BLOCKED = 3,
    O0_THREAD_EXITED = 4
};

enum o0_thread_block_reason {
    O0_BLOCK_NONE = 0,
    O0_BLOCK_SLEEP = 1,
    O0_BLOCK_JOIN = 2,
    O0_BLOCK_MUTEX = 3
};

struct o0_thread {
    uint64_t rsp;                     /* saved stack pointer while not running */
    struct o0_thread *next_ready;     /* ready-queue link */
    struct o0_thread *wait_next;      /* wait-list link (mutexes) */
    struct o0_thread *join_waiter;    /* thread blocked in o0_thread_join */
    struct o0_process *proc;

    void (*entry)(void *arg);
    void *arg;

    o0_vaddr_t stack_base;
    size_t stack_size;
    o0_vaddr_t kstack_base;   /* mapped kernel stack; guard page below */
    size_t kstack_pages;
    uint64_t kernel_stack_top; /* TSS.RSP0 for this thread */
    bool user;
    uint64_t user_entry;
    uint64_t user_rsp;

    uint64_t wake_tick;
    int exit_status;

    uint32_t id;
    uint32_t state;
    uint32_t block_reason;
    uint32_t timeslice;

    int32_t priority;

    bool preemptible;
    bool exited;
    bool joined;

    char name[O0_THREAD_NAME_MAX];

    void *user_ptr;
    uint64_t gs_base;
    uint64_t fs_base;
};

struct o0_process {
    uint32_t pid;
    bool in_use;
    bool user;
    uint32_t thread_count;
    char name[O0_PROCESS_NAME_MAX];
    o0_as_t *as;
};

/* Thread registry (thread.c). */
struct o0_thread *o0_thread_alloc_slot(void);
void o0_thread_release_slot(struct o0_thread *thread);
/* Frees the kernel stack and, when the last thread leaves, the process and its
 * user address space. The thread must not be running. */
void o0_thread_free_resources(struct o0_thread *thread);
struct o0_thread *o0_thread_by_index(uint32_t index);
uint32_t o0_thread_count(void);
void o0_thread_tick(uint64_t now);
void o0_thread_wake(struct o0_thread *thread);
bool o0_thread_init_system(void);
/* A ring-3 fault: contained, kills only the faulting thread. */
O0_NORETURN void o0_thread_user_fault(uint64_t rip, uint64_t cr2, uint64_t error);

/* Scheduler (sched.c). */
void o0_sched_init(void);
O0_NORETURN void o0_sched_start(void);
struct o0_thread *o0_sched_current(void);
void o0_sched_enqueue(struct o0_thread *thread);
void o0_sched_yield(void);
void o0_sched_block(uint32_t reason);
void o0_sched_tick(void);
uint64_t o0_sched_ticks(void);
void o0_sched_set_hz(uint32_t hz);
uint32_t o0_sched_hz(void);
uint32_t o0_sched_ready_count(void);
void o0_sched_set_preemption(bool enabled);
bool o0_sched_preemption_enabled(void);
void o0_sched_note_resume(struct o0_thread *thread);

#endif /* O0_THREAD_INTERNAL_H */
