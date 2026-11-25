/* Ozero kernel - per-CPU state.
 *
 * One slot per CPU is reserved from the start (O0_MAX_CPUS == 4) so
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

#define O0_MAX_CPUS 4u

struct o0_cpu_local {
    struct o0_cpu_local *self;       /* offset 0 */
    uint64_t kernel_rsp0;            /* offset 8  : TSS.RSP0 mirror */
    uint64_t current;                /* offset 16 : struct o0_thread * */
    uint64_t user_rsp;               /* offset 24 : scratch across entry */
    uint64_t usercopy_fixup;         /* offset 32 : 0 or fixup rip */
    uint64_t usercopy_expected;      /* offset 40 : fault rip to match */
    uint32_t cpu_index;              /* offset 48 */
    uint32_t preempt_depth;          /* offset 52 */
    uint32_t apic_id;                /* offset 56 */
    uint32_t online;                 /* offset 60 */
    volatile uint32_t tlb_pending;   /* offset 64 */
    volatile uint32_t tlb_ack;       /* offset 68 */
    volatile uint64_t tlb_addr;      /* offset 72 */
    volatile uint32_t tlb_len;       /* offset 80 : pages, or 0 for one page */
    volatile uint32_t tlb_all;       /* offset 84 : non-zero means full flush */
    volatile uint32_t resched_pending; /* offset 88 */
    uint32_t lock_depth;             /* offset 92 */
    void *held_locks[8];             /* offset 96 */
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

/* Marks a slot online after its AP has finished architectural setup. */
void o0_cpu_local_mark_online(uint32_t index, uint32_t apic_id);

/* Number of slots that have reached the online marker. */
uint32_t o0_cpu_local_online_count(void);

/* Kernel GS base currently in force (from IA32_GS_BASE). */
struct o0_cpu_local *o0_cpu_local_current(void);

/* Updates the TSS.RSP0 mirror used by ring-3 -> ring-0 transitions. */
void o0_cpu_set_rsp0(uint64_t rsp0);

#ifdef __cplusplus
}
#endif

#endif /* O0_CPU_LOCAL_H */
