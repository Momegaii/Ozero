/* Ozero kernel - PCI BAR, capability and MSI/MSI-X host tests.
 *
 * The config-space images below are constructed from the PCI Express base
 * specification's capability layouts (MSI 0x05, MSI-X 0x11) and the standard
 * header layout, not captured from a device. They are recorded as literals so
 * the parsers are checked against the same bytes the hardware would present:
 * header type 0x80 proves the multi-function bit is not treated as a layout,
 * and the MSI-X table/PBA BIRs and offsets exercise the capability walk.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ozero/pci.h"

/* The decode/parse helpers live in the driver TU. Only pure functions are called
 * here, so the config-space I/O in the rest of the file never executes. */
#include "../kernel/src/pci.c"

static void dump_set32(uint8_t *cfg, size_t offset, uint32_t value) {
    cfg[offset + 0u] = (uint8_t)value;
    cfg[offset + 1u] = (uint8_t)(value >> 8);
    cfg[offset + 2u] = (uint8_t)(value >> 16);
    cfg[offset + 3u] = (uint8_t)(value >> 24);
}

/* Header type 0x80 (multi-function, layout 0), status bit 4 set, an MSI
 * capability (64-bit, per-vector mask) at 0x50 and MSI-X at 0x60 with a
 * four-entry table in BAR 3 at 0x2000 and the PBA at 0x3000. */
static void build_dump(uint8_t *cfg) {
    memset(cfg, 0, 256);
    dump_set32(cfg, 0x00, 0x12341AF4u);      /* vendor 0x1AF4, device 0x1234 */
    cfg[0x06] = 0x10;                        /* status: capabilities list */
    cfg[0x0C] = 0x00;                        /* header layout 0 */
    cfg[0x0E] = 0x80;                        /* bit 7: multi-function */
    cfg[0x34] = 0x50;                        /* capability pointer */
    cfg[0x50] = 0x05;                        /* MSI */
    cfg[0x51] = 0x60;
    cfg[0x52] = 0x80;                        /* control low: 64-bit */
    cfg[0x53] = 0x01;                        /* control high: per-vector mask */
    dump_set32(cfg, 0x54, 0xFEE02000u);      /* message address */
    cfg[0x60] = 0x11;                        /* MSI-X */
    cfg[0x61] = 0x00;
    cfg[0x62] = 0x03;                        /* table size 4 - 1 */
    cfg[0x63] = 0x00;
    dump_set32(cfg, 0x64, 0x00002003u);      /* table BIR 3, offset 0x2000 */
    dump_set32(cfg, 0x68, 0x00003003u);      /* PBA BIR 3, offset 0x3000 */
}

int main(void) {
    struct o0_pci_bar bar;

    /* ---- BAR decode ---- */
    o0_pci_decode_bar(0x10000000u, 0, 0, 0, &bar);
    assert(bar.is_memory && !bar.is_64bit && bar.base == 0x10000000u);
    o0_pci_decode_bar(0x10000004u, 0x000000A0u, 0, 1, &bar);
    assert(bar.is_memory && bar.is_64bit && bar.base == 0x000000A010000000u);
    o0_pci_decode_bar(0x00001001u, 0, 0, 2, &bar);
    assert(!bar.is_memory);

    /* ---- BAR size from mask ---- */
    assert(o0_pci_bar_size_from_mask(true, false, 0xFFFFF000u, 0) == 0x1000u);
    assert(o0_pci_bar_size_from_mask(true, false, 0xF0000000u, 0) == 0x10000000u);
    assert(o0_pci_bar_size_from_mask(true, true, 0xFFFFF000u, 0xFFFFFFFFu) == 0x1000u);
    assert(o0_pci_bar_size_from_mask(true, true, 0x00000000u, 0xFFFFFFF0u) == 0x1000000000ull);
    assert(o0_pci_bar_size_from_mask(false, false, 0xFFFFFFF9u, 0) == 0x8u);
    assert(o0_pci_bar_size_from_mask(true, false, 0u, 0) == 0u);

    /* ---- Capability list from a recorded dump ---- */
    uint8_t cfg[256];
    struct o0_pci_resources res;
    memset(&res, 0, sizeof(res));
    res.bar_count = 6;
    build_dump(cfg);
    assert(o0_pci_parse_capabilities(cfg, sizeof(cfg), &res) == 0);
    assert(res.msi_supported && res.msi_cap_offset == 0x50);
    assert(res.msi_64bit && res.msi_per_vector_mask);
    assert(res.msix_supported && res.msix_cap_offset == 0x60);
    assert(res.msix_table_bir == 3 && res.msix_table_offset == 0x2000);
    assert(res.msix_pba_bir == 3 && res.msix_pba_offset == 0x3000);
    assert(res.msix_table_size == 4);

    /* Bare-metal layout: BIR that no BAR owns must not claim MSI-X support. */
    memset(&res, 0, sizeof(res));
    res.bar_count = 2;
    assert(o0_pci_parse_capabilities(cfg, sizeof(cfg), &res) == 0);
    assert(!res.msix_supported && res.msi_supported);

    /* No capability-list bit: the pointer is reserved and must be ignored. */
    memset(&res, 0, sizeof(res));
    res.bar_count = 6;
    cfg[0x06] = 0x00;
    assert(o0_pci_parse_capabilities(cfg, sizeof(cfg), &res) == 0);
    assert(!res.msi_supported && !res.msix_supported);

    /* Truncated image and self-referential list are rejected. */
    build_dump(cfg);
    memset(&res, 0, sizeof(res));
    res.bar_count = 6;
    assert(o0_pci_parse_capabilities(cfg, 128, &res) == -1);
    build_dump(cfg);
    cfg[0x50] = 0x05;
    cfg[0x51] = 0x50; /* next == self */
    memset(&res, 0, sizeof(res));
    res.bar_count = 6;
    assert(o0_pci_parse_capabilities(cfg, sizeof(cfg), &res) == -1);

    /* ---- MSI/MSI-X encoding ---- */
    assert(o0_pci_msi_address(0x02u) == 0xFEE02000u);
    assert(o0_pci_msi_data(0x21u, false) == 0x21u);
    assert(o0_pci_msi_data(0x21u, true) == 0x4021u);

    struct o0_pci_msix_entry entry;
    assert(o0_pci_msix_encode(0x02u, 0x30u, true, &entry) == 0);
    assert(entry.address == 0xFEE02000u);
    assert(entry.data == 0x30u);
    assert(entry.vector_control == 1u);
    assert(o0_pci_msix_encode(0, 0x100u, false, &entry) == -1);

    uint32_t table[4 * 4];
    memset(table, 0xFF, sizeof(table));
    assert(o0_pci_msix_program(table, 4, 1, 0x02u, 0x31u, false) == 0);
    assert(table[4] == 0xFEE02000u);
    assert(table[5] == 0u);
    assert(table[6] == 0x31u);
    assert(table[7] == 0u);
    assert(table[0] == 0xFFFFFFFFu); /* other entries untouched */
    assert(o0_pci_msix_program(table, 4, 4, 0x02u, 0x31u, false) == -1);
    assert(o0_pci_msix_program(NULL, 4, 0, 0x02u, 0x31u, false) == -1);

    printf("PCI host tests: BAR, capabilities, MSI/MSI-X PASS\n");
    return 0;
}
