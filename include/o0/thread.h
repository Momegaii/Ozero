/* Ozero kernel - processes, threads and address spaces.
 *
 * A2 needs stable concepts, not a final Windows ABI. Handle and syscall details are in
 * ipc.h and are explicitly unfrozen.
 */
#ifndef O0_THREAD_H
#define O0_THREAD_H

#include "o0/mm.h"
#include "o0/status.h"
#include "o0/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct o0_thread_attr {
    const char *name;
    void (*entry)(void *arg);
    void *arg;
    size_t stack_size; /* 0 selects the kernel default */
    int32_t priority;  /* 0 is normal; larger is higher priority */
    uint32_t flags;
};

/* ---- Processes ---- */

O0_NODISCARD o0_status o0_process_create(const char *name, o0_as_t *as,
                                         o0_process_t **out);
void o0_process_destroy(o0_process_t *proc);
o0_process_t *o0_process_current(void);
o0_as_t *o0_process_as(o0_process_t *proc);
const char *o0_process_name(const o0_process_t *proc);

/* ---- Threads ---- */

O0_NODISCARD o0_status o0_thread_create(o0_process_t *proc,
                                        const struct o0_thread_attr *attr,
                                        o0_thread_t **out);
O0_NORETURN void o0_thread_exit(int status);
o0_status o0_thread_join(o0_thread_t *thread, int *status);
o0_thread_t *o0_thread_self(void);
o0_process_t *o0_thread_process(const o0_thread_t *thread);
const char *o0_thread_name(const o0_thread_t *thread);

void o0_thread_yield(void);
void o0_thread_sleep_ns(uint64_t ns);

#ifdef __cplusplus
}
#endif

#endif /* O0_THREAD_H */
