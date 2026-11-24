/* Ozero kernel - GDT, TSS and MSR installation (x86-64).
 *
 * Descriptor bytes come from o0_gdt_build() in kernel/src/abi.c so the host test
 * asserts the same values the kernel installs. See ADR 0007 §2.1-§2.3 and §2.8.
 */

#include <stddef.h>
#include <stdint.h>

#include "ozero/abi.h"
#include "ozero/cpu_local.h"
#include "ozero/gdt.h"
#include "ozero/types.h"
#include "ozero/uapi.h"

#define O0_MSR_EFER 0xc0000080u
#define O0_MSR_STAR 0xc0000081u
#define O0_MSR_LSTAR 0xc0000082u
#define O0_MSR_FMASK 0xc0000084u
#define O0_MSR_GS_BASE 0xc0000101u
#define O0_MSR_KERNEL_GS_BASE 0xc0000102u

#define O0_TSS_LIMIT (sizeof(struct o0_tss) - 1u)

struct __attribute__((packed)) o0_gdtr {
    uint16_t limit;
    uint64_t base;
};

extern void o0_syscall_entry(void);

static uint64_t gdt[O0_GDT_ENTRIES] O0_ALIGN(16);
static struct o0_tss tss O0_ALIGN(16);
static uint8_t double_fault_stack[8192] O0_ALIGN(16);

struct o0_cpu_local o0_cpu_local[O0_MAX_CPUS] O0_ALIGN(16);

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile ("wrmsr" : : "a"((uint32_t)value), "d"((uint32_t)(value >> 32)),
                      "c"(msr));
}

struct o0_cpu_local *o0_cpu_local_install(uint32_t index) {
    if (index >= O0_MAX_CPUS) {
        return NULL;
    }
    struct o0_cpu_local *local = &o0_cpu_local[index];
    local->self = local;
    local->cpu_index = index;
    write_msr(O0_MSR_GS_BASE, (uint64_t)(uintptr_t)local);
    /* User GS base before the first swapgs. */
    write_msr(O0_MSR_KERNEL_GS_BASE, 0);
    return local;
}

struct o0_cpu_local *o0_cpu_local_current(void) {
    uint64_t base = read_msr(O0_MSR_GS_BASE);
    if (base == 0) {
        return NULL;
    }
    return (struct o0_cpu_local *)(uintptr_t)base;
}

void o0_arch_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
    o0_cpu_local[0].kernel_rsp0 = rsp0;
}

uint64_t o0_gdt_tss_limit(void) {
    return O0_TSS_LIMIT;
}

void o0_gdt_init(void) {
    struct o0_gdt_view view;
    o0_gdt_build(&view, (uint64_t)(uintptr_t)&tss, (uint32_t)O0_TSS_LIMIT);
    for (size_t i = 0; i < O0_GDT_ENTRIES; ++i) {
        gdt[i] = view.entries[i];
    }

    /* TSS: RSP0 is replaced on every context switch; IST1 owns the dedicated
     * double-fault stack. The I/O bitmap is disabled. */
    for (size_t i = 0; i < sizeof(tss); ++i) {
        ((uint8_t *)&tss)[i] = 0;
    }
    tss.ist1 = (uint64_t)(uintptr_t)(double_fault_stack + sizeof(double_fault_stack));
    tss.iomap_base = (uint16_t)sizeof(struct o0_tss);

    const struct o0_gdtr descriptor = {
        .limit = (uint16_t)(sizeof(gdt) - 1u),
        .base = (uint64_t)(uintptr_t)gdt
    };
    __asm__ volatile ("lgdt %0" : : "m"(descriptor) : "memory");

    __asm__ volatile (
        "mov %[kd], %%ds\n"
        "mov %[kd], %%es\n"
        "mov %[kd], %%ss\n"
        "mov %[kd], %%fs\n"
        "mov %[kd], %%gs\n"
        :
        : [kd] "r"((uint16_t)O0_SEL_KERNEL_DATA)
        : "memory");

    __asm__ volatile (
        "pushq %[kc]\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        :
        : [kc] "r"((uint64_t)O0_SEL_KERNEL_CODE)
        : "rax", "memory");

    __asm__ volatile ("ltr %0" : : "r"((uint16_t)O0_SEL_TSS));

    o0_cpu_local_install(0);

    /* EFER.SCE enables SYSCALL/SYSRET; NXE was set with the CR3 switch. */
    uint64_t efer = read_msr(O0_MSR_EFER);
    efer |= 1ull; /* SCE */
    write_msr(O0_MSR_EFER, efer);

    write_msr(O0_MSR_STAR, O0_ABI_STAR);
    write_msr(O0_MSR_LSTAR, (uint64_t)(uintptr_t)o0_syscall_entry);
    write_msr(O0_MSR_FMASK, O0_ABI_FMASK);
}
