/* Ozero host test - ACPI table parser coverage-guided fuzz target.
 *
 * Drives o0_acpi_parse against a controlled in-memory ACPI table arena.
 * The arena holds a valid RSDP -> XSDT -> MADT chain; the fuzz input is
 * overlaid onto the arena so mutations can corrupt signatures, checksums,
 * table addresses, and MADT records in place. Physical addresses inside the
 * tables are offsets within the arena, mapped by hhdm_offset so the parser
 * never dereferences memory outside the arena.
 *
 * Every iteration must return a status without faulting. Under ASan/UBSan
 * any out-of-bounds read is caught.
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

/* ---- Controlled ACPI table arena ---- */

/* Size of the emulated physical memory arena. 8 KiB fits the fuzzer's
 * maximum input size (O0_FUZZ_MAX_INPUT) so seeds are accepted. */
#define O0_ACPI_ARENA_SIZE 8192u

/* Offsets within the arena where the tables live. These are the "physical"
 * addresses the RSDP and XSDT reference. Packed tightly to stay under
 * O0_FUZZ_MAX_INPUT. */
#define O0_ACPI_RSDP_OFFSET 0x000u
#define O0_ACPI_XSDT_OFFSET 0x100u
#define O0_ACPI_MADT_OFFSET 0x200u

static uint8_t g_arena[O0_ACPI_ARENA_SIZE];
static uint64_t g_hhdm_offset; /* maps physical address 0 -> g_arena */

/* ---- Little-endian helpers ---- */

static void write32_le(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
    }
}

static void write64_le(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
    }
}

static uint8_t checksum_bytes(const uint8_t *p, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum;
}

/* Set the checksum byte so that bytes [0,len) sum to zero. */
static void fix_checksum(uint8_t *p, size_t len, size_t checksum_offset) {
    uint8_t c = checksum_bytes(p, len);
    if (c != 0u) {
        p[checksum_offset] = (uint8_t)(p[checksum_offset] - c);
    }
}

/* ---- Table builders (produce checksum-valid ACPI tables) ---- */

/* Build an RSDP v2.0 pointing at xsdt_phys (an offset within the arena).
 *
 * RSDP v2.0 layout (36 bytes):
 *   0-7  Signature "RSD PTR "
 *   8    Checksum (bytes 0-19 sum to 0)
 *   9-14 OEM ID
 *   15   Revision (= 2)
 *   16-23 OEM Table ID / reserved
 *   24-31 XSDT physical address (u64)
 *   32   Extended checksum (bytes 0-35 sum to 0)
 *   33-35 Reserved
 */
static void build_rsdp(uint8_t *p, uint64_t xsdt_phys) {
    memcpy(p, "RSD PTR ", 8);
    p[15] = 2u;                     /* revision 2.0 */
    write64_le(p + 24, xsdt_phys);  /* xsdt_address */
    /* OEM ID, OEM Table ID, etc. are all zero. */
    fix_checksum(p, 20u, 8u);   /* first checksum covers bytes 0-19 */
    fix_checksum(p, 36u, 32u);  /* extended checksum covers bytes 0-35 */
}

/* Build an XSDT with one entry pointing at madt_phys.
 *
 * XSDT layout:
 *   0-3  Signature "XSDT"
 *   4-7  Length
 *   8    Revision (= 1)
 *   9    Checksum (bytes 0-length-1 sum to 0)
 *   ...  OEM fields (zero)
 *   36-43 Entry 0: MADT physical address (u64)
 */
static void build_xsdt(uint8_t *p, uint64_t madt_phys) {
    memcpy(p, "XSDT", 4);
    uint32_t len = 36u + 8u; /* header + one 8-byte entry */
    write32_le(p + 4, len);
    p[8] = 1u;               /* revision */
    write64_le(p + 36, madt_phys); /* entry 0 = MADT physical address */
    fix_checksum(p, len, 9u); /* checksum covers bytes 0-len-1 */
}

