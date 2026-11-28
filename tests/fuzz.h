/* Ozero host test - coverage-guided fuzzing harness.
 *
 * A self-contained, dependency-free fuzzer: no libFuzzer, no AFL, no external
 * libraries. It tracks edge coverage in a bitmap and uses deterministic
 * mutation (bit flips, byte substitutions, insertions, deletions, truncations,
 * splicing) to grow a corpus of inputs that reach new code paths.
 *
 * Each fuzz target registers:
 *   - a seed corpus (byte buffers),
 * *   - a target function that consumes one input buffer and returns void,
 *   - optionally, a "crash check" that validates the target's return status.
 *
 * The harness runs a fixed number of generations. In each generation it
 * selects a corpus entry (round-robin, weighted toward recent additions),
 * mutates it, runs the target under a signal handler (SIGSEGV/SIGBUS/SIGFPE
 * are caught and reported as crashes), and keeps the mutated input if it
 * increased edge coverage.
 *
 * Host build only. Run with `make test` / `make test-sanitize`.
 */

#ifndef O0_FUZZ_H
#define O0_FUZZ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Coverage bitmap size. One bit per tracked edge. */
#define O0_FUZZ_COVERAGE_BITS 65536u

/* Maximum corpus entries and maximum input size. */
#define O0_FUZZ_MAX_CORPUS 4096u
#define O0_FUZZ_MAX_INPUT 8192u

/* A single corpus entry. */
struct o0_fuzz_entry {
    uint8_t data[O0_FUZZ_MAX_INPUT];
    size_t len;
    bool active; /* false = slot free */
};

/* The fuzzer state: coverage bitmap, corpus, counters. */
struct o0_fuzz {
    uint64_t coverage[O0_FUZZ_COVERAGE_BITS / 64u];
    uint64_t rng_state;
    struct o0_fuzz_entry corpus[O0_FUZZ_MAX_CORPUS];
    uint64_t corpus_count;
    uint64_t total_runs;
    uint64_t new_coverage_runs;
    uint64_t crash_count;
    uint64_t seed_count;
};

/* Target function signature: consume one input buffer. Return non-zero to
 * signal a crash/failure that the harness records. */
typedef int (*o0_fuzz_target_fn)(const uint8_t *data, size_t len);

/* Initialise the fuzzer state. */
void o0_fuzz_init(struct o0_fuzz *f);

/* Add a seed input to the corpus. Returns false if the corpus is full. */
bool o0_fuzz_add_seed(struct o0_fuzz *f, const uint8_t *data, size_t len);

/* Record that an edge was hit. Call from the target (or from a
 * __sanitizer_cov_trace_pc_guard shim) to mark coverage. */
void o0_fuzz_cover(struct o0_fuzz *f, uint64_t edge_id);

/* Run the fuzzing loop against one target for `generations` generations.
 * Returns the number of crashes found. */
uint64_t o0_fuzz_run(struct o0_fuzz *f, o0_fuzz_target_fn target,
                     uint64_t generations);

/* Print a summary line. */
void o0_fuzz_summary(const struct o0_fuzz *f);

/* Deterministic PRNG used by the mutator (stable across hosts). */
uint64_t o0_fuzz_rng_next(struct o0_fuzz *f);
void o0_fuzz_rng_seed(struct o0_fuzz *f, uint64_t seed);

/* Mutate `in` into `out`, returning the mutated length. `out` must hold at
 * least O0_FUZZ_MAX_INPUT bytes. */
size_t o0_fuzz_mutate(struct o0_fuzz *f, const uint8_t *in, size_t in_len,
                      uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* O0_FUZZ_H */
