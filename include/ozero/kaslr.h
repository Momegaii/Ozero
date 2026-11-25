/* Ozero kernel - kernel address-space layout randomisation.
 *
 * The kernel image is linked at a fixed high-half address, so this stage
 * randomises the dedicated kernel-stack region instead of pretending that text
 * relocation exists. One function derives the layout so every consumer sees
 * the same offset. See docs/decisions/0011-wave3-ioapic-percpu-kaslr-pmm.md.
 */
#ifndef O0_KASLR_H
#define O0_KASLR_H

#include <stdint.h>

#define O0_KASLR_KSTACK_BASE 0xffff900000000000ull
#define O0_KASLR_KSTACK_WINDOW 0x40000000ull
#define O0_KASLR_GRANULE 0x200000ull
#define O0_KASLR_SLOTS 512u

static inline uint64_t o0_kaslr_offset_from_seed(uint64_t seed) {
    seed ^= seed >> 33;
    seed *= 0xff51afd7ed558ccdull;
    seed ^= seed >> 33;
    seed *= 0xc4ceb9fe1a85ec53ull;
    seed ^= seed >> 33;
    return (seed % O0_KASLR_SLOTS) * O0_KASLR_GRANULE;
}

/* Called once before paging initialisation. */
void o0_kaslr_init(void);
uint64_t o0_kaslr_offset(void);
uint64_t o0_kaslr_kstack_base(void);

#endif /* O0_KASLR_H */
