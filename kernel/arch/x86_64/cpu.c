/* Ozero kernel - x86-64 CPU feature discovery.
 *
 * Detects baseline and optional CPU features using CPUID. Results are exposed
 * through the o0_cpu_features / o0_cpu_info contract in include/ozero/cpu.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/cpu.h"
#include "ozero/status.h"
#include "ozero/types.h"

static struct o0_cpu_features features;
static struct o0_cpu_info boot_cpu;
static bool features_detected;

static inline void cpuid(uint32_t leaf, uint32_t subleaf,
                         uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(subleaf));
}

static inline uint64_t xgetbv(uint32_t index) {
    uint32_t eax, edx;
    __asm__ volatile ("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
}

o0_status o0_cpu_detect(void) {
    if (features_detected) {
        return O0_EBUSY;
    }

    uint32_t eax, ebx, ecx, edx;

    /* Leaf 0: max basic leaf + vendor string. */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    features.max_basic_leaf = eax;
    boot_cpu.vendor[0] = (uint8_t)(ebx & 0xFF);
    boot_cpu.vendor[1] = (uint8_t)((ebx >> 8) & 0xFF);
    boot_cpu.vendor[2] = (uint8_t)((ebx >> 16) & 0xFF);
    boot_cpu.vendor[3] = (uint8_t)((ebx >> 24) & 0xFF);
    boot_cpu.vendor[4] = (uint8_t)(edx & 0xFF);
    boot_cpu.vendor[5] = (uint8_t)((edx >> 8) & 0xFF);
    boot_cpu.vendor[6] = (uint8_t)((edx >> 16) & 0xFF);
    boot_cpu.vendor[7] = (uint8_t)((edx >> 24) & 0xFF);
    boot_cpu.vendor[8] = (uint8_t)(ecx & 0xFF);
    boot_cpu.vendor[9] = (uint8_t)((ecx >> 8) & 0xFF);
    boot_cpu.vendor[10] = (uint8_t)((ecx >> 16) & 0xFF);
    boot_cpu.vendor[11] = (uint8_t)((ecx >> 24) & 0xFF);
    boot_cpu.vendor[12] = '\0';

    /* Leaf 1: processor info + feature bits. */
    if (features.max_basic_leaf >= 1) {
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        boot_cpu.family = (eax >> 8) & 0xF;
        boot_cpu.model = (eax >> 4) & 0xF;
        boot_cpu.stepping = eax & 0xF;
        if (boot_cpu.family == 0xF) {
            boot_cpu.family += (eax >> 20) & 0xFF;
        }
        if (boot_cpu.family >= 0x6) {
            boot_cpu.model += ((eax >> 16) & 0xF) << 4;
        }

        features.long_mode = false; /* determined from leaf 0x80000001 */
        features.pae = ((edx >> 6) & 1u) != 0;
        features.nx = false; /* determined from extended leaf */
        features.msr = ((edx >> 25) & 1u) != 0;
        features.apic = ((edx >> 9) & 1u) != 0;
        features.cmov = ((edx >> 15) & 1u) != 0;
        features.cx8 = ((edx >> 8) & 1u) != 0;
        features.clfsh = ((edx >> 19) & 1u) != 0;
        features.mtrr = ((edx >> 12) & 1u) != 0;

        features.sse = ((edx >> 25) & 1u) != 0;
        features.sse2 = ((edx >> 26) & 1u) != 0;
        features.fxsr = ((edx >> 24) & 1u) != 0;

        features.smep = ((ecx >> 7) & 1u) != 0;
        features.smap = ((ecx >> 20) & 1u) != 0;
        features.xsave = ((ecx >> 26) & 1u) != 0;
        features.umip = ((ecx >> 11) & 1u) != 0;
        features.serialize = ((ecx >> 6) & 1u) != 0;
        features.pge = ((edx >> 13) & 1u) != 0;
        features.pat = ((edx >> 16) & 1u) != 0;

        features.page_1gb = ((ecx >> 26) & 1u) != 0; /* wait, that's xsave */
        /* page_1gb is in ECX bit 26? No - let me recheck. */
        /* CPUID leaf 1 ECX bit 26 = XSAVE. Page 1GB is leaf 0x80000001 EDX bit 26. */
        features.page_1gb = false;
    }

    /* Extended leaf 0x80000000: max extended leaf. */
    cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    features.max_extended_leaf = eax;

    /* Extended leaf 0x80000001: extended processor info + feature bits. */
    if (features.max_extended_leaf >= 0x80000001) {
        cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);
        features.long_mode = ((edx >> 29) & 1u) != 0;
        features.page_1gb = ((edx >> 26) & 1u) != 0;
        features.nx = ((edx >> 20) & 1u) != 0;
        features.serialize = features.serialize || ((ecx >> 6) & 1u) != 0;
    }

    /* Extended leaf 0x80000007: advanced power management + TSC. */
    if (features.max_extended_leaf >= 0x80000007) {
        cpuid(0x80000007, 0, &eax, &ebx, &ecx, &edx);
        features.invariant_tsc = ((edx >> 8) & 1u) != 0;
    }

    /* Extended leaf 0x80000008: physical/virtual address sizes. */
    if (features.max_extended_leaf >= 0x80000008) {
        cpuid(0x80000008, 0, &eax, &ebx, &ecx, &edx);
        features.phys_addr_bits = (uint8_t)(eax & 0xFF);
        features.virt_addr_bits = (uint8_t)((eax >> 8) & 0xFF);
        features.clflush_size = (uint8_t)((ebx >> 8) & 0xFF) * 8;
        if (features.clflush_size == 0) {
            features.clflush_size = 64;
        }
    }

    /* XSAVE state. */
    if (features.xsave) {
        features.xcr0_supported = xgetbv(0);
    }

    /* Fill in boot CPU info. */
    boot_cpu.index = 0;
    boot_cpu.apic_id = 0;
    boot_cpu.acpi_uid = 0;
    boot_cpu.flags = O0_CPU_F_BSP;

    features_detected = true;
    return O0_OK;
}

const struct o0_cpu_features *o0_cpu_features(void) {
    return features_detected ? &features : NULL;
}

const struct o0_cpu_info *o0_cpu_info(uint32_t index) {
    if (index == 0 && features_detected) {
        return &boot_cpu;
    }
    return NULL;
}

uint32_t o0_cpu_count(void) {
    return 1;
}

uint32_t o0_cpu_index(void) {
    return 0;
}

uint64_t o0_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void o0_cpu_relax(void) {
    __asm__ volatile ("pause" ::: "memory");
}

void o0_cpu_idle(void) {
    __asm__ volatile ("sti; hlt" ::: "memory");
}

O0_NORETURN void o0_cpu_halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt" ::: "memory");
    }
}
