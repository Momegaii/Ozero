/* Ozero kernel - SMP bring-up and cross-CPU TLB invalidation. */
#ifndef O0_SMP_H
#define O0_SMP_H

#include <stdint.h>

#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct limine_mp_response;
struct limine_mp_info;

/* Releases every AP described by the bootloader's MP response. Returns the
 * number of CPUs online, including the BSP. */
uint32_t o0_smp_start(struct limine_mp_response *response);

/* CPUs that reached the online marker, including the BSP. */
uint32_t o0_smp_online_count(void);
uint32_t o0_smp_ipi_seen(void);

/* Invalidates `vaddr` locally and on every other online CPU. The IPI carries
 * one address; this is a single-page shootdown, not a range operation. */
o0_status o0_tlb_shootdown(uint64_t vaddr);

/* AP entry point and its C continuation (called from kernel/arch/x86_64). */
void o0_ap_entry(struct limine_mp_info *info);
void o0_ap_entry_c(struct limine_mp_info *info);

#ifdef __cplusplus
}
#endif

#endif /* O0_SMP_H */
