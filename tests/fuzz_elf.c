/* Ozero host test - ELF parser coverage-guided fuzz target.
 *
 * Drives o0_elf_parse with mutated byte buffers. The parser is a pure function
 * over a byte buffer, so every iteration must return a status without
 * faulting. Under ASan/UBSan any out-of-bounds read or write is caught.
 *
 * Build:
 *   gcc -Iinclude -Ikernel/src -o build/fuzz_elf tests/fuzz_elf.c \
 *       tests/fuzz.c kernel/src/elf.c
 *
 * Run it with `make test` (it is registered in the Makefile test target).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fuzz.h"
#include "ozero/elf.h"
#include "ozero/status.h"
#include "ozero/types.h"

static struct o0_fuzz g_fuzz;

/* Minimal valid ELF64 ET_EXEC seed, large enough to exercise the header and
 * program-header parsing paths. */
static const uint8_t seed_elf[] = {
    0x7f, 'E', 'L', 'F',            /* magic */
    2,                              /* ELFCLASS64 */
    1,                              /* ELFDATA2LSB */
    1,                              /* EV_CURRENT */
    0,                              /* OS/ABI */
    0, 0, 0, 0, 0, 0, 0,            /* pad */
    2, 0,                           /* ET_EXEC */
    0x3e, 0,                        /* EM_X86_64 */
    1, 0, 0, 0,                     /* EV_CURRENT */
    0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0, /* entry */
    0x40, 0, 0, 0, 0, 0, 0, 0,      /* phoff */
    0, 0, 0, 0, 0, 0, 0, 0,         /* shoff */
    0, 0, 0, 0,                     /* flags */
    64, 0,                          /* ehsize */
    56, 0,                          /* phentsize */
    1, 0,                           /* phnum */
    0, 0,                           /* shentsize */
    0, 0,                           /* shnum */
    0, 0,                           /* shstrndx */
    /* One PT_LOAD program header */
    1, 0, 0, 0,                     /* p_type = PT_LOAD */
    0x6, 0, 0, 0,                   /* p_flags = RW */
    0, 0, 0, 0, 0, 0, 0, 0,         /* p_offset */
    0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0, /* p_vaddr */
    0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0, /* p_paddr */
    0x10, 0, 0, 0, 0, 0, 0, 0,      /* p_filesz */
    0x10, 0, 0, 0, 0, 0, 0, 0,      /* p_memsz */
    0x10, 0, 0, 0, 0, 0, 0, 0,      /* p_align */
};

static int target(const uint8_t *data, size_t len) {
    struct o0_user_image image;
    o0_status st = o0_elf_parse(data, len, &image);
    /* Any status is acceptable; we only require the parser not to fault. */
    (void)st;
    return 0;
}

int main(void) {
    uint64_t generations = 20000u;

    o0_fuzz_init(&g_fuzz);
    o0_fuzz_add_seed(&g_fuzz, seed_elf, sizeof(seed_elf));
    /* Add some near-valid seeds: valid header with truncated program header. */
    uint8_t truncated[64];
    memcpy(truncated, seed_elf, 60);
    truncated[60] = 0x00;
    truncated[61] = 0x00;
    o0_fuzz_add_seed(&g_fuzz, truncated, sizeof(truncated));
    /* All-zeros seed. */
    uint8_t zeros[128];
    memset(zeros, 0, sizeof(zeros));
    o0_fuzz_add_seed(&g_fuzz, zeros, sizeof(zeros));

    printf("fuzz_elf: %llu generations over ELF parser\n",
           (unsigned long long)generations);
    uint64_t crashes = o0_fuzz_run(&g_fuzz, target, generations);
    o0_fuzz_summary(&g_fuzz);

    if (crashes != 0) {
        printf("fuzz_elf: FAIL (%llu crashes)\n",
               (unsigned long long)crashes);
        return 1;
    }
    printf("fuzz_elf: PASS\n");
    return 0;
}
