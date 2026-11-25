/* Ozero kernel - scheduler unit test (host build).
 *
 * kernel/src/sched.c is included with the architecture pieces stubbed out, so
 * the selection, accounting and preemption rules run on the host without any
 * real context switching.
 *
 * Run it with `make test`.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/cpu.h"
#include "ozero/cpu_local.h"
#include "ozero/irq.h"
#include "ozero/status.h"
#include "ozero/thread.h"
#include "ozero/types.h"

#include "../kernel/src/thread_internal.h"

/* ---- stubs ---- */

struct o0_as {
    int unused;
};

static struct o0_as kernel_as_stub;

o0_as_t *o0_as_kernel(void) {
    return &kernel_as_stub;
}

o0_as_t *o0_as_active(void) {
    return &kernel_as_stub;
}

o0_status o0_as_activate(o0_as_t *as) {
    (void)as;
    return O0_OK;
}

void o0_arch_set_rsp0(uint64_t rsp0) {
    (void)rsp0;
}

struct o0_cpu_local o0_cpu_local[O0_MAX_CPUS];

static uint32_t switches;
static uint32_t idle_calls;
static uint32_t thread_tick_calls;

void x86_64_context_switch(uint64_t *save_rsp, uint64_t new_rsp) {
    /* Pretend the switch happened: record it so the test can assert on it. */
    (void)save_rsp;
    (void)new_rsp;
    ++switches;
}

void o0_thread_tick(uint64_t now) {
    (void)now;
    ++thread_tick_calls;
}

void o0_thread_reap_detached(void) {}

void o0_cpu_idle(void) {
    ++idle_calls;
}

o0_irq_state_t o0_irq_save(void) { return 0; }
void o0_irq_restore(o0_irq_state_t state) { (void)state; }

/* Threads are represented by plain structures here; only the pieces the
 * scheduler touches are implemented. */
const char *o0_thread_name(const o0_thread_t *thread) {
    return thread != NULL ? thread->name : "";
}

void o0_thread_wake(struct o0_thread *thread) {
    if (thread == NULL || thread->state != O0_THREAD_BLOCKED) {
        return;
    }
    thread->state = O0_THREAD_READY;
    thread->block_reason = O0_BLOCK_NONE;
    o0_sched_enqueue(thread);
}

#include "../kernel/src/sched.c"

static int failures;
static int tests_run;

#define TEST(name)                                            \
    do {                                                      \
        ++tests_run;                                          \
        printf("  TEST: %s ... ", name);                      \
        fflush(stdout);                                       \
    } while (0)

#define PASS()                                                \
    do {                                                      \
        printf("PASS\n");                                     \
    } while (0)

#define FAIL(msg)                                             \
    do {                                                      \
        printf("FAIL: %s\n", msg);                            \
        ++failures;                                           \
    } while (0)

#define ASSERT(cond, msg)                                     \
    do {                                                      \
        if (!(cond)) {                                        \
            FAIL(msg);                                        \
            return;                                           \
        }                                                     \
    } while (0)

static struct o0_thread pool[8];
static uint32_t pool_used;

static struct o0_thread *make_thread(const char *name, int32_t priority) {
    struct o0_thread *thread = &pool[pool_used++];
    memset(thread, 0, sizeof(*thread));
    snprintf(thread->name, sizeof(thread->name), "%s", name);
    thread->priority = priority;
    thread->state = O0_THREAD_READY;
    thread->timeslice = 5;
    thread->preemptible = true;
    thread->rsp = 0x1000;
    return thread;
}

static void reset_scheduler(void) {
    pool_used = 0;
    switches = 0;
    idle_calls = 0;
    thread_tick_calls = 0;
    o0_sched_init();
    o0_sched_set_hz(100);
    o0_sched_set_preemption(false);
}

