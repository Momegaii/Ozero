/* Ozero kernel - SMP bring-up, per-CPU online accounting and TLB shootdown.
 *
 * Limine's MP protocol parks APs and releases one when goto_address is
 * published. Ozero publishes a small assembly entry that switches to a
 * kernel-owned stack; the C continuation loads the kernel page tables, GDT,
 * IDT and local APIC before marking the slot online.
 *
 * APs run their own scheduler loop after setup. They service TLB-shootdown
 * and reschedule IPIs and pick kernel threads from their per-CPU runqueue;
 * user threads remain pinned to the BSP until per-CPU TSS/IST exists.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <limine.h>

#include "ozero/cpu.h"
#include "ozero/cpu_local.h"
#include "ozero/apic_timer.h"
#include "ozero/gdt.h"
#include "ozero/lapic.h"
#include "ozero/mm.h"
#include "ozero/smp.h"
#include "ozero/status.h"

#define O0_AP_STACK_BYTES 16384u

uint8_t o0_ap_stacks[O0_MAX_CPUS][O0_AP_STACK_BYTES] __attribute__((aligned(16)));

static volatile uint32_t online_aps;
static volatile uint32_t ipi_seen;

extern void x86_64_idt_init(void);
extern void x86_64_idt_install_irq_gates(void);
extern void x86_64_idt_install_ipi_gate(void);
extern void x86_64_idt_install_apic_timer_gate(void);
extern void x86_64_idt_install_device_gates(void);
extern void o0_sched_ap_start(uint32_t cpu, uint64_t stack_top);
extern void o0_sched_resched_ipi(void);
extern void o0_serial_puts(const char *text);
extern void o0_serial_dec(uint64_t value);

#define O0_TLB_ACK_SPIN 10000000ull

static inline void invlpg(uint64_t vaddr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(vaddr) : "memory");
}

static inline void flush_all_tlb(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

static struct o0_cpu_local *local_for(uint32_t index) {
    if (index >= O0_MAX_CPUS) {
        return NULL;
    }
    return &o0_cpu_local[index];
}

void x86_apic_ipi_dispatch(void *frame) {
    (void)frame;
    struct o0_cpu_local *local = o0_cpu_local_current();
    if (local != NULL) {
        if (__atomic_load_n(&local->tlb_pending, __ATOMIC_ACQUIRE) != 0) {
            if (__atomic_load_n(&local->tlb_all, __ATOMIC_ACQUIRE) != 0) {
                flush_all_tlb();
            } else {
                uint64_t base = __atomic_load_n(&local->tlb_addr, __ATOMIC_ACQUIRE);
                uint32_t pages = __atomic_load_n(&local->tlb_len, __ATOMIC_ACQUIRE);
                if (pages == 0u) {
                    pages = 1u;
                }
                for (uint32_t i = 0; i < pages; ++i) {
                    invlpg(base + (uint64_t)i * 4096u);
                }
            }
            __atomic_store_n(&local->tlb_pending, 0u, __ATOMIC_RELEASE);
            __atomic_add_fetch(&local->tlb_ack, 1u, __ATOMIC_RELEASE);
        }
    }
    __atomic_add_fetch(&ipi_seen, 1u, __ATOMIC_RELEASE);
    o0_lapic_eoi();
    if (local != NULL &&
        __atomic_load_n(&local->resched_pending, __ATOMIC_ACQUIRE) != 0) {
        o0_sched_resched_ipi();
    }
}

uint32_t o0_smp_ipi_seen(void) {
    return __atomic_load_n(&ipi_seen, __ATOMIC_ACQUIRE);
}

void o0_ap_entry_c(struct limine_mp_info *info) {
    uint32_t index = info != NULL ? (uint32_t)info->extra_argument : 0u;
    if (index == 0 || index >= O0_MAX_CPUS) {
        for (;;) {
            __asm__ volatile ("cli; hlt" ::: "memory");
        }
    }

    (void)o0_paging_ap_load();
    (void)o0_cpu_enable_smep_smap();

    /* Per-CPU GDT/TSS/IST: must run ltr before enabling interrupts or scheduling. */
    o0_gdt_init_ap(index);

    struct o0_cpu_local *local = o0_cpu_local_install(index);
    if (local != NULL) {
        local->apic_id = info->lapic_id;
        local->kernel_rsp0 =
            (uint64_t)(uintptr_t)&o0_ap_stacks[index][O0_AP_STACK_BYTES];
    }

    x86_64_idt_init();
    x86_64_idt_install_irq_gates();
    x86_64_idt_install_ipi_gate();
    x86_64_idt_install_apic_timer_gate();
    x86_64_idt_install_device_gates();
    (void)o0_lapic_ap_init();
    (void)o0_apic_timer_start_ap();

    /* Mark TSS ready after ltr and IDT/LAPIC are installed. */
    local = o0_cpu_local_current();
    if (local != NULL) {
        local->tss_ready = 1;
    }

    o0_cpu_local_mark_online(index, info->lapic_id);
    __atomic_add_fetch(&online_aps, 1u, __ATOMIC_RELEASE);

    o0_sched_ap_start(index,
                      (uint64_t)(uintptr_t)&o0_ap_stacks[index][O0_AP_STACK_BYTES]);
}

