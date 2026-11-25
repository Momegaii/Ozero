/* Ozero kernel - CPU features and per-CPU state.
 *
 * Feature bits are decoded from the CPUID leaves in kernel/arch/x86_64/cpu.c; the boot
 * path refuses to continue without long mode and PAE.
 */
#ifndef O0_CPU_H
#define O0_CPU_H

#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct o0_cpu_features {
    /* Required baseline. The boot path refuses to continue without long_mode and pae. */
    bool long_mode;
    bool pae;
    bool nx;
    bool sse;
    bool sse2;
    bool fxsr;
    bool msr;
    bool apic;
    bool tsc;
    bool cmov;
    bool cx8;
    bool clfsh;

    /* Optional features with defined fallbacks. */
    bool x2apic;
    bool tsc_deadline;
    bool invariant_tsc;
    bool rdtscp;
    bool pcid;
    bool invpcid;
    bool smep;
    bool smap;
    bool umip;
    bool fsgsbase;
    bool la57;
    bool page_1gb;
    bool xsave;
    bool avx;
    bool avx2;
    bool avx512f;
    bool aesni;
    bool rdrand;
    bool rdseed;
    bool pku;
    bool ospke;
    bool cet_ibt;
    bool cet_ss;
    bool syscall;
    bool serialize;
    bool pat;
    bool pge;
    bool mtrr;
    bool hypervisor;

    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;
    uint8_t phys_addr_bits;
    uint8_t virt_addr_bits;
    uint8_t clflush_size;
    uint8_t reserved0;
    uint64_t xcr0_supported; /* XGETBV(0) when XSAVE+OSXSAVE are enabled, else 0 */
};

struct o0_cpu_info {
    uint32_t index;
    uint32_t apic_id;
    uint32_t acpi_uid;
    uint32_t flags;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    char vendor[13];
    char brand[49];
    char hypervisor[13];
    uint32_t hypervisor_signature;
};

#define O0_CPU_F_BSP (1u << 0)

/* Detect CPU features once on the BSP. Must be called before o0_cpu_features(). */
o0_status o0_cpu_detect(void);

/* The feature set is detected once on the BSP; APs must match it. */
const struct o0_cpu_features *o0_cpu_features(void);
const struct o0_cpu_info *o0_cpu_info(uint32_t index);
uint32_t o0_cpu_count(void);

/* Records the processor count discovered from the MP or MADT response. */
void o0_cpu_set_count(uint32_t count);

/* Fills one CPU record. Index 0 is the BSP; higher indices are APs. */
void o0_cpu_set_info(uint32_t index, uint32_t apic_id, uint32_t acpi_uid,
                     uint32_t flags);

/* Index of the current CPU, or UINT32_MAX before per-CPU data is installed. */
uint32_t o0_cpu_index(void);

/* Enables CR4.SMEP and CR4.SMAP when CPUID advertises them. Returns the mask
 * that was actually enabled: bit 0 = SMEP, bit 1 = SMAP. */
uint32_t o0_cpu_enable_smep_smap(void);

/* True when CR4.SMAP is currently enabled on this CPU. */
bool o0_cpu_smap_enabled(void);

uint64_t o0_rdtsc(void);
void o0_cpu_relax(void);
void o0_cpu_idle(void); /* hlt; returns when an interrupt is delivered */
O0_NORETURN void o0_cpu_halt_forever(void);

#ifdef __cplusplus
}
#endif

#endif /* O0_CPU_H */