static void test_priority_selection(void) {
    TEST("priority selection");
    reset_scheduler();

    struct o0_thread *low = make_thread("low", 0);
    struct o0_thread *high = make_thread("high", 10);
    struct o0_thread *mid = make_thread("mid", 5);

    o0_sched_enqueue(low);
    o0_sched_enqueue(high);
    o0_sched_enqueue(mid);
    ASSERT(o0_sched_ready_count() == 3, "ready count should be 3");

    o0_sched_yield();
    ASSERT(strcmp(o0_thread_name(o0_sched_current()), "high") == 0,
           "highest priority thread should run first");
    ASSERT(switches == 1, "exactly one context switch expected");
    /* The idle thread is queued back, so low, mid and idle remain. */
    ASSERT(o0_sched_ready_count() == 3, "preempted idle thread should be queued");

    o0_sched_yield();
    ASSERT(strcmp(o0_thread_name(o0_sched_current()), "mid") == 0,
           "next highest priority should run second");
    PASS();
}

static void test_fifo_within_priority(void) {
    TEST("FIFO within one priority");
    reset_scheduler();

    struct o0_thread *first = make_thread("first", 0);
    struct o0_thread *second = make_thread("second", 0);
    o0_sched_enqueue(first);
    o0_sched_enqueue(second);

    o0_sched_yield();
    ASSERT(strcmp(o0_thread_name(o0_sched_current()), "first") == 0,
           "earlier queued thread should run first");
    PASS();
}

static void test_blocking(void) {
    TEST("blocking switches away");
    reset_scheduler();

    struct o0_thread *waiter = make_thread("waiter", 0);
    o0_sched_enqueue(waiter);

    struct o0_thread *idle = o0_sched_current();
    o0_sched_block(O0_BLOCK_SLEEP);
    ASSERT(o0_sched_current() == waiter, "blocked thread should yield the CPU");
    ASSERT(idle->state == O0_THREAD_BLOCKED, "idle thread should be blocked");
    ASSERT(idle->block_reason == O0_BLOCK_SLEEP, "block reason not recorded");

    o0_thread_wake(idle);
    ASSERT(idle->state == O0_THREAD_READY, "woken thread should be ready");
    ASSERT(o0_sched_ready_count() == 1, "woken thread should be queued");
    PASS();
}

static void test_tick_and_preemption(void) {
    TEST("tick and preemption");
    reset_scheduler();

    uint64_t before = o0_sched_ticks();
    o0_sched_tick();
    ASSERT(o0_sched_ticks() == before + 1, "tick counter should advance");
    ASSERT(thread_tick_calls == 1, "sleeper scan should run on every tick");

    struct o0_thread *runner = make_thread("runner", 0);
    o0_sched_enqueue(runner);
    o0_sched_yield();
    ASSERT(o0_sched_current() == runner, "runner should be current");

    /* Preemption off: a higher-priority thread waits. */
    struct o0_thread *urgent = make_thread("urgent", 50);
    o0_sched_enqueue(urgent);
    o0_sched_tick();
    ASSERT(o0_sched_current() == runner, "preemption is disabled: no switch expected");

    /* Preemption on: the higher-priority thread takes over immediately. */
    o0_sched_set_preemption(true);
    o0_sched_tick();
    ASSERT(o0_sched_current() == urgent, "higher priority thread should preempt");
    PASS();
}

static void test_timeslice_rotation(void) {
    TEST("timeslice rotation");
    reset_scheduler();

    struct o0_thread *a = make_thread("a", 0);
    struct o0_thread *b = make_thread("b", 0);
    o0_sched_enqueue(a);
    o0_sched_enqueue(b);
    o0_sched_yield();
    ASSERT(o0_sched_current() == a, "a should run first");

    o0_sched_set_preemption(true);
    uint32_t switched_to_b = 0;
    for (uint32_t i = 0; i < 10; ++i) {
        o0_sched_tick();
        if (o0_sched_current() == b) {
            switched_to_b = 1;
            break;
        }
    }
    ASSERT(switched_to_b == 1, "equal-priority threads should rotate on timeslice");
    PASS();
}

int main(void) {
    printf("Ozero scheduler unit tests\n\n");

    test_priority_selection();
    test_fifo_within_priority();
    test_blocking();
    test_tick_and_preemption();
    test_timeslice_rotation();

    printf("\n%d test(s) run, %d failure(s)\n", tests_run, failures);
    return failures == 0 ? 0 : 1;
}
