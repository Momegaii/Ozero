/* Ozero kernel - PE32+ (x86-64) image parser.
 *
 * PE32+ is accepted here only as a container format for Windows-compatible
 * user programs (Stage 7). It is not Ozero's native executable format and it
 * does not define Ozero's runtime, ABI or process model. See
 * docs/decisions/0021-pe-loader-windows-user-mode-abi.md.
 *
 * The parser is a pure function over a byte buffer so host fuzz tests can drive
 * it without a process or page tables. It accepts PE32+ images for x86-64
 * (optional-header magic 0x20b, machine 0x8664) and rejects everything else
 * with a distinct status so the negative tests can assert which rule fired.
 *
 * All multi-byte fields are read through memcpy so the parser is safe on
 * unaligned and adversarial input.
 */
#ifndef O0_PE_H
#define O0_PE_H

#include <stddef.h>
#include <stdint.h>

#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_PE_MAX_SECTIONS 16u
#define O0_PE_MAX_IMAGE_BYTES (16ull << 20)
#define O0_PE_MAX_ALIGN (2ull << 20)

/* ---- PE/COFF constants (Microsoft PE/COFF specification) ---- */

/* DOS header. */
#define O0_PE_DOS_MAGIC 0x5A4Du /* 'MZ' */
#define O0_PE_DOS_LFANEW_OFFSET 0x3Cu

/* NT headers. */
#define O0_PE_SIGNATURE 0x00004550u /* 'PE\0\0' */

/* COFF machine types. */
#define O0_PE_MACHINE_AMD64 0x8664u

/* Optional header magic. */
#define O0_PE_OPTIONAL_MAGIC_PE32PLUS 0x20Bu

/* Section characteristics. */
#define O0_PE_SCN_CNT_CODE 0x00000020u
#define O0_PE_SCN_CNT_INITIALIZED_DATA 0x00000040u
#define O0_PE_SCN_CNT_UNINITIALIZED_DATA 0x00000080u
#define O0_PE_SCN_MEM_EXECUTE 0x20000000u
#define O0_PE_SCN_MEM_READ 0x40000000u
#define O0_PE_SCN_MEM_WRITE 0x80000000u

/* Data directory indices. */
#define O0_PE_DIR_EXPORT 0u
#define O0_PE_DIR_IMPORT 1u
#define O0_PE_DIR_RESOURCE 2u
#define O0_PE_DIR_EXCEPTION 3u
#define O0_PE_DIR_SECURITY 4u
#define O0_PE_DIR_BASERELOC 5u
#define O0_PE_DIR_DEBUG 6u
#define O0_PE_DIR_ARCHITECTURE 7u
#define O0_PE_DIR_GLOBALPTR 8u
#define O0_PE_DIR_TLS 9u
#define O0_PE_DIR_LOAD_CONFIG 10u
#define O0_PE_DIR_BOUND_IMPORT 11u
#define O0_PE_DIR_IAT 12u
#define O0_PE_DIR_DELAY_IMPORT 13u
#define O0_PE_DIR_COM_DESCRIPTOR 14u
#define O0_PE_DIR_RESERVED 15u
#define O0_PE_DIR_COUNT 16u

/* Base relocation types (x86-64). */
#define O0_PE_REL_BASED_ABSOLUTE 0u
#define O0_PE_REL_BASED_DIR64 10u

/* ---- Parsed image description ---- */

struct o0_pe_section {
    char name[8];                 /* not NUL-terminated; 8 bytes */
    uint32_t virtual_size;
    uint64_t virtual_address;     /* RVA */
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data; /* file offset */
    uint32_t characteristics;
    uint64_t load_address;        /* set by the loader: chosen base + virtual_address */
};

/* Data directory entries the loader cares about. */
struct o0_pe_directory {
    uint32_t virtual_address; /* RVA */
    uint32_t size;
};

struct o0_pe_image {
    uint64_t image_base;        /* preferred image base from the optional header */
    uint64_t entry_rva;         /* AddressOfEntryPoint */
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint32_t size_of_image;
    uint32_t number_of_sections;
    struct o0_pe_section sections[O0_PE_MAX_SECTIONS];
    struct o0_pe_directory import_directory;
    struct o0_pe_directory base_relocation_directory;
    struct o0_pe_directory exception_directory;
};

/* Parses and validates one PE32+ x86-64 image. */
o0_status o0_pe_parse(const void *data, size_t size, struct o0_pe_image *out);

#ifdef __cplusplus
}
#endif

#endif /* O0_PE_H */
