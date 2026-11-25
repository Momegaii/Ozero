/* Ozero kernel - ACPI MADT parsing host test. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/acpi.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define O0_PAGE_SIZE 4096

void *o0_phys_to_virt(uint64_t phys) {
    (void)phys;
    return NULL;
}

void o0_serial_puts(const char *text) {
    (void)text;
}

void o0_serial_hex(uint64_t value) {
    (void)value;
}

void o0_serial_dec(uint64_t value) {
    (void)value;
}

static uint8_t madt_table[4096];

int main(void) {
    int failures = 0;
    printf("Ozero ACPI MADT tests\n\n");

    /* Test 1: Minimal valid MADT with one LAPIC and one I/O APIC */
    memset(madt_table, 0, sizeof(madt_table));
    struct o0_acpi_madt *madt = (struct o0_acpi_madt *)madt_table;
    madt->header.signature = O0_ACPI_SIG('A','P','I','C');
    madt->header.length = sizeof(struct o0_acpi_madt) - sizeof(madt->header);
    madt->header.revision = 4;
    madt->local_apic_address = 0xFEE00000;
    madt->flags = 1; /* PC-AT compatible */

    /* LAPIC entry for CPU 0 */
    struct o0_acpi_madt_lapic *lapic = (struct o0_acpi_madt_lapic *)madt->entries;
    lapic->header.type = O0_ACPI_MADT_LAPIC;
    lapic->header.length = sizeof(struct o0_acpi_madt_lapic);
    lapic->processor_id = 0;
    lapic->id = 0;
    lapic->flags = 1; /* Enabled */

    /* I/O APIC entry */
    uint8_t *ptr = (uint8_t *)lapic + sizeof(struct o0_acpi_madt_lapic);
    struct o0_acpi_madt_ioapic *ioapic = (struct o0_acpi_madt_ioapic *)ptr;
    ioapic->header.type = O0_ACPI_MADT_IOAPIC;
    ioapic->header.length = sizeof(struct o0_acpi_madt_ioapic);
    ioapic->id = 0;
    ioapic->reserved = 0;
    ioapic->address = 0xFEC00000;
    ioapic->global_system_interrupt_base = 0;

    /* ISO entry for IRQ0 -> GSI 2 */
    ptr += sizeof(struct o0_acpi_madt_ioapic);
    struct o0_acpi_madt_iso *iso = (struct o0_acpi_madt_iso *)ptr;
    iso->header.type = O0_ACPI_MADT_ISO;
    iso->header.length = sizeof(struct o0_acpi_madt_iso);
    iso->bus = 0;
    iso->source = 0; /* IRQ0 */
    iso->global_system_interrupt = 2;
    iso->flags = 0; /* Active high, edge triggered */

    madt->header.length = (uint8_t *)ptr + sizeof(struct o0_acpi_madt_iso) - (uint8_t *)madt - sizeof(madt->header);

    struct o0_acpi_madt_parsed parsed;
    o0_status status = o0_acpi_parse_madt(madt, &parsed);
    if (status != O0_OK) {
        printf("FAIL: MADT parse returned %d\n", status);
        ++failures;
    } else {
        if (parsed.lapic_count != 1) {
            printf("FAIL: expected 1 LAPIC, got %u\n", parsed.lapic_count);
            ++failures;
        }
        if (parsed.ioapic_count != 1) {
            printf("FAIL: expected 1 I/O APIC, got %u\n", parsed.ioapic_count);
            ++failures;
        }
        if (parsed.iso_count != 1) {
            printf("FAIL: expected 1 ISO, got %u\n", parsed.iso_count);
            ++failures;
        }
        if (parsed.ioapics[0].gsi_base != 0) {
            printf("FAIL: I/O APIC GSI base mismatch\n");
            ++failures;
        }
    }

    /* Test 2: Invalid signature */
    madt->header.signature = O0_ACPI_SIG('B','A','D','!');
    status = o0_acpi_parse_madt(madt, &parsed);
    if (status != O0_EINVAL) {
        printf("FAIL: invalid signature should return EINVAL, got %d\n", status);
        ++failures;
    }

    if (failures == 0) {
        printf("ACPI MADT: PASS\n");
        return 0;
    }
    printf("ACPI MADT: FAIL (%d)\n", failures);
    return 1;
}