/* Ozero host test - PE32+ parser fuzz test.
 *
 * Drives o0_pe_parse with deterministic pseudo-random byte buffers. The parser
 * is a pure function over a byte buffer (memcpy + bounds checks only), so every
 * iteration must return a status without faulting. See
 * docs/decisions/0021-pe-loader-windows-user-mode-abi.md.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ozero/pe.h"
#include "ozero/status.h"

#define FUZZ_ITERATIONS 20000u
#define FUZZ_MAX_SIZE 4096u

/* Simple 64-bit LCG, deterministic across hosts. */
static uint64_t rng_state = 0x123456789abcdef0ull;
static uint8_t next_byte(void) {
    rng_state = rng_state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint8_t)(rng_state >> 32);
}

int main(void) {
    uint8_t buf[FUZZ_MAX_SIZE];
    uint64_t accepted = 0, rejected = 0;

    for (uint64_t iter = 0; iter < FUZZ_ITERATIONS; ++iter) {
        size_t size = (size_t)(next_byte() % (FUZZ_MAX_SIZE + 1u));
        for (size_t i = 0; i < size; ++i) {
            buf[i] = next_byte();
        }
        struct o0_pe_image image;
        o0_status st = o0_pe_parse(buf, size, &image);
        if (st == O0_OK) {
            ++accepted;
        } else {
            ++rejected;
        }
        /* The parser must never leak memory or crash; it only reports a status. */
    }

    printf("pe_fuzz: %llu iterations, accepted=%llu rejected=%llu\n",
           (unsigned long long)FUZZ_ITERATIONS, (unsigned long long)accepted,
           (unsigned long long)rejected);

    /* Sanity: a buffer of all zeros is not MZ, so it must be rejected, not
     * accepted. If every random buffer were accepted the parser would be
     * trivially wrong. */
    memset(buf, 0, sizeof(buf));
    assert(o0_pe_parse(buf, sizeof(buf), &(struct o0_pe_image){0}) != O0_OK);
    printf("pe_fuzz: all passed\n");
    return 0;
}
