/* Ozero kernel - SMP bring-up and cross-CPU TLB invalidation. */
#ifndef O0_SMP_H
#define O0_SMP_H

#include <stddef.h>
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

/* Invalidates one page locally and on every other online CPU. */
o0_status o0_tlb_shootdown(uint64_t vaddr);
/* Flushes [vaddr, vaddr+len) on every online CPU, with a bounded ACK wait. */
o0_status o0_tlb_shootdown_range(uint64_t vaddr, size_t len);
/* Flushes every TLB entry on every online CPU. */
o0_status o0_tlb_shootdown_all(void);

/* AP entry point and its C continuation (called from kernel/arch/x86_64). */
void o0_ap_entry(struct limine_mp_info *info);
void o0_ap_entry_c(struct limine_mp_info *info);

#ifdef __cplusplus
}
#endif

#endif /* O0_SMP_H */
