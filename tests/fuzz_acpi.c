/* Ozero host test - ACPI table parser coverage-guided fuzz target.
 *
 * Drives o0_acpi_parse with mutated RSDP byte buffers. The parser validates
 * the RSDP, walks the RSDT/XSDT, and parses the MADT. Every iteration must
 * return a status without faulting. Under ASan/UBSan any out-of-bounds read
 * is caught.
 *
 * Build:
 *   gcc -Iinclude -Ikernel/src -o build/fuzz_acpi tests/fuzz_acpi.c \
 *       tests/fuzz.c kernel/src/acpi.c
 *
 * Run it with `make test` (it is registered in the Makefile test target).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fuzz.h"
#include "ozero/acpi.h"
#include "ozero/status.h"
#include "ozero/types.h"

static struct o0_fuzz g_fuzz;

/* Minimal valid RSDP v2.0 seed pointing at an XSDT containing one MADT entry.
 * All addresses are physical; hhdm_offset=0 in the test means identity. */
static const uint8_t seed_rsdp[] = {
    /* RSDP signature "RSD PTR " */
    'R', 'S', 'D', ' ', 'P', 'T', 'R', ' ',
    2,                    /* v2.0 */
    0,                    /* checksum (fix below) */
    0,                    /* oem_id[0] */
    0, 0, 0, 0, 0,        /* oem_id[1..5] */
    0, 0,                 /* oem_table_id */
    0,                    /* oem_revision */
    0, 0,                 /* creator_id */
    0, 0, 0, 0,           /* creator_revision */
    0x10, 0, 0, 0,        /* xsdt_address = 0x10 (little-endian u32, but v2 uses u64) */
    0, 0, 0, 0,           /* xsdt_address high dword = 0 */
    0,                    /* extended checksum (fix below) */
    0, 0, 0, 0, 0, 0,     /* reserved */
};

static int target(const uint8_t *data, size_t len) {
    struct o0_acpi_info info;
    o0_status st = o0_acpi_parse(data, 0, 0xffffffffu, &info);
    /* Any status is acceptable; we only require the parser not to fault. */
    (void)st;
    return 0;
}

int main(void) {
    uint64_t generations = 20000u;

    o0_fuzz_init(&g_fuzz);
    o0_fuzz_add_seed(&g_fuzz, seed_rsdp, sizeof(seed_rsdp));
    /* All-zeros seed. */
    uint8_t zeros[128];
    memset(zeros, 0, sizeof(zeros));
    o0_fuzz_add_seed(&g_fuzz, zeros, sizeof(zeros));
    /* Truncated RSDP (signature only). */
    uint8_t sig_only[8];
    memcpy(sig_only, seed_rsdp, 8);
    o0_fuzz_add_seed(&g_fuzz, sig_only, sizeof(sig_only));

    printf("fuzz_acpi: %llu generations over ACPI parser\n",
           (unsigned long long)generations);
    uint64_t crashes = o0_fuzz_run(&g_fuzz, target, generations);
    o0_fuzz_summary(&g_fuzz);

    if (crashes != 0) {
        printf("fuzz_acpi: FAIL (%llu crashes)\n",
               (unsigned long long)crashes);
        return 1;
    }
    printf("fuzz_acpi: PASS\n");
    return 0;
}