/* Build a MADT with one Processor Local APIC record.
 *
 * MADT layout:
 *   0-3  Signature "APIC"
 *   4-7  Length
 *   8    Revision (= 1)
 *   9    Checksum (bytes 0-length-1 sum to 0)
 *   36-39 Local APIC address
 *   40-43 Flags
 *   44+  Records: type(1), length(1), ...
 */
static void build_madt(uint8_t *p) {
    memcpy(p, "APIC", 4);
    uint32_t len = 44u + 8u; /* header + one 8-byte LAPIC record */
    write32_le(p + 4, len);
    p[8] = 1u;               /* revision */
    write32_le(p + 36, 0xFEE00000u); /* lapic_address */
    write32_le(p + 40, 0u);          /* madt_flags */
    /* Record 0: type=0 (Processor Local APIC), length=8 */
    p[44] = 0u;
    p[45] = 8u;
    p[46] = 0u;   /* acpi_uid */
    p[47] = 0u;   /* apic_id */
    p[48] = 1u;   /* flags: enabled */
    p[49] = 0u;
    p[50] = 0u;
    p[51] = 0u;
    fix_checksum(p, len, 9u); /* checksum covers bytes 0-len-1 */
}

/* Reset the arena to a known-good RSDP -> XSDT -> MADT chain. */
static void arena_init(void) {
    g_hhdm_offset = (uint64_t)(uintptr_t)g_arena;
    memset(g_arena, 0, sizeof(g_arena));
    build_rsdp(g_arena + O0_ACPI_RSDP_OFFSET, O0_ACPI_XSDT_OFFSET);
    build_xsdt(g_arena + O0_ACPI_XSDT_OFFSET, O0_ACPI_MADT_OFFSET);
    build_madt(g_arena + O0_ACPI_MADT_OFFSET);
}

/* ---- Fuzz target ---- */

static int target(const uint8_t *data, size_t len) {
    struct o0_acpi_info info;

    /* Start from a known-good arena so the parser can walk RSDP -> XSDT
     * -> MADT. The fuzz input is then overlaid onto the arena so mutations
     * can corrupt any byte of any table in place. */
    arena_init();

    size_t copy_len = len < O0_ACPI_ARENA_SIZE ? len : O0_ACPI_ARENA_SIZE;
    if (copy_len > 0) {
        memcpy(g_arena, data, copy_len);
    }

    /* Parse: RSDP lives at a fixed offset within the arena. Physical
     * addresses inside the tables are arena offsets; hhdm_offset maps them
     * back into g_arena so the parser never leaves the buffer. */
    o0_status st = o0_acpi_parse(g_arena + O0_ACPI_RSDP_OFFSET,
                                 g_hhdm_offset, UINT32_MAX, &info);
    (void)st;
    return 0;
}

int main(void) {
    uint64_t generations = 20000u;

    o0_fuzz_init(&g_fuzz);

    /* Seed 1: fully valid arena contents (RSDP + XSDT + MADT chain). */
    {
        uint8_t seed[O0_ACPI_ARENA_SIZE];
        memset(seed, 0, sizeof(seed));
        build_rsdp(seed + O0_ACPI_RSDP_OFFSET, O0_ACPI_XSDT_OFFSET);
        build_xsdt(seed + O0_ACPI_XSDT_OFFSET, O0_ACPI_MADT_OFFSET);
        build_madt(seed + O0_ACPI_MADT_OFFSET);
        o0_fuzz_add_seed(&g_fuzz, seed, sizeof(seed));
    }

    /* Seed 2: all-zeros. */
    {
        uint8_t zeros[O0_ACPI_ARENA_SIZE];
        memset(zeros, 0, sizeof(zeros));
        o0_fuzz_add_seed(&g_fuzz, zeros, sizeof(zeros));
    }

    /* Seed 3: RSDP signature only at the RSDP offset (truncated). */
    {
        uint8_t sig_only[O0_ACPI_ARENA_SIZE];
        memset(sig_only, 0, sizeof(sig_only));
        memcpy(sig_only + O0_ACPI_RSDP_OFFSET, "RSD PTR ", 8);
        o0_fuzz_add_seed(&g_fuzz, sig_only, sizeof(sig_only));
    }

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
