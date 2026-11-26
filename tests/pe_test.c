/* Ozero host test - PE32+ parser validation.
 *
 * Builds synthetic PE32+ images in memory and asserts that o0_pe_parse
 * accepts valid images and rejects each malformed shape with the distinct
 * status required by docs/decisions/0021-pe-loader-windows-user-mode-abi.md.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/pe.h"
#include "ozero/status.h"

#define DOS_SIZE 64u
#define LFANEW_OFF 0x3Cu
#define NT_OFF 0x80u
#define COFF_OFF (NT_OFF + 4u)
#define OPT_OFF (COFF_OFF + 20u)
#define SECT_OFF (OPT_OFF + 236u)   /* optional header: 20 std + 88 win + 16*8 data dirs */
#define FILE_SIZE 0x3000u

static uint8_t file[FILE_SIZE];

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

/* Build a minimal valid PE32+ console image:
 *   .text (RX) at RVA 0x1000, .data (RW) at RVA 0x2000,
 *   entry 0x1000, image base 0x40000000 (in-window for Ozero).
 */
static void make_valid(void) {
    memset(file, 0, sizeof(file));

    /* DOS header. */
    put16(file + 0, 0x5A4Du);              /* e_magic = 'MZ' */
    put32(file + LFANEW_OFF, (uint32_t)NT_OFF); /* e_lfanew */

    /* NT signature. */
    put32(file + NT_OFF, 0x00004550u);     /* 'PE\0\0' */

    /* COFF file header. */
    put16(file + COFF_OFF + 0, 0x8664u);   /* Machine = AMD64 */
    put16(file + COFF_OFF + 2, 2u);        /* NumberOfSections */
    /* TimeDateStamp(4), PointerToSymbolTable(4), NumberOfSymbols(4) skipped */
    put16(file + COFF_OFF + 16, 224u);     /* SizeOfOptionalHeader */
    put16(file + COFF_OFF + 18, 0u);       /* Characteristics */

    /* Optional header PE32+ (offsets from OPT_OFF per the PE/COFF spec). */
    put16(file + OPT_OFF + 0, 0x20Bu);                    /* Magic = PE32+ */
    /* Major/MinorLinkerVersion(2) skipped */
    put32(file + OPT_OFF + 4, 0u);                        /* SizeOfCode */
    put32(file + OPT_OFF + 8, 0u);                        /* SizeOfInitializedData */
    put32(file + OPT_OFF + 12, 0u);                       /* SizeOfUninitializedData */
    put32(file + OPT_OFF + 16, 0x1000u);                  /* AddressOfEntryPoint */
    put64(file + OPT_OFF + 20, 0x40000000ull);            /* ImageBase (in-window) */
    put32(file + OPT_OFF + 28, 0x1000u);                  /* SectionAlignment */
    put32(file + OPT_OFF + 32, 0x200u);                   /* FileAlignment */
    /* OS/image/subsystem versions + Win32VersionValue(16) skipped */
    put32(file + OPT_OFF + 52, 0x3000u);                  /* SizeOfImage */
    put32(file + OPT_OFF + 56, 0x200u);                   /* SizeOfHeaders */
    /* CheckSum(4) skipped */
    put16(file + OPT_OFF + 64, 3u);                       /* Subsystem = console */
    /* DllCharacteristics(2), stack/heap(32), LoaderFlags(4) skipped */
    put32(file + OPT_OFF + 104, 16u);                     /* NumberOfRvaAndSizes */
    /* 16 data directories (128 bytes), all zero */
    put16(file + COFF_OFF + 16, 236u);                    /* SizeOfOptionalHeader */

    /* Section table: .text (RX) then .data (RW). Each header is 40 bytes:
     *  Name(0) VirtualSize(8) VirtualAddress(12) SizeOfRawData(16)
     *  PointerToRawData(20) .. Characteristics(36)
     */
    memcpy(file + SECT_OFF + 0, ".text", 5);
    put32(file + SECT_OFF + 8, 0x1000u);   /* VirtualSize */
    put32(file + SECT_OFF + 12, 0x1000u);  /* VirtualAddress */
    put32(file + SECT_OFF + 16, 0x1000u);  /* SizeOfRawData */
    put32(file + SECT_OFF + 20, 0x1000u);  /* PointerToRawData */
    put32(file + SECT_OFF + 36, 0x60000020u); /* Characteristics: R|X|CNT_CODE */

    memcpy(file + SECT_OFF + 40, ".data", 5);
    put32(file + SECT_OFF + 48, 0x1000u);   /* VirtualSize */
    put32(file + SECT_OFF + 52, 0x2000u);   /* VirtualAddress */
    put32(file + SECT_OFF + 56, 0x1000u);   /* SizeOfRawData */
    put32(file + SECT_OFF + 60, 0x2000u);   /* PointerToRawData */
    put32(file + SECT_OFF + 76, 0xC0000040u); /* Characteristics: R|W|CNT_INITIALIZED_DATA */

    /* Section raw data. */
    memset(file + 0x1000, 0xcc, 0x1000); /* .text: int3 padding */
    memset(file + 0x2000, 0x00, 0x1000); /* .data: zeroed */
}

