/* Ozero kernel - GDT, TSS and the architectural entry state.
 *
 * The GDT layout is fixed by include/ozero/uapi.h: kernel code/data, user
 * data/code and a 64-bit TSS. The TSS provides RSP0 for ring-3 -> ring-0
 * transitions and IST1 for the double-fault stack.
 */
#ifndef O0_GDT_H
#define O0_GDT_H

#include <stdint.h>

#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct __attribute__((packed)) o0_tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

/* Installs the GDT/TSS, loads the segment registers, programs STAR/LSTAR/FMASK,
 * enables EFER.SCE and installs the per-CPU GS bases. Must run after paging is
 * active and before user mode is entered. */
void o0_gdt_init(void);

/* Updates TSS.RSP0 (and its per-CPU mirror). */
void o0_arch_set_rsp0(uint64_t rsp0);

uint64_t o0_gdt_tss_limit(void);

#ifdef __cplusplus
}
#endif

#endif /* O0_GDT_H */
