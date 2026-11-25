/* Ozero kernel - KASLR layout derivation test (host build). */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ozero/kaslr.h"

int main(void) {
    int failures = 0;
    printf("Ozero KASLR tests\n\n");

    uint64_t seen[O0_KASLR_SLOTS];
    for (uint32_t i = 0; i < O0_KASLR_SLOTS; ++i) {
        seen[i] = 0;
    }
    for (uint64_t seed = 1; seed <= 4096u; ++seed) {
        uint64_t offset = o0_kaslr_offset_from_seed(seed);
        if ((offset & (O0_KASLR_GRANULE - 1u)) != 0) {
            printf("FAIL: offset not 2 MiB aligned\n");
            ++failures;
            break;
        }
        if (offset + O0_KASLR_GRANULE > O0_KASLR_KSTACK_WINDOW) {
            printf("FAIL: offset outside stack window\n");
            ++failures;
            break;
        }
        seen[offset / O0_KASLR_GRANULE] = 1;
    }
    uint32_t slots = 0;
    for (uint32_t i = 0; i < O0_KASLR_SLOTS; ++i) {
        slots += seen[i] != 0;
    }
    if (slots < 2u) {
        printf("FAIL: layout derivation does not vary\n");
        ++failures;
    }

    if (failures == 0) {
        printf("kaslr: PASS\n");
        return 0;
    }
    printf("kaslr: %d failure(s)\n", failures);
    return 1;
}
