/* Ozero kernel - x86-64 interrupt descriptor table.
 *
 * Exception gates (vectors 0-31) are installed at boot; the 16 legacy IRQ gates
 * (vectors 32-47) are installed separately, after the PIC has been remapped.
 */

#include <stddef.h>
#include <stdint.h>

#define IDT_ENTRIES 256
#define IDT_EXCEPTION_ENTRIES 32
#define IDT_INTERRUPT_GATE 0x8e

struct __attribute__((packed)) idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

struct __attribute__((packed)) idtr {
    uint16_t limit;
    uint64_t base;
};

extern void *x86_64_isr_stub_table[IDT_EXCEPTION_ENTRIES];

#define IDT_IRQ_ENTRIES 16u
#define IDT_IRQ_BASE 32u
extern void *x86_64_irq_stub_table[IDT_IRQ_ENTRIES];

#define IDT_IPI_VECTOR 0x40u
extern void x86_64_ipi_stub(void);
#define IDT_APIC_TIMER_VECTOR 0x41u
extern void x86_64_apic_timer_stub(void);

static struct idt_gate idt[IDT_ENTRIES];

static uint16_t read_code_selector(void) {
    uint16_t selector;
    __asm__ volatile ("mov %%cs, %0" : "=r"(selector));
    return selector;
}

static void idt_set_gate(size_t vector, uintptr_t handler, uint16_t selector) {
    idt[vector].offset_low = (uint16_t)(handler & 0xffffU);
    idt[vector].selector = selector;
    idt[vector].ist = 0;
    idt[vector].attributes = IDT_INTERRUPT_GATE;
    idt[vector].offset_middle = (uint16_t)((handler >> 16) & 0xffffU);
    idt[vector].offset_high = (uint32_t)(handler >> 32);
    idt[vector].reserved = 0;
}

void x86_64_idt_init(void) {
    const uint16_t selector = read_code_selector();
    for (size_t i = 0; i < IDT_EXCEPTION_ENTRIES; ++i) {
        idt_set_gate(i, (uintptr_t)x86_64_isr_stub_table[i], selector);
    }
    const struct idtr descriptor = {
        .limit = (uint16_t)(sizeof(idt) - 1U),
        .base = (uintptr_t)idt
    };
    __asm__ volatile ("lidt %0" : : "m"(descriptor) : "memory");
}

/* Installs the 16 legacy IRQ gates (vectors 32..47). Stays separate from
 * x86_64_idt_init so exception handling works before the PIC is remapped. */
void x86_64_idt_install_irq_gates(void) {
    const uint16_t selector = read_code_selector();
    for (size_t i = 0; i < IDT_IRQ_ENTRIES; ++i) {
        idt_set_gate(IDT_IRQ_BASE + i, (uintptr_t)x86_64_irq_stub_table[i], selector);
    }
}

void x86_64_idt_install_ipi_gate(void) {
    const uint16_t selector = read_code_selector();
    idt_set_gate(IDT_IPI_VECTOR, (uintptr_t)x86_64_ipi_stub, selector);
}

void x86_64_idt_install_apic_timer_gate(void) {
    const uint16_t selector = read_code_selector();
    idt_set_gate(IDT_APIC_TIMER_VECTOR, (uintptr_t)x86_64_apic_timer_stub,
                 selector);
}

/* The double fault must run on a stack that may already be corrupt: IST1 points
 * at the dedicated stack installed with the TSS. */
void x86_64_idt_install_double_fault_ist(void) {
    idt[8].ist = 1;
}
