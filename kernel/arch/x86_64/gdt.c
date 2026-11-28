/* Ozero kernel - GDT, TSS and MSR installation (x86-64).
 *
 * Descriptor bytes come from o0_gdt_build() in kernel/src/abi.c so the host test
 * asserts the same values the kernel installs. See ADR 0007 §2.1-§2.3 and §2.8.
 */

#include <stddef.h>
#include <stdint.h>

#include "ozero/abi.h"
#include "ozero/cpu.h"
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

/* IST stacks per CPU:
 * IST1: double fault, 8 KiB
 * IST2: NMI, 8 KiB
 * IST3: machine check, 4 KiB
 */
#define O0_IST1_STACK_BYTES 8192u
#define O0_IST2_STACK_BYTES 8192u
#define O0_IST3_STACK_BYTES 4096u
#define O0_IST_STACK_TOTAL (O0_IST1_STACK_BYTES + O0_IST2_STACK_BYTES + O0_IST3_STACK_BYTES)

struct __attribute__((packed)) o0_gdtr {
    uint16_t limit;
    uint64_t base;
};

extern void o0_syscall_entry(void);

static uint64_t gdt_images[O0_MAX_CPUS][O0_GDT_ENTRIES] O0_ALIGN(16);
static struct o0_tss o0_tss[O0_MAX_CPUS] O0_ALIGN(16);
static uint8_t ist_stacks[O0_MAX_CPUS][O0_IST_STACK_TOTAL] O0_ALIGN(16);

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
    local->online = 0;
    local->tss_ready = 0;
    write_msr(O0_MSR_GS_BASE, (uint64_t)(uintptr_t)local);
    /* User GS base before the first swapgs. */
    write_msr(O0_MSR_KERNEL_GS_BASE, 0);
    return local;
}

void o0_cpu_local_mark_online(uint32_t index, uint32_t apic_id) {
    if (index >= O0_MAX_CPUS) {
        return;
    }
    struct o0_cpu_local *local = &o0_cpu_local[index];
    local->apic_id = apic_id;
    local->online = 1;
    __atomic_store_n(&local->online, 1u, __ATOMIC_RELEASE);
}

uint32_t o0_cpu_local_online_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < O0_MAX_CPUS; ++i) {
        if (__atomic_load_n(&o0_cpu_local[i].online, __ATOMIC_ACQUIRE) != 0) {
            ++count;
        }
    }
    return count;
}

struct o0_cpu_local *o0_cpu_local_current(void) {
    uint64_t base = read_msr(O0_MSR_GS_BASE);
    if (base == 0) {
        return NULL;
    }
    return (struct o0_cpu_local *)(uintptr_t)base;
}

void o0_arch_set_rsp0(uint64_t rsp0) {
    uint32_t cpu = o0_cpu_index();
    if (cpu < O0_MAX_CPUS) {
        o0_tss[cpu].rsp0 = rsp0;
        struct o0_cpu_local *local = o0_cpu_local_current();
        if (local == NULL) {
            local = &o0_cpu_local[0];
        }
        local->kernel_rsp0 = rsp0;
    }
}

uint64_t o0_gdt_tss_limit(void) {
    return O0_TSS_LIMIT;
}

static void gdt_load_segments_for_cpu(uint32_t cpu) {
    const struct o0_gdtr descriptor = {
        .limit = (uint16_t)(sizeof(gdt_images[0]) - 1u),
        .base = (uint64_t)(uintptr_t)gdt_images[cpu]
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
}

static void gdt_install_syscall_msrs(void) {
    /* EFER.SCE enables SYSCALL/SYSRET; NXE was set with the CR3 switch. */
    uint64_t efer = read_msr(O0_MSR_EFER);
    efer |= 1ull; /* SCE */
    write_msr(O0_MSR_EFER, efer);

    write_msr(O0_MSR_STAR, O0_ABI_STAR);
    write_msr(O0_MSR_LSTAR, (uint64_t)(uintptr_t)o0_syscall_entry);
    write_msr(O0_MSR_FMASK, O0_ABI_FMASK);
}

static void tss_init_for_cpu(uint32_t cpu) {
    struct o0_tss *tss = &o0_tss[cpu];
    uint8_t *ist = &ist_stacks[cpu][0];

    for (size_t i = 0; i < sizeof(*tss); ++i) {
        ((uint8_t *)tss)[i] = 0;
    }

    /* IST1: double fault stack (8 KiB) */
    tss->ist1 = (uint64_t)(uintptr_t)(ist + O0_IST1_STACK_BYTES);
    /* IST2: NMI stack (8 KiB) */
    tss->ist2 = (uint64_t)(uintptr_t)(ist + O0_IST1_STACK_BYTES + O0_IST2_STACK_BYTES);
    /* IST3: machine check stack (4 KiB) */
    tss->ist3 = (uint64_t)(uintptr_t)(ist + O0_IST1_STACK_BYTES + O0_IST2_STACK_BYTES + O0_IST3_STACK_BYTES);
    tss->iomap_base = (uint16_t)sizeof(struct o0_tss);
}

void o0_gdt_init(void) {
    /* Build BSP GDT image with BSP TSS */
    struct o0_gdt_view view;
    o0_gdt_build(&view, (uint64_t)(uintptr_t)&o0_tss[0], (uint32_t)O0_TSS_LIMIT);
    for (size_t i = 0; i < O0_GDT_ENTRIES; ++i) {
        gdt_images[0][i] = view.entries[i];
    }

    /* Initialize BSP TSS and IST stacks */
    tss_init_for_cpu(0);

    gdt_load_segments_for_cpu(0);
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)O0_SEL_TSS));

    o0_cpu_local_install(0);
    o0_cpu_local_mark_online(0, 0);
    o0_cpu_local[0].tss_ready = 1;

    gdt_install_syscall_msrs();
}

void o0_gdt_init_ap(uint32_t cpu_index) {
    if (cpu_index == 0u || cpu_index >= O0_MAX_CPUS) {
        for (;;) {
            __asm__ volatile ("cli; hlt" ::: "memory");
        }
    }

    /* Build per-CPU GDT image with this CPU's TSS */
    struct o0_gdt_view view;
    o0_gdt_build(&view, (uint64_t)(uintptr_t)&o0_tss[cpu_index], (uint32_t)O0_TSS_LIMIT);
    for (size_t i = 0; i < O0_GDT_ENTRIES; ++i) {
        gdt_images[cpu_index][i] = view.entries[i];
    }

    /* Initialize this CPU's TSS and IST stacks */
    tss_init_for_cpu(cpu_index);

    gdt_load_segments_for_cpu(cpu_index);
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)O0_SEL_TSS));

    gdt_install_syscall_msrs();
}

void o0_gdt_load_for_ap(void) {
    /* Legacy path: kept for compatibility until all callers are updated.
     * This loads the BSP's GDT image and does not execute ltr. */
    gdt_load_segments_for_cpu(0);
    gdt_install_syscall_msrs();
}