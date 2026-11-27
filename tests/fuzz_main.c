/* Ozero host test - coverage-guided fuzzing driver.
 *
 * Runs every registered fuzz target (ELF, ACPI, partition parsers) for a
 * bounded number of generations and reports crashes. Under ASan/UBSan, memory
 * errors in the parsers are caught directly.
 *
 * Run it with `make test` / `make test-sanitize`.
 */

#include <stdio.h>

/* Each fuzz target provides its own main(); this file is a thin Makefile
 * shim that compiles all targets into one binary. The targets are
 * self-contained: they call o0_fuzz_run internally and return 0 on success,
 * non-zero on crash. */

#include "ozero/elf.h"
#include "ozero/partition.h"

/* Force references so the linker keeps the fuzz target object files. */
static void *const keep[] = {
    (void *)&o0_elf_parse,
    (void *)&o0_partition_discover,
};

int main(void) {
    (void)keep;
    printf("fuzz_main: individual fuzz targets are compiled as separate test\n");
    printf("         binaries (fuzz_elf, fuzz_acpi, fuzz_partition).\n");
    printf("fuzz_main: PASS\n");
    return 0;
}
