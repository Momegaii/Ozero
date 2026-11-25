#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/elf.h"
#include "ozero/status.h"
#include "ozero/uapi.h"

#define EHDR 64u
#define PHDR 56u
#define FILE_SIZE 0x1010u

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static void put_header(uint8_t *f, uint32_t phnum) {
    memset(f, 0, EHDR);
    f[0] = 0x7f;
    f[1] = 'E';
    f[2] = 'L';
    f[3] = 'F';
    f[4] = 2; /* ELFCLASS64 */
    f[5] = 1; /* ELFDATA2LSB */
    f[6] = 1; /* EV_CURRENT */
    put16(f + 16, 2);   /* ET_EXEC */
    put16(f + 18, 62);  /* EM_X86_64 */
    put32(f + 20, 1);   /* EV_CURRENT */
    put64(f + 24, 0x40000000ull); /* e_entry */
    put64(f + 32, EHDR);          /* e_phoff */
    put16(f + 52, EHDR);          /* e_ehsize */
    put16(f + 54, PHDR);          /* e_phentsize */
    put16(f + 56, (uint16_t)phnum);
}

static void put_load(uint8_t *ph, uint64_t vaddr, uint64_t offset, uint64_t filesz,
                     uint64_t memsz, uint64_t align, uint32_t flags) {
    put32(ph + 0, 1); /* PT_LOAD */
    put32(ph + 4, flags);
    put64(ph + 8, offset);
    put64(ph + 16, vaddr);
    put64(ph + 24, vaddr); /* p_paddr */
    put64(ph + 32, filesz);
    put64(ph + 40, memsz);
    put64(ph + 48, align);
}

static uint8_t file[FILE_SIZE];

static void make_valid(void) {
    memset(file, 0x90, sizeof(file));
    put_header(file, 1);
    put_load(file + EHDR, 0x40000000ull, 0x1000u, 16u, 16u, 0x1000u,
             0x4u /* R */ | 0x1u /* X */);
    memset(file + 0x1000, 0xcc, 16);
}

static o0_status parse(void) {
    struct o0_user_image image;
    return o0_elf_parse(file, sizeof(file), &image);
}

static void expect(o0_status want) {
    o0_status got = parse();
    if (got != want) {
        fprintf(stderr, "elf_test: expected status %d, got %d\n", (int)want, (int)got);
    }
    assert(got == want);
}

static void test_accept(void) {
    struct o0_user_image image;
    make_valid();
    assert(o0_elf_parse(file, sizeof(file), &image) == O0_OK);
    assert(image.segment_count == 1u);
    assert(image.entry == 0x40000000ull);
    assert(image.image_start == 0x40000000ull);
    assert(image.image_end == 0x40001000ull);
    assert(image.segments[0].filesz == 16u);
    assert(image.segments[0].memsz == 16u);

    /* PT_NOTE and PT_GNU_STACK are accepted and ignored. */
    make_valid();
    put32(file + EHDR + 0, 4u); /* PT_NOTE */
    assert(parse() == O0_ENOENT); /* no loadable segment left */
    make_valid();
    put32(file + EHDR + 0, 0x6474e551u);
    assert(parse() == O0_ENOENT);
}

static void test_header_rejections(void) {
    make_valid();
    file[0] = 0;
    expect(O0_EINVAL);
    make_valid();
    file[4] = 1; /* 32-bit */
    expect(O0_EINVAL);
    make_valid();
    file[5] = 2; /* big endian */
    expect(O0_EINVAL);
    make_valid();
    file[6] = 0;
    expect(O0_EINVAL);
    make_valid();
    put16(file + 16, 3); /* ET_DYN */
    expect(O0_EINVAL);
    make_valid();
    put16(file + 18, 3); /* EM_386 */
    expect(O0_EINVAL);
    make_valid();
    put16(file + 54, 32); /* wrong phentsize */
    expect(O0_EINVAL);
    make_valid();
    put16(file + 56, 0);
    expect(O0_ERANGE);
    make_valid();
    put16(file + 56, O0_ELF_MAX_SEGMENTS + 1u);
    expect(O0_ERANGE);
    make_valid();
    put64(file + 32, 0x100000ull); /* phoff past EOF */
    expect(O0_ERANGE);
    make_valid();
    put64(file + 24, 0x1000ull); /* entry in the null guard */
    expect(O0_ERANGE);
    make_valid();
    put64(file + 24, 0x0000800000000000ull); /* entry in the kernel half */
    expect(O0_ERANGE);
}

