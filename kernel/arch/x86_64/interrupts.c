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

#define IDT_DEVICE_BASE 0x30u
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

#define IDT_DEVICE_VECTORS 192u
extern void *x86_64_device_stub_table[IDT_DEVICE_VECTORS];

void x86_64_idt_install_device_gates(void) {
    const uint16_t selector = read_code_selector();
    for (size_t i = 0; i < IDT_DEVICE_VECTORS; ++i) {
        const size_t vector = IDT_DEVICE_BASE + i;
        /* The device range straddles the IPI and LAPIC timer vectors, which have
         * their own stubs. Installing here would replace them and the timer and
         * TLB shootdown would stop running. */
        if (vector == IDT_IPI_VECTOR || vector == IDT_APIC_TIMER_VECTOR) {
            continue;
        }
        idt_set_gate(vector, (uintptr_t)x86_64_device_stub_table[i], selector);
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

/* Reports gates the CPU would fault on: a present gate whose target selector is
 * not our code segment, the live IDTR, and the TR/TSS state the delivery path
 * depends on. */
void x86_64_idt_audit(void) {
    extern void o0_serial_puts(const char *);
    extern void o0_serial_dec(uint64_t);
    extern void o0_serial_hex(uint64_t);
    extern uint64_t o0_gdt_tss_limit(void);
    extern uint32_t o0_lapic_id(void);

    const uint16_t cs = read_code_selector();
    for (size_t i = 0; i < IDT_ENTRIES; ++i) {
        uint64_t offset = (uint64_t)idt[i].offset_low |
                          ((uint64_t)idt[i].offset_middle << 16) |
                          ((uint64_t)idt[i].offset_high << 32);
        if (offset == 0u && idt[i].selector == 0u) {
            continue;
        }
        if (idt[i].selector == cs && (idt[i].attributes & 0x80u) != 0u) {
            continue;
        }
        o0_serial_puts("OZERO: idt gate vector=");
        o0_serial_dec(i);
        o0_serial_puts(" sel=");
        o0_serial_hex(idt[i].selector);
        o0_serial_puts(" attr=");
        o0_serial_hex(idt[i].attributes);
        o0_serial_puts(" ist=");
        o0_serial_dec(idt[i].ist);
        o0_serial_puts("\n");
    }

    struct idtr live;
    __asm__ volatile ("sidt %0" : "=m"(live));
    uint16_t tr = 0;
    __asm__ volatile ("str %0" : "=r"(tr));
    o0_serial_puts("OZERO: idt live base=");
    o0_serial_hex(live.base);
    o0_serial_puts(" limit=");
    o0_serial_hex(live.limit);
    o0_serial_puts(" array=");
    o0_serial_hex((uint64_t)(uintptr_t)idt);
    o0_serial_puts(" tr=");
    o0_serial_hex(tr);
    o0_serial_puts(" tss_limit=");
    o0_serial_hex(o0_gdt_tss_limit());
    o0_serial_puts(" lapic=");
    o0_serial_dec(o0_lapic_id());
    o0_serial_puts("\n");
}

/* The double fault must run on a stack that may already be corrupt: IST1 points
 * at the dedicated stack installed with the TSS. */
void x86_64_idt_install_double_fault_ist(void) {
    idt[8].ist = 1;
}

/* NMI uses IST2 to avoid stack corruption in the interrupted context. */
void x86_64_idt_install_nmi_ist(void) {
    idt[2].ist = 2;
}

/* Machine check uses IST3. Handler may only record and halt. */
void x86_64_idt_install_mce_ist(void) {
    idt[18].ist = 3;
}
