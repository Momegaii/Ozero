/* Ozero kernel - static ELF64 (PT_LOAD only) parser.
 *
 * Accepted file shape: ELFCLASS64, ELFDATA2LSB, EV_CURRENT, EM_X86_64, ET_EXEC.
 * PT_LOAD is honoured; PT_GNU_STACK and PT_NOTE are accepted and ignored;
 * anything else is refused. See ADR 0007 §2.7.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/elf.h"
#include "ozero/status.h"
#include "ozero/types.h"
#include "ozero/uapi.h"

#define O0_EHDR_SIZE 64u
#define O0_PHDR_SIZE 56u

#define O0_EI_CLASS 4u
#define O0_EI_DATA 5u
#define O0_EI_VERSION 6u
#define O0_EI_NIDENT 16u

#define O0_ET_EXEC 2u
#define O0_EM_X86_64 62u
#define O0_EV_CURRENT 1u

#define O0_PT_NULL 0u
#define O0_PT_LOAD 1u
#define O0_PT_DYNAMIC 2u
#define O0_PT_INTERP 3u
#define O0_PT_NOTE 4u
#define O0_PT_PHDR 6u
#define O0_PT_TLS 7u
#define O0_PT_GNU_STACK 0x6474e551u

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static bool is_pow2(uint64_t v) {
    return v != 0 && (v & (v - 1u)) == 0;
}

static bool ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start,
                           uint64_t b_end) {
    return a_start < b_end && b_start < a_end;
}

o0_status o0_elf_parse(const void *data, size_t size, struct o0_user_image *out) {
    if (data == NULL || out == NULL) {
        return O0_EINVAL;
    }
    const uint8_t *file = (const uint8_t *)data;
    if (size < O0_EHDR_SIZE) {
        return O0_ERANGE;
    }
    if (file[0] != 0x7fu || file[1] != 'E' || file[2] != 'L' || file[3] != 'F') {
        return O0_EINVAL;
    }
    if (file[O0_EI_CLASS] != 2u /* ELFCLASS64 */ ||
        file[O0_EI_DATA] != 1u /* ELFDATA2LSB */ ||
        file[O0_EI_VERSION] != O0_EV_CURRENT) {
        return O0_EINVAL;
    }
    if (rd16(file + 16) != O0_ET_EXEC || rd16(file + 18) != O0_EM_X86_64 ||
        rd32(file + 20) != O0_EV_CURRENT) {
        return O0_EINVAL;
    }
    if (rd16(file + 52) != O0_EHDR_SIZE || rd16(file + 54) != O0_PHDR_SIZE) {
        return O0_EINVAL;
    }

    uint64_t phoff = rd64(file + 32);
    uint32_t phnum = rd16(file + 56);
    if (phnum == 0u || phnum > O0_ELF_MAX_SEGMENTS) {
        return O0_ERANGE;
    }
    if (phoff > size || (uint64_t)phnum * O0_PHDR_SIZE > size - phoff) {
        return O0_ERANGE;
    }

    uint64_t entry = rd64(file + 24);
    if (entry >= O0_USER_LIMIT || entry < O0_USER_NULL_GUARD_END) {
        return O0_ERANGE;
    }

    for (uint32_t i = 0; i < O0_ELF_MAX_SEGMENTS; ++i) {
        out->segments[i].vaddr = 0;
        out->segments[i].offset = 0;
        out->segments[i].filesz = 0;
        out->segments[i].memsz = 0;
        out->segments[i].flags = 0;
        out->segments[i].reserved = 0;
    }
    out->entry = entry;
    out->image_start = UINT64_MAX;
    out->image_end = 0;
    out->segment_count = 0;
    out->reserved = 0;

    uint64_t total = 0;
    const uint64_t stack_guard_low = O0_USER_STACK_BASE - O0_PAGE_SIZE;

    for (uint32_t i = 0; i < phnum; ++i) {
        const uint8_t *ph = file + phoff + (uint64_t)i * O0_PHDR_SIZE;
        uint32_t type = rd32(ph);
        uint32_t flags = rd32(ph + 4);
        uint64_t offset = rd64(ph + 8);
        uint64_t vaddr = rd64(ph + 16);
        uint64_t filesz = rd64(ph + 32);
        uint64_t memsz = rd64(ph + 40);
        uint64_t align = rd64(ph + 48);

        if (type == O0_PT_NOTE || type == O0_PT_GNU_STACK) {
            continue;
        }
        if (type != O0_PT_LOAD) {
            return O0_ENOTSUP;
        }

        if (filesz > memsz) {
            return O0_ERANGE;
        }
        /* Linkers emit an empty RW PT_LOAD when an image has no writable
         * sections; it maps nothing, so it is skipped rather than rejected. */
        if (filesz == 0u && memsz == 0u) {
            continue;
        }
        if (memsz == 0) {
            return O0_EINVAL;
        }
        if (vaddr < O0_USER_NULL_GUARD_END || vaddr >= O0_USER_IMAGE_END) {
            return O0_ERANGE;
        }
        if (memsz > O0_USER_IMAGE_END - vaddr) {
            return O0_ERANGE;
        }
        if (offset > size || filesz > size - offset) {
            return O0_ERANGE;
        }
        if (align != 0 && align != 1) {
            if (!is_pow2(align) || align > O0_ELF_MAX_ALIGN) {
                return O0_EINVAL;
            }
            if ((vaddr & (align - 1u)) != (offset & (align - 1u))) {
                return O0_ERANGE;
            }
        }
        if ((flags & (O0_ELF_PF_W | O0_ELF_PF_X)) == (O0_ELF_PF_W | O0_ELF_PF_X)) {
            return O0_EPERM;
        }

        uint64_t start = O0_ALIGN_DOWN(vaddr, O0_PAGE_SIZE);
        uint64_t end = O0_ALIGN_UP(vaddr + memsz, O0_PAGE_SIZE);
        if (end <= start) {
            return O0_EOVERFLOW;
        }
        uint64_t span = end - start;
        if (span > O0_ELF_MAX_IMAGE_BYTES || total > O0_ELF_MAX_IMAGE_BYTES - span) {
            return O0_ENOMEM;
        }
        total += span;

        /* Reject overlap with the stack window (including its guard page). */
        if (ranges_overlap(start, end, stack_guard_low, O0_USER_STACK_TOP)) {
            return O0_EEXIST;
        }
        for (uint32_t j = 0; j < out->segment_count; ++j) {
            const struct o0_elf_segment *other = &out->segments[j];
            uint64_t other_start = O0_ALIGN_DOWN(other->vaddr, O0_PAGE_SIZE);
            uint64_t other_end = O0_ALIGN_UP(other->vaddr + other->memsz, O0_PAGE_SIZE);
            if (ranges_overlap(start, end, other_start, other_end)) {
                return O0_EEXIST;
            }
        }

        struct o0_elf_segment *seg = &out->segments[out->segment_count++];
        seg->vaddr = vaddr;
        seg->offset = offset;
        seg->filesz = filesz;
        seg->memsz = memsz;
        seg->flags = flags;

        if (start < out->image_start) {
            out->image_start = start;
        }
        if (end > out->image_end) {
            out->image_end = end;
        }
    }

    if (out->segment_count == 0) {
        return O0_ENOENT;
    }

    bool entry_ok = false;
    for (uint32_t i = 0; i < out->segment_count; ++i) {
        const struct o0_elf_segment *seg = &out->segments[i];
        if ((seg->flags & O0_ELF_PF_X) != 0 && entry >= seg->vaddr &&
            entry < seg->vaddr + seg->memsz) {
            entry_ok = true;
        }
    }
    if (!entry_ok) {
        return O0_ENOENT;
    }

    return O0_OK;
}