static void test_segment_rejections(void) {
    make_valid();
    put32(file + EHDR, 3u); /* PT_INTERP */
    expect(O0_ENOTSUP);
    make_valid();
    put32(file + EHDR, 2u); /* PT_DYNAMIC (relocations) */
    expect(O0_ENOTSUP);
    make_valid();
    put32(file + EHDR, 7u); /* PT_TLS */
    expect(O0_ENOTSUP);
    make_valid();
    put32(file + EHDR, 6u); /* PT_PHDR */
    expect(O0_ENOTSUP);
    make_valid();
    put64(file + EHDR + 40, 8u); /* memsz < filesz (filesz 16) */
    expect(O0_ERANGE);
    make_valid();
    put64(file + EHDR + 32, 0u);
    put64(file + EHDR + 40, 0u); /* empty segment: skipped */
    expect(O0_ENOENT);
    make_valid();
    put_header(file, 2);
    put_load(file + EHDR + PHDR, 0u, 0u, 0u, 0u, 0x1000u, 0x6u);
    assert(parse() == O0_OK); /* empty trailing segment is harmless */
    make_valid();
    put64(file + EHDR + 32, 0x2000u); /* filesz beyond EOF */
    expect(O0_ERANGE);
    make_valid();
    put64(file + EHDR + 16, 0x1000ull); /* segment in the null guard */
    expect(O0_ERANGE);
    make_valid();
    put64(file + EHDR + 16, 0x7ffe0000ull); /* beyond the image window */
    expect(O0_ERANGE);
    make_valid();
    put64(file + EHDR + 48, 0x1800u); /* not a power of two */
    expect(O0_EINVAL);
    make_valid();
    put64(file + EHDR + 48, 0x400000u); /* align above 2 MiB */
    expect(O0_EINVAL);
    make_valid();
    put64(file + EHDR + 48, 0x2000u); /* vaddr/offset not congruent */
    expect(O0_ERANGE);
    make_valid();
    put64(file + EHDR + 8, 0x1000u);
    put64(file + EHDR + 16, 0x40001000ull);
    put64(file + EHDR + 24, 0x40001000ull);
    put64(file + EHDR + 32, 0u);
    put64(file + EHDR + 40, 0x1000u);
    put64(file + EHDR + 48, 0x2000u); /* congruent: both are 0x1000 mod 0x2000 */
    put64(file + 24, 0x40001000ull);
    expect(O0_OK);
    make_valid();
    put32(file + EHDR + 4, 0x3u); /* W + X */
    expect(O0_EPERM);
    make_valid();
    put64(file + 24, 0x50000000ull); /* entry outside any exec segment */
    expect(O0_ENOENT);
}

static void test_overlap(void) {
    make_valid();
    memset(file, 0x90, sizeof(file));
    put_header(file, 2);
    put_load(file + EHDR, 0x40000000ull, 0x1000u, 16u, 16u, 0x1000u, 0x5u);
    put_load(file + EHDR + PHDR, 0x40000000ull, 0x1000u, 16u, 16u, 0x1000u, 0x6u);
    put64(file + 24, 0x40000000ull);
    expect(O0_EEXIST);
}

static void check_invariants(const struct o0_user_image *img, o0_status st) {
    if (st != O0_OK) {
        return;
    }
    assert(img->segment_count >= 1u);
    assert(img->segment_count <= O0_ELF_MAX_SEGMENTS);
    assert(img->image_end > img->image_start);
    assert(img->entry >= O0_USER_NULL_GUARD_END);
    assert(img->entry < O0_USER_IMAGE_END);
    bool entry_ok = false;
    for (uint32_t i = 0; i < img->segment_count; ++i) {
        const struct o0_elf_segment *s = &img->segments[i];
        assert(s->filesz <= s->memsz);
        assert(s->memsz != 0u);
        assert((s->flags & (O0_ELF_PF_W | O0_ELF_PF_X)) !=
               (O0_ELF_PF_W | O0_ELF_PF_X));
        assert(s->vaddr >= O0_USER_NULL_GUARD_END);
        assert(s->vaddr + s->memsz <= O0_USER_IMAGE_END);
        if ((s->flags & O0_ELF_PF_X) != 0 && img->entry >= s->vaddr &&
            img->entry < s->vaddr + s->memsz) {
            entry_ok = true;
        }
        for (uint32_t j = 0; j < i; ++j) {
            const struct o0_elf_segment *o = &img->segments[j];
            uint64_t a0 = s->vaddr & ~0xfffull;
            uint64_t a1 = (s->vaddr + s->memsz + 0xfffull) & ~0xfffull;
            uint64_t b0 = o->vaddr & ~0xfffull;
            uint64_t b1 = (o->vaddr + o->memsz + 0xfffull) & ~0xfffull;
            assert(!(a0 < b1 && b0 < a1));
        }
    }
    assert(entry_ok);
}

static void test_fuzz(void) {
    static uint8_t copy[FILE_SIZE];
    uint64_t state = 0x9e3779b97f4a7c15ull;
    for (unsigned iter = 0; iter < 4000u; ++iter) {
        make_valid();
        memcpy(copy, file, sizeof(copy));
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        unsigned changes = (unsigned)(state % 4u) + 1u;
        for (unsigned c = 0; c < changes; ++c) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            size_t at = (size_t)(state % sizeof(copy));
            copy[at] = (uint8_t)(state >> 32);
        }
        struct o0_user_image image;
        o0_status st = o0_elf_parse(copy, sizeof(copy), &image);
        check_invariants(&image, st);

        /* Truncation must never read past the supplied length. */
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        size_t cut = (size_t)(state % sizeof(copy));
        memset(&image, 0, sizeof(image));
        st = o0_elf_parse(copy, cut, &image);
        check_invariants(&image, st);
    }
    /* Pure random input. */
    for (unsigned iter = 0; iter < 1000u; ++iter) {
        for (size_t i = 0; i < sizeof(copy); ++i) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            copy[i] = (uint8_t)(state >> 24);
        }
        struct o0_user_image image;
        o0_status st = o0_elf_parse(copy, sizeof(copy), &image);
        check_invariants(&image, st);
    }
}

int main(void) {
    test_accept();
    test_header_rejections();
    test_segment_rejections();
    test_overlap();
    test_fuzz();
    return 0;
}