static o0_status parse(void) {
    struct o0_pe_image image;
    return o0_pe_parse(file, sizeof(file), &image);
}

static void expect(o0_status want) {
    o0_status got = parse();
    if (got != want) {
        fprintf(stderr, "pe_test: expected status %d, got %d\n", (int)want, (int)got);
    }
    assert(got == want);
}

static void test_accept(void) {
    struct o0_pe_image image;
    make_valid();
    assert(o0_pe_parse(file, sizeof(file), &image) == O0_OK);
    assert(image.image_base == 0x40000000ull);
    assert(image.entry_rva == 0x1000u);
    assert(image.number_of_sections == 2u);
    assert(image.size_of_image == 0x3000u);
    assert(memcmp(image.sections[0].name, ".text", 5) == 0);
    assert(image.sections[0].virtual_address == 0x1000u);
    assert(memcmp(image.sections[1].name, ".data", 5) == 0);
    assert(image.sections[1].virtual_address == 0x2000u);
    printf("pe_test: accept passed\n");
}

static void test_not_mz(void) {
    memset(file, 0, sizeof(file));
    put32(file + LFANEW_OFF, (uint32_t)NT_OFF);
    put32(file + NT_OFF, 0x00004550u);
    expect(O0_EPE_BAD_MZ);
}

static void test_truncated(void) {
    make_valid();
    struct o0_pe_image image;
    assert(o0_pe_parse(file, 10u, &image) == O0_EPE_TRUNCATED);
    printf("pe_test: truncated passed\n");
}

static void test_bad_signature(void) {
    make_valid();
    put32(file + NT_OFF, 0xDEADBEEFu);
    expect(O0_EPE_BAD_SIGNATURE);
}

static void test_bad_machine(void) {
    make_valid();
    put16(file + COFF_OFF + 0, 0x014cu); /* i386 */
    expect(O0_EPE_BAD_MACHINE);
}

static void test_bad_optional_magic(void) {
    make_valid();
    put16(file + OPT_OFF + 0, 0x10Bu); /* PE32 */
    expect(O0_EPE_BAD_OPTIONAL_MAGIC);
}

static void test_too_many_sections(void) {
    make_valid();
    put16(file + COFF_OFF + 2, (uint16_t)(O0_PE_MAX_SECTIONS + 1u));
    expect(O0_EPE_TOO_MANY_SECTIONS);
}

static void test_bad_alignment(void) {
    make_valid();
    put32(file + OPT_OFF + 28, 0x200u);    /* SectionAlignment */
    put32(file + OPT_OFF + 32, 0x1000u);   /* FileAlignment > SectionAlignment */
    expect(O0_EPE_BAD_ALIGNMENT);
}

static void test_section_range(void) {
    make_valid();
    /* .data raw data extends past end of file. */
    put32(file + SECT_OFF + 60, 0x4000u); /* .data PointerToRawData beyond file */
    expect(O0_EPE_SECTION_RANGE);
}

static void test_sections_overlap(void) {
    make_valid();
    /* Make .data virtual range overlap .text. */
    put64(file + SECT_OFF + 52, 0x1200u); /* .data VirtualAddress inside .text */
    expect(O0_EPE_SECTIONS_OVERLAP);
}

static void test_bad_entry(void) {
    make_valid();
    /* Entry in .data (not executable). */
    put32(file + OPT_OFF + 16, 0x2000u);
    expect(O0_EPE_BAD_ENTRY);
}

static void test_wx(void) {
    make_valid();
    /* Make .text writable as well as executable. */
    put32(file + SECT_OFF + 40, 0xE0000020u); /* R|W|X|CNT_CODE */
    expect(O0_EPE_WX);
}

static void test_bad_directory(void) {
    make_valid();
    /* Data directory 0 (export) points beyond the image. */
    put32(file + OPT_OFF + 108, 0x4000u); /* Export Directory VirtualAddress */
    put32(file + OPT_OFF + 112, 0x100u);  /* Export Directory Size: non-empty */
    expect(O0_EPE_BAD_DIRECTORY);
}

int main(void) {
    test_accept();
    test_not_mz();
    test_truncated();
    test_bad_signature();
    test_bad_machine();
    test_bad_optional_magic();
    test_too_many_sections();
    test_bad_alignment();
    test_section_range();
    test_sections_overlap();
    test_bad_entry();
    test_wx();
    test_bad_directory();
    printf("pe_test: all passed\n");
    return 0;
}
