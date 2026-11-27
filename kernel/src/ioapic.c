/* Ozero kernel - I/O APIC redirection driver.
 *
 * Each I/O APIC register block is mapped once in the kernel half as uncached
 * MMIO. Redirection entries use physical destination mode and target the BSP;
 * per-device affinity is a later scheduler policy decision.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/acpi.h"
#include "ozero/ioapic.h"
#include "ozero/lapic.h"
#include "ozero/mm.h"
#include "ozero/status.h"

#define O0_IOAPIC_REG_SELECT 0x00u
#define O0_IOAPIC_REG_WINDOW 0x10u
#define O0_IOAPIC_REG_VER 0x01u
#define O0_IOAPIC_REG_REDTBL 0x10u
#define O0_IOAPIC_WINDOW_BASE 0xffffc00010000000ull
#define O0_IOAPIC_WINDOW_SIZE 0x1000u

struct o0_ioapic_instance {
    volatile uint32_t *regs;
    uint32_t gsi_base;
    uint32_t pin_count;
};

static struct o0_ioapic_instance ioapics[O0_ACPI_MAX_IOAPICS];
static uint32_t ioapic_count;
static bool ioapic_ready;

extern o0_status o0_pic_mask(uint32_t line);

static uint32_t ioapic_read(struct o0_ioapic_instance *io, uint32_t reg) {
    io->regs[O0_IOAPIC_REG_SELECT / 4u] = reg;
    return io->regs[O0_IOAPIC_REG_WINDOW / 4u];
}

static void ioapic_write(struct o0_ioapic_instance *io, uint32_t reg, uint32_t value) {
    io->regs[O0_IOAPIC_REG_SELECT / 4u] = reg;
    io->regs[O0_IOAPIC_REG_WINDOW / 4u] = value;
}

static struct o0_ioapic_instance *ioapic_for_gsi(uint32_t gsi) {
    for (uint32_t i = 0; i < ioapic_count; ++i) {
        struct o0_ioapic_instance *io = &ioapics[i];
        if (gsi >= io->gsi_base && gsi < io->gsi_base + io->pin_count) {
            return io;
        }
    }
    return NULL;
}

bool o0_ioapic_available(void) {
    return ioapic_ready;
}

uint32_t o0_ioapic_pin_count(void) {
    uint32_t total = 0;
    for (uint32_t i = 0; i < ioapic_count; ++i) {
        total += ioapics[i].pin_count;
    }
    return total;
}

o0_status o0_ioapic_init(void) {
    if (ioapic_ready) {
        return O0_EBUSY;
    }
    const struct o0_acpi_info *info = o0_acpi_info();
    if (info == NULL || info->ioapic_count == 0) {
        return O0_ENOENT;
    }
    if (info->ioapic_count > O0_ACPI_MAX_IOAPICS) {
        return O0_ENOMEM;
    }

    ioapic_count = 0;
    for (uint32_t i = 0; i < info->ioapic_count; ++i) {
        const struct o0_acpi_ioapic *src = &info->ioapics[i];
        if (src->address == 0) {
            continue;
        }
        o0_vaddr_t window = O0_IOAPIC_WINDOW_BASE +
                            (uint64_t)ioapic_count * O0_IOAPIC_WINDOW_SIZE;
        o0_status st = o0_as_map(o0_as_kernel(), window, src->address,
                                 O0_IOAPIC_WINDOW_SIZE,
                                 O0_MAP_READ | O0_MAP_WRITE | O0_MAP_UC | O0_MAP_NX);
        if (st != O0_OK) {
            return st;
        }
        struct o0_ioapic_instance *io = &ioapics[ioapic_count];
        io->regs = (volatile uint32_t *)(uintptr_t)window;
        io->gsi_base = src->gsi_base;
        uint32_t ver = ioapic_read(io, O0_IOAPIC_REG_VER);
        io->pin_count = ((ver >> 16) & 0xffu) + 1u;
        if (io->pin_count > O0_IOAPIC_MAX_PINS) {
            io->pin_count = O0_IOAPIC_MAX_PINS;
        }
        for (uint32_t pin = 0; pin < io->pin_count; ++pin) {
            ioapic_write(io, O0_IOAPIC_REG_REDTBL + pin * 2u,
                         O0_IOAPIC_RTE_MASKED);
            ioapic_write(io, O0_IOAPIC_REG_REDTBL + pin * 2u + 1u, 0);
        }
        ++ioapic_count;
    }

    if (ioapic_count == 0) {
        return O0_ENOENT;
    }

    /* The 8259 is no longer the delivery path. Mask every legacy line so an
     * interrupt cannot arrive twice through ExtINT and the I/O APIC. */
    for (uint32_t line = 0; line < 16u; ++line) {
        (void)o0_pic_mask(line);
    }
    ioapic_ready = true;
    return O0_OK;
}

uint32_t o0_ioapic_gsi(uint32_t isa_irq) {
    return o0_ioapic_gsi_for_isa(o0_acpi_info(), isa_irq, NULL);
}

o0_status o0_ioapic_route_isa(uint32_t isa_irq, uint32_t vector, uint32_t flags,
                              bool masked) {
    if (!ioapic_ready || isa_irq >= 16u || vector > 0xffu) {
        return O0_EINVAL;
    }
    uint32_t resolved_flags = 0;
    uint32_t gsi = o0_ioapic_gsi_for_isa(o0_acpi_info(), isa_irq, &resolved_flags);
    if ((flags & (O0_IRQ_TRIGGER_LEVEL | O0_IRQ_POLARITY_LOW)) != 0) {
        resolved_flags = flags;
    }
    struct o0_ioapic_instance *io = ioapic_for_gsi(gsi);
    if (io == NULL) {
        return O0_ENOSYS;
    }
    uint32_t pin = gsi - io->gsi_base;
    uint32_t dest = o0_lapic_id();
    if (dest == UINT32_MAX) {
        dest = 0;
    }
    uint64_t rte = o0_ioapic_encode_rte(vector, dest, resolved_flags, masked);
    ioapic_write(io, O0_IOAPIC_REG_REDTBL + pin * 2u, (uint32_t)rte);
    ioapic_write(io, O0_IOAPIC_REG_REDTBL + pin * 2u + 1u, (uint32_t)(rte >> 32));
    return O0_OK;
}

o0_status o0_ioapic_set_mask(uint32_t isa_irq, bool masked) {
    if (!ioapic_ready || isa_irq >= 16u) {
        return O0_EINVAL;
    }
    uint32_t gsi = o0_ioapic_gsi(isa_irq);
    struct o0_ioapic_instance *io = ioapic_for_gsi(gsi);
    if (io == NULL) {
        return O0_ENOSYS;
    }
    uint32_t pin = gsi - io->gsi_base;
    uint32_t low = ioapic_read(io, O0_IOAPIC_REG_REDTBL + pin * 2u);
    if (masked) {
        low |= O0_IOAPIC_RTE_MASKED;
    } else {
        low &= ~O0_IOAPIC_RTE_MASKED;
    }
    ioapic_write(io, O0_IOAPIC_REG_REDTBL + pin * 2u, low);
    return O0_OK;
}
