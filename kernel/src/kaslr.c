/* Ozero kernel - KASLR layout derivation. */

#include <stdbool.h>
#include <stdint.h>

#include "ozero/kaslr.h"

static uint64_t layout_offset;
static bool initialised;

static uint64_t read_tsc(void) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static bool cpu_has_rdrand(void) {
    uint32_t eax = 1;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    (void)ebx;
    (void)edx;
    return (ecx & (1u << 30)) != 0;
}

static uint64_t hardware_seed(void) {
    if (cpu_has_rdrand()) {
        for (uint32_t i = 0; i < 16u; ++i) {
            uint64_t value = 0;
            unsigned char ok = 0;
            __asm__ volatile ("rdrand %0; setc %1" : "=r"(value), "=qm"(ok));
            if (ok != 0) {
                return value ^ read_tsc();
            }
        }
    }
    return read_tsc() ^ ((uint64_t)(uintptr_t)&layout_offset << 17);
}

void o0_kaslr_init(void) {
    if (initialised) {
        return;
    }
    layout_offset = o0_kaslr_offset_from_seed(hardware_seed());
    initialised = true;
}

uint64_t o0_kaslr_offset(void) {
    return layout_offset;
}

uint64_t o0_kaslr_kstack_base(void) {
    return O0_KASLR_KSTACK_BASE + layout_offset;
}
