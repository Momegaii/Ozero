/* Ozero kernel - I/O APIC redirection encoding test (host build). */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/acpi.h"
#include "ozero/ioapic.h"
#include "ozero/irq.h"

static int failures;

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s\n", msg); \
            ++failures; \
        } \
    } while (0)

int main(void) {
    printf("Ozero I/O APIC tests\n\n");

    uint64_t rte = o0_ioapic_encode_rte(0x21u, 0x0au, O0_IRQ_TRIGGER_EDGE,
                                        false);
    ASSERT((rte & 0xffu) == 0x21u, "vector not encoded");
    ASSERT(((rte >> 56) & 0xffu) == 0x0au, "destination not encoded");
    ASSERT((rte & O0_IOAPIC_RTE_MASKED) == 0, "unmasked entry has mask bit");
    ASSERT((rte & O0_IOAPIC_RTE_LEVEL) == 0, "edge entry has level bit");

    rte = o0_ioapic_encode_rte(0x22u, 0x0bu,
                               O0_IRQ_TRIGGER_LEVEL | O0_IRQ_POLARITY_LOW,
                               true);
    ASSERT((rte & O0_IOAPIC_RTE_LEVEL) != 0, "level trigger not encoded");
    ASSERT((rte & O0_IOAPIC_RTE_LOW_ACTIVE) != 0, "low polarity not encoded");
    ASSERT((rte & O0_IOAPIC_RTE_MASKED) != 0, "mask bit not encoded");

    struct o0_acpi_info info;
    memset(&info, 0, sizeof(info));
    info.override_count = 1;
    info.overrides[0].bus = 0;
    info.overrides[0].source = 2;
    info.overrides[0].gsi = 9;
    info.overrides[0].flags = 0x000f; /* level, low active */
    uint32_t flags = 0;
    uint32_t gsi = o0_ioapic_gsi_for_isa(&info, 2, &flags);
    ASSERT(gsi == 9u, "override GSI not used");
    ASSERT((flags & O0_IRQ_TRIGGER_LEVEL) != 0, "override level flag lost");
    ASSERT((flags & O0_IRQ_POLARITY_LOW) != 0, "override polarity flag lost");

    gsi = o0_ioapic_gsi_for_isa(&info, 3, &flags);
    ASSERT(gsi == 3u, "identity GSI mapping broken");
    ASSERT(flags == (O0_IRQ_TRIGGER_EDGE | O0_IRQ_POLARITY_HIGH),
           "default ISA flags wrong");

    if (failures == 0) {
        printf("ioapic: PASS\n");
        return 0;
    }
    printf("ioapic: %d failure(s)\n", failures);
    return 1;
}
