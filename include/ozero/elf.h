/* Ozero kernel - static Ozero user image (ELF64 subset) parser.
 *
 * The parser is a pure function over a byte buffer so host fuzz tests can drive
 * it without a process or page tables. It accepts exactly the subset approved
 * in ADR 0007 §2.7 and rejects everything else with a distinct status so the
 * negative tests can assert which rule fired.
 *
 * ELF is used here as a file container only. It does not define Ozero's
 * process model, ABI or user semantics.
 */
#ifndef O0_ELF_H
#define O0_ELF_H

#include <stddef.h>
#include <stdint.h>

#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_ELF_MAX_SEGMENTS 16u
#define O0_ELF_MAX_IMAGE_BYTES (16ull << 20)
#define O0_ELF_MAX_ALIGN (2ull << 20)

/* Program header flags, as stored in the file. */
#define O0_ELF_PF_X 0x1u
#define O0_ELF_PF_W 0x2u
#define O0_ELF_PF_R 0x4u

struct o0_elf_segment {
    uint64_t vaddr;
    uint64_t offset;
    uint64_t filesz;
    uint64_t memsz;
    uint32_t flags;
    uint32_t reserved;
};

struct o0_user_image {
    uint64_t entry;
    uint64_t image_start; /* lowest page-aligned segment start */
    uint64_t image_end;   /* highest page-aligned segment end */
    uint32_t segment_count;
    uint32_t reserved;
    struct o0_elf_segment segments[O0_ELF_MAX_SEGMENTS];
};

/* Parses and validates one ET_EXEC x86-64 ELF64 image. */
o0_status o0_elf_parse(const void *data, size_t size, struct o0_user_image *out);

#ifdef __cplusplus
}
#endif

#endif /* O0_ELF_H */