uint32_t o0_smp_start(struct limine_mp_response *response) {
    if (response == NULL || response->cpus == NULL) {
        return o0_cpu_local_online_count();
    }

    uint32_t total = response->cpu_count > O0_MAX_CPUS
                         ? O0_MAX_CPUS
                         : (uint32_t)response->cpu_count;
    o0_cpu_set_count(total);
    online_aps = 0;

    uint32_t next_index = 1;
    for (uint32_t i = 0; i < response->cpu_count; ++i) {
        struct limine_mp_info *info = response->cpus[i];
        if (info == NULL) {
            continue;
        }
        if (info->lapic_id == response->bsp_lapic_id) {
            o0_cpu_set_info(0, info->lapic_id, info->processor_id, O0_CPU_F_BSP);
            struct o0_cpu_local *bsp = local_for(0);
            if (bsp != NULL) {
                bsp->apic_id = info->lapic_id;
            }
            continue;
        }
        if (next_index >= total) {
            break;
        }
        o0_cpu_set_info(next_index, info->lapic_id, info->processor_id, 0);
        info->extra_argument = next_index;
        __atomic_store_n(&info->goto_address, o0_ap_entry, __ATOMIC_RELEASE);
        ++next_index;
    }

    uint32_t expected = total > 1u ? total - 1u : 0u;
    for (uint64_t spin = 0; spin < 200000000ull; ++spin) {
        if (__atomic_load_n(&online_aps, __ATOMIC_ACQUIRE) >= expected &&
            o0_cpu_local_online_count() >= total) {
            break;
        }
        __asm__ volatile ("pause");
    }
    return o0_cpu_local_online_count();
}

uint32_t o0_smp_online_count(void) {
    return o0_cpu_local_online_count();
}

static O0_NORETURN void tlb_timeout_panic(uint32_t cpu, uint32_t ack) {
    __asm__ volatile ("cli" : : : "memory");
    o0_serial_puts("OZERO: PANIC tlb shootdown timeout cpu=");
    o0_serial_dec(cpu);
    o0_serial_puts(" ack=");
    o0_serial_dec(ack);
    o0_serial_puts(" (CPU did not acknowledge; refusing to continue with stale TLB)\n");
    for (;;) {
        __asm__ volatile ("hlt" : : : "memory");
    }
}

static o0_status tlb_shootdown_common(uint64_t vaddr, uint32_t pages, bool all) {
    uint32_t me = o0_cpu_index();
    uint32_t online = o0_cpu_local_online_count();
    if (online <= 1u || !o0_lapic_available()) {
        if (all) {
            flush_all_tlb();
        } else {
            for (uint32_t i = 0; i < pages; ++i) {
                invlpg(vaddr + (uint64_t)i * 4096u);
            }
        }
        return O0_OK;
    }

    bool sent = false;
    for (uint32_t i = 0; i < O0_MAX_CPUS; ++i) {
        if (i == me) {
            continue;
        }
        struct o0_cpu_local *local = local_for(i);
        if (local == NULL ||
            __atomic_load_n(&local->online, __ATOMIC_ACQUIRE) == 0) {
            continue;
        }
        __atomic_store_n(&local->tlb_addr, vaddr, __ATOMIC_RELEASE);
        __atomic_store_n(&local->tlb_len, pages, __ATOMIC_RELEASE);
        __atomic_store_n(&local->tlb_all, all ? 1u : 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&local->tlb_ack, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&local->tlb_pending, 1u, __ATOMIC_RELEASE);
        o0_status st = o0_lapic_send_ipi(local->apic_id, O0_LAPIC_IPI_VECTOR);
        if (st != O0_OK) {
            __atomic_store_n(&local->tlb_pending, 0u, __ATOMIC_RELEASE);
            return st;
        }
        sent = true;
    }

    if (sent) {
        for (uint32_t i = 0; i < O0_MAX_CPUS; ++i) {
            if (i == me) {
                continue;
            }
            struct o0_cpu_local *local = local_for(i);
            if (local == NULL ||
                __atomic_load_n(&local->online, __ATOMIC_ACQUIRE) == 0) {
                continue;
            }
            uint64_t spin = 0;
            while (__atomic_load_n(&local->tlb_ack, __ATOMIC_ACQUIRE) == 0) {
                if (++spin > O0_TLB_ACK_SPIN) {
                    tlb_timeout_panic(i, 0);
                }
                __asm__ volatile ("pause");
            }
        }
    }
    if (all) {
        flush_all_tlb();
    } else {
        for (uint32_t i = 0; i < pages; ++i) {
            invlpg(vaddr + (uint64_t)i * 4096u);
        }
    }
    return O0_OK;
}

o0_status o0_tlb_shootdown(uint64_t vaddr) {
    return tlb_shootdown_common(vaddr, 1u, false);
}

o0_status o0_tlb_shootdown_range(uint64_t vaddr, size_t len) {
    if (len == 0) {
        return O0_OK;
    }
    uint64_t pages = ((uint64_t)len + 4095u) / 4096u;
    if (pages > UINT32_MAX) {
        return O0_EOVERFLOW;
    }
    return tlb_shootdown_common(vaddr, (uint32_t)pages, false);
}

o0_status o0_tlb_shootdown_all(void) {
    return tlb_shootdown_common(0, 0, true);
}
