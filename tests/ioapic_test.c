/* Ozero kernel - I/O APIC encoding and GSI mapping host test. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/ioapic.h"
#include "ozero/status.h"
#include "ozero/types.h"

static uint8_t ioapic_mem[4096];
static uint32_t ioapic_reg_sel;
static uint32_t ioapic_reg_win;

uint32_t o0_ioapic_read(uint32_t reg) {
    (void)reg;
    return ioapic_reg_win;
}

void o0_ioapic_write(uint32_t reg, uint32_t value) {
    if (reg == 0) {
        ioapic_reg_sel = value;
    } else if (reg == 0x10) {
        ioapic_reg_win = value;
    }
}

int main(void) {
    int failures = 0;
    printf("Ozero I/O APIC tests\n\n");

    /* Test 1: Redirection entry encoding */
    struct o0_ioapic_redir entry = {0};
    entry.vector = 0x21;
    entry.delivery_mode = O0_IOAPIC_DELIVERY_FIXED;
    entry.dest_mode = O0_IOAPIC_DEST_PHYSICAL;
    entry.delivery_status = 0;
    entry.polarity = O0_IOAPIC_POLARITY_HIGH;
    entry.trigger_mode = O0_IOAPIC_TRIGGER_EDGE;
    entry.mask = 0;
    entry.destination = 0; /* CPU 0 */

    uint64_t encoded = o0_ioapic_encode_redir(&entry);
    uint32_t low = (uint32_t)encoded;
    uint32_t high = (uint32_t)(encoded >> 32);

    if ((low & 0xFF) != 0x21) {
        printf("FAIL: vector mismatch: got 0x%x, expected 0x21\n", low & 0xFF);
        ++failures;
    }
    if (((low >> 8) & 0x7) != O0_IOAPIC_DELIVERY_FIXED) {
        printf("FAIL: delivery mode mismatch\n");
        ++failures;
    }
    if (((low >> 11) & 0x1) != O0_IOAPIC_DEST_PHYSICAL) {
        printf("FAIL: dest mode mismatch\n");
        ++failures;
    }
    if (((low >> 13) & 0x1) != O0_IOAPIC_POLARITY_HIGH) {
        printf("FAIL: polarity mismatch\n");
        ++failures;
    }
    if (((low >> 15) & 0x1) != O0_IOAPIC_TRIGGER_EDGE) {
        printf("FAIL: trigger mode mismatch\n");
        ++failures;
    }
    if (((low >> 16) & 0x1) != 0) {
        printf("FAIL: mask bit mismatch\n");
        ++failures;
    }
    if (high != 0) {
        printf("FAIL: high dword should be 0 for phys destination 0\n");
        ++failures;
    }

    /* Test 2: GSI to redir index mapping */
    if (o0_ioapic_gsi_to_redir(0) != 0) {
        printf("FAIL: GSI 0 -> redir 0\n");
        ++failures;
    }
    if (o0_ioapic_gsi_to_redir(23) != 23) {
        printf("FAIL: GSI 23 -> redir 23\n");
        ++failures;
    }

    /* Test 3: Round-trip decode */
    struct o0_ioapic_redir decoded;
    o0_ioapic_decode_redir(encoded, &decoded);
    if (decoded.vector != entry.vector ||
        decoded.delivery_mode != entry.delivery_mode ||
        decoded.dest_mode != entry.dest_mode ||
        decoded.polarity != entry.polarity ||
        decoded.trigger_mode != entry.trigger_mode ||
        decoded.mask != entry.mask ||
        decoded.destination != entry.destination) {
        printf("FAIL: decode round-trip mismatch\n");
        ++failures;
    }

    if (failures == 0) {
        printf("I/O APIC: PASS\n");
        return 0;
    }
    printf("I/O APIC: FAIL (%d)\n", failures);
    return 1;
}