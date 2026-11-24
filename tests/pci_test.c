#include <stdio.h>
#include <assert.h>
#include "ozero/pci.h"

int main(void) {
    struct o0_pci_bar bar;
    /* 32-bit memory BAR */
    o0_pci_decode_bar(0x10000000u, 0, 0, 0, &bar);
    assert(bar.is_memory && !bar.is_64bit && bar.base == 0x10000000u);
    /* 64-bit memory BAR */
    o0_pci_decode_bar(0x10000004u, 0x000000A0u, 0, 1, &bar);
    assert(bar.is_memory && bar.is_64bit && bar.base == 0x000000A010000000u);
    /* IO BAR */
    o0_pci_decode_bar(0x00001001u, 0, 0, 2, &bar);
    assert(!bar.is_memory);
    printf("PCI host tests: basic decode PASS\n");
    return 0;
}