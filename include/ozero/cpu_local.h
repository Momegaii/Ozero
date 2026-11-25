/* Ozero kernel - per-CPU state.
 *
 * One slot per CPU is reserved from the start (O0_MAX_CPUS == 1 in Wave 2) so
 * SMP bring-up fills slots instead of changing the ABI, the syscall entry stub
 * or the swapgs convention. The first field is the self pointer: swapgs gives a
 * stable base, and the assembly stubs address the remaining fields by the
 * offsets below.
 */
#ifndef O0_CPU_LOCAL_H
#define O0_CPU_LOCAL_H

#include <stdint.h>

#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_MAX_CPUS 1u

#ifdef O0_LOCK_ORDER_CHECK
#define O0_MAX_LOCK_DEPTH 16
#else
#define O0_MAX_LOCK_DEPTH 0
#endif

struct o0_cpu_local {
    struct o0_cpu_local *self;       /* offset 0 */
    uint64_t kernel_rsp0;            /* offset 8  : TSS.RSP0 mirror */
    uint64_t current;                /* offset 16 : struct o0_thread * */
    uint64_t user_rsp;               /* offset 24 : scratch across entry */
    uint64_t usercopy_fixup;         /* offset 32 : 0 or fixup rip */
    uint64_t usercopy_expected;      /* offset 40 : fault rip to match */
    uint32_t cpu_index;              /* offset 48 */
    uint32_t preempt_depth;          /* offset 52 */
#ifdef O0_LOCK_ORDER_CHECK
    uint32_t lock_depth;             /* offset 56 : lock checker nesting depth */
    const void *held_locks[O0_MAX_LOCK_DEPTH]; /* offset 60 : held ordered locks */
#endif
};

#define O0_CPU_LOCAL_OFFSET_SELF 0u
#define O0_CPU_LOCAL_OFFSET_KERNEL_RSP0 8u
#define O0_CPU_LOCAL_OFFSET_CURRENT 16u
#define O0_CPU_LOCAL_OFFSET_USER_RSP 24u
#define O0_CPU_LOCAL_OFFSET_USERCOPY_FIXUP 32u
#define O0_CPU_LOCAL_OFFSET_USERCOPY_EXPECTED 40u

extern struct o0_cpu_local o0_cpu_local[O0_MAX_CPUS];

/* Installs the per-CPU GS bases and returns the local block for `index`. */
struct o0_cpu_local *o0_cpu_local_install(uint32_t index);

/* Kernel GS base currently in force (from IA32_GS_BASE). */
struct o0_cpu_local *o0_cpu_local_current(void);

/* Updates the TSS.RSP0 mirror used by ring-3 -> ring-0 transitions. */
void o0_cpu_set_rsp0(uint64_t rsp0);

#ifdef __cplusplus
}
#endif

#endif /* O0_CPU_LOCAL_H */
