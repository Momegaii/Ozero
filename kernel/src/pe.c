/* Ozero kernel - PE32+ (x86-64) image parser.
 *
 * Pure function over a byte buffer: no memory allocation, no page tables, no
 * assembly. Safe on unaligned and adversarial input. See include/ozero/pe.h
 * and docs/decisions/0021-pe-loader-windows-user-mode-abi.md.
 */

#include <string.h>

#include "ozero/pe.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define O0_PE_DOS_HEADER_SIZE 64u
#define O0_PE_NT_SIGNATURE_SIZE 4u
#define O0_PE_COFF_HEADER_SIZE 20u
#define O0_PE_OPTIONAL_HEADER_STD_SIZE 28u   /* PE32+ standard fields through BaseOfCode */
#define O0_PE_OPTIONAL_HEADER_WIN_SIZE 68u   /* Windows-specific fields through NumberOfRvaAndSizes */
#define O0_PE_DATA_DIRECTORY_SIZE 8u
#define O0_PE_SECTION_HEADER_SIZE 40u
#define O0_PE_BASE_RELOC_BLOCK_HEADER_SIZE 8u
#define O0_PE_BASE_RELOC_ENTRY_SIZE 2u

/* ---- bounds-checked little-endian readers (memcpy, safe on unaligned input) ---- */

static int pe_read_u8(const uint8_t *data, size_t size, size_t off, uint8_t *out) {
    if (off > size || size - off < 1u) {
        return -1;
    }
    *out = data[off];
    return 0;
}

static int pe_read_u16(const uint8_t *data, size_t size, size_t off, uint16_t *out) {
    uint16_t v = 0;
    if (off > size || size - off < 2u) {
        return -1;
    }
    memcpy(&v, data + off, 2);
    *out = v;
    return 0;
}

static int pe_read_u32(const uint8_t *data, size_t size, size_t off, uint32_t *out) {
    uint32_t v = 0;
    if (off > size || size - off < 4u) {
        return -1;
    }
    memcpy(&v, data + off, 4);
    *out = v;
    return 0;
}

static int pe_read_u64(const uint8_t *data, size_t size, size_t off, uint64_t *out) {
    uint64_t v = 0;
    if (off > size || size - off < 8u) {
        return -1;
    }
    memcpy(&v, data + off, 8);
    *out = v;
    return 0;
}

static int o0_min_u32(int a, int b) { return a < b ? a : b; }

static int is_power_of_two_u32(uint32_t v) {
    return v != 0u && (v & (v - 1u)) == 0u;
}

static int o0_u64_add_overflow(uint64_t a, uint64_t b, uint64_t *out) {
    uint64_t sum = a + b;
    if (sum < a) {
        return -1;
    }
    *out = sum;
    return 0;
}

/* ---- DOS header ---- */

static int pe_dos_header(const uint8_t *data, size_t size, uint32_t *lfanew_out) {
    uint16_t magic = 0;
    uint32_t lfanew = 0;
    if (pe_read_u16(data, size, 0, &magic) != 0 || magic != O0_PE_DOS_MAGIC) {
        return O0_PE_ERR_BAD_MZ;
    }
    if (pe_read_u32(data, size, O0_PE_DOS_LFANEW_OFFSET, &lfanew) != 0) {
        return O0_PE_ERR_LFANEW;
    }
    *lfanew_out = lfanew;
    return O0_PE_OK;
}

/* ---- COFF file header ---- */

struct o0_pe_coff {
    uint16_t machine;
    uint16_t number_of_sections;
    uint16_t size_of_optional_header;
};

static int pe_coff_header(const uint8_t *data, size_t size, uint32_t off,
                          struct o0_pe_coff *out) {
    uint16_t machine = 0, number_of_sections = 0, size_of_optional_header = 0;
    uint32_t cursor = off;
    if (pe_read_u16(data, size, cursor, &machine) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 2;
    /* skip TimeDateStamp(4), PointerToSymbolTable(4), NumberOfSymbols(4) */
    cursor += 12;
    if (pe_read_u16(data, size, cursor, &number_of_sections) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 2;
    if (pe_read_u16(data, size, cursor, &size_of_optional_header) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    out->machine = machine;
    out->number_of_sections = number_of_sections;
    out->size_of_optional_header = size_of_optional_header;
    return O0_PE_OK;
}

/* ---- optional header (PE32+) ---- */

struct o0_pe_optional {
    uint16_t magic;
    uint32_t address_of_entry_point;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint16_t subsystem;
    uint32_t number_of_rva_and_sizes;
};

static int pe_optional_header(const uint8_t *data, size_t size, uint32_t off,
                              uint16_t size_of_optional_header,
                              struct o0_pe_optional *out) {
    uint16_t magic = 0;
    uint32_t cursor = off;
    uint32_t address_of_entry_point = 0;
    uint64_t image_base = 0;
    uint32_t section_alignment = 0, file_alignment = 0;
    uint32_t size_of_image = 0, size_of_headers = 0;
    uint16_t subsystem = 0;
    uint32_t number_of_rva_and_sizes = 0;

    if (size_of_optional_header < O0_PE_OPTIONAL_HEADER_STD_SIZE) {
        return O0_PE_ERR_TRUNCATED;
    }
    if (pe_read_u16(data, size, cursor, &magic) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    if (magic != O0_PE_OPTIONAL_MAGIC_PE32PLUS) {
        return O0_PE_ERR_BAD_OPTIONAL_MAGIC;
    }
    cursor += 2;
    /* skip Major/MinorLinkerVersion, SizeOfCode, SizeOfInitializedData,
     * SizeOfUninitializedData (4 + 4 + 4 + 4 = 16 bytes) -> AddressOfEntryPoint */
    cursor += 16;
    if (pe_read_u32(data, size, cursor, &address_of_entry_point) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 4;
    /* skip BaseOfCode (4 bytes); no BaseOfData in PE32+ */
    cursor += 4;
    if (pe_read_u64(data, size, cursor, &image_base) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 8;
    if (pe_read_u32(data, size, cursor, &section_alignment) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 4;
    if (pe_read_u32(data, size, cursor, &file_alignment) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 4;
    /* skip OS/image/subsystem versions, Win32VersionValue */
    cursor += 16;
    if (pe_read_u32(data, size, cursor, &size_of_image) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 4;
    if (pe_read_u32(data, size, cursor, &size_of_headers) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 4;
    /* skip CheckSum(4) */
    cursor += 4;
    if (pe_read_u16(data, size, cursor, &subsystem) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 2;
    /* skip DllCharacteristics(2), stack/heap reserves/commits(32), LoaderFlags(4) */
    cursor += 40;
    if (pe_read_u32(data, size, cursor, &number_of_rva_and_sizes) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }

    out->magic = magic;
    out->address_of_entry_point = address_of_entry_point;
    out->image_base = image_base;
    out->section_alignment = section_alignment;
    out->file_alignment = file_alignment;
    out->size_of_image = size_of_image;
    out->size_of_headers = size_of_headers;
    out->subsystem = subsystem;
    out->number_of_rva_and_sizes = number_of_rva_and_sizes;
    return O0_PE_OK;
}

/* ---- section table ---- */

static int pe_section(const uint8_t *data, size_t size, uint32_t off,
                      struct o0_pe_section *out) {
    uint32_t cursor = off;
    if (cursor + O0_PE_SECTION_HEADER_SIZE > size) {
        return O0_PE_ERR_TRUNCATED;
    }
    memcpy(out->name, data + cursor, 8);
    cursor += 8;
    uint32_t virtual_size = 0, size_of_raw_data = 0, pointer_to_raw_data = 0;
    uint64_t virtual_address = 0;
    uint32_t characteristics = 0;
    if (pe_read_u32(data, size, cursor, &virtual_size) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 4;
    if (pe_read_u64(data, size, cursor, &virtual_address) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 8;
    if (pe_read_u32(data, size, cursor, &size_of_raw_data) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 4;
    if (pe_read_u32(data, size, cursor, &pointer_to_raw_data) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    cursor += 4;
    /* skip PointerToRelocations(4), PointerToLinenumbers(4),
     * NumberOfRelocations(2), NumberOfLinenumbers(2) */
    cursor += 12;
    if (pe_read_u32(data, size, cursor, &characteristics) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    out->virtual_size = virtual_size;
    out->virtual_address = virtual_address;
    out->size_of_raw_data = size_of_raw_data;
    out->pointer_to_raw_data = pointer_to_raw_data;
    out->characteristics = characteristics;
    out->load_address = 0;
    return O0_PE_OK;
}

/* ---- data directory ---- */

static int pe_directory(const uint8_t *data, size_t size, uint32_t off,
                        struct o0_pe_directory *out) {
    uint32_t va = 0, sz = 0;
    if (pe_read_u32(data, size, off, &va) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    if (pe_read_u32(data, size, off + 4, &sz) != 0) {
        return O0_PE_ERR_TRUNCATED;
    }
    out->virtual_address = va;
    out->size = sz;
    return O0_PE_OK;
}

/* ---- top-level parse and validation ---- */

o0_status o0_pe_parse(const void *data, size_t size, struct o0_pe_image *out) {
    const uint8_t *image = (const uint8_t *)data;
    struct o0_pe_image img;
    struct o0_pe_coff coff;
    struct o0_pe_optional opt;
    uint32_t lfanew = 0, coff_off = 0, opt_off = 0;
    uint32_t sec_off = 0, dir_off = 0;
    uint32_t i;

    if (out == NULL || data == NULL) {
        return O0_EINVAL;
    }
    memset(&img, 0, sizeof(img));
    memset(&coff, 0, sizeof(coff));
    memset(&opt, 0, sizeof(opt));

    /* 1. DOS header and 'MZ'. */
    if (size < O0_PE_DOS_HEADER_SIZE) {
        return O0_PE_ERR_TRUNCATED;
    }
    if (pe_dos_header(image, size, &lfanew) != O0_PE_OK) {
        return O0_PE_ERR_BAD_MZ;
    }
    /* lfanew must point at a complete NT header signature + COFF header. */
    if (lfanew > size || size - lfanew < O0_PE_NT_SIGNATURE_SIZE + O0_PE_COFF_HEADER_SIZE) {
        return O0_PE_ERR_LFANEW;
    }

    /* 2. NT signature 'PE\0\0'. */
    {
        uint32_t sig = 0;
        if (pe_read_u32(image, size, lfanew, &sig) != 0 || sig != O0_PE_SIGNATURE) {
            return O0_PE_ERR_BAD_SIGNATURE;
        }
    }
    coff_off = lfanew + O0_PE_NT_SIGNATURE_SIZE;

    /* 3. COFF file header: machine x86-64, bounded section count. */
    if (pe_coff_header(image, size, coff_off, &coff) != O0_PE_OK) {
        return O0_PE_ERR_TRUNCATED;
    }
    if (coff.machine != O0_PE_MACHINE_AMD64) {
        return O0_PE_ERR_BAD_MACHINE;
    }
    if (coff.number_of_sections == 0 || coff.number_of_sections > O0_PE_MAX_SECTIONS) {
        return O0_PE_ERR_TOO_MANY_SECTIONS;
    }
    opt_off = coff_off + O0_PE_COFF_HEADER_SIZE;
    if (opt_off > size || size - opt_off < coff.size_of_optional_header) {
        return O0_PE_ERR_TRUNCATED;
    }
    if (coff.size_of_optional_header == 0) {
        return O0_PE_ERR_TRUNCATED;
    }

    /* 4. Optional header PE32+. */
    if (pe_optional_header(image, size, opt_off, coff.size_of_optional_header, &opt) !=
        O0_PE_OK) {
        return O0_PE_ERR_TRUNCATED;
    }

    /* Alignment rules: both powers of two, section alignment >= file alignment,
     * file alignment within sane bounds. */
    if (!is_power_of_two_u32(opt.section_alignment) ||
        !is_power_of_two_u32(opt.file_alignment)) {
        return O0_PE_ERR_BAD_ALIGNMENT;
    }
    if (opt.section_alignment < opt.file_alignment) {
        return O0_PE_ERR_BAD_ALIGNMENT;
    }
    if (opt.file_alignment > O0_PE_MAX_ALIGN || opt.section_alignment > O0_PE_MAX_ALIGN) {
        return O0_PE_ERR_BAD_ALIGNMENT;
    }
    if (opt.size_of_headers > size) {
        return O0_PE_ERR_TRUNCATED;
    }
    if (opt.size_of_image == 0 || opt.size_of_image > O0_PE_MAX_IMAGE_BYTES) {
        return O0_PE_ERR_SECTION_RANGE;
    }

    /* 5. Section table. */
    sec_off = opt_off + coff.size_of_optional_header;
    img.number_of_sections = coff.number_of_sections;
    for (i = 0; i < coff.number_of_sections; ++i) {
        struct o0_pe_section *s = &img.sections[i];
        uint64_t sec_end_virt = 0, sec_end_raw = 0;
        if (pe_section(image, size, sec_off, s) != O0_PE_OK) {
            return O0_PE_ERR_TRUNCATED;
        }
        sec_off += O0_PE_SECTION_HEADER_SIZE;

        /* Virtual range must fit inside the image. */
        if (o0_u64_add_overflow(s->virtual_address, (uint64_t)s->virtual_size,
                                &sec_end_virt) != 0 ||
            sec_end_virt > (uint64_t)opt.size_of_image) {
            return O0_PE_ERR_SECTION_RANGE;
        }
        /* Raw data, when present, must fit inside the file and its pointer must
         * respect file alignment. */
        if (s->size_of_raw_data != 0u) {
            if (s->pointer_to_raw_data > size ||
                size - s->pointer_to_raw_data < s->size_of_raw_data) {
                return O0_PE_ERR_SECTION_RANGE;
            }
            if (s->pointer_to_raw_data % opt.file_alignment != 0u) {
                return O0_PE_ERR_BAD_ALIGNMENT;
            }
            if (o0_u64_add_overflow((uint64_t)s->pointer_to_raw_data,
                                    (uint64_t)s->size_of_raw_data, &sec_end_raw) != 0) {
                return O0_PE_ERR_SECTION_RANGE;
            }
        }

        /* W^X: no section may be both writable and executable. */
        if ((s->characteristics & O0_PE_SCN_MEM_WRITE) != 0u &&
            (s->characteristics & O0_PE_SCN_MEM_EXECUTE) != 0u) {
            return O0_PE_ERR_WX;
        }
    }

    /* 6. Sections must not overlap each other by virtual address range. */
    for (i = 0; i < coff.number_of_sections; ++i) {
        uint64_t a_start = img.sections[i].virtual_address;
        uint64_t a_end = a_start + (uint64_t)img.sections[i].virtual_size;
        uint32_t j;
        for (j = i + 1; j < coff.number_of_sections; ++j) {
            uint64_t b_start = img.sections[j].virtual_address;
            uint64_t b_end = b_start + (uint64_t)img.sections[j].virtual_size;
            if (a_start < b_end && b_start < a_end) {
                return O0_PE_ERR_SECTIONS_OVERLAP;
            }
        }
    }

    /* 7. Entry point must fall inside an executable section. */
    {
        int found = 0;
        for (i = 0; i < coff.number_of_sections; ++i) {
            const struct o0_pe_section *s = &img.sections[i];
            if (opt.address_of_entry_point >= s->virtual_address &&
                opt.address_of_entry_point < s->virtual_address +
                    (uint64_t)o0_min_u32(s->virtual_size, 0x7fffffffu)) {
                if ((s->characteristics & O0_PE_SCN_MEM_EXECUTE) != 0u) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            return O0_PE_ERR_BAD_ENTRY;
        }
    }

    /* 8. Data directories present in the optional header must be within the
     * image. Parse Import, Base Relocation and Exception directories. */
    dir_off = opt_off + O0_PE_OPTIONAL_HEADER_STD_SIZE + O0_PE_OPTIONAL_HEADER_WIN_SIZE;
    for (i = 0; i < opt.number_of_rva_and_sizes && i < O0_PE_DIR_COUNT; ++i) {
        struct o0_pe_directory dir;
        if (pe_directory(image, size, dir_off + (uint32_t)i * O0_PE_DATA_DIRECTORY_SIZE,
                         &dir) != O0_PE_OK) {
            return O0_PE_ERR_TRUNCATED;
        }
        if (dir.size != 0u &&
            (dir.virtual_address > (uint64_t)opt.size_of_image ||
             opt.size_of_image - dir.virtual_address < dir.size)) {
            return O0_PE_ERR_BAD_DIRECTORY;
        }
        switch (i) {
        case O0_PE_DIR_IMPORT:
            img.import_directory = dir;
            break;
        case O0_PE_DIR_BASERELOC:
            img.base_relocation_directory = dir;
            break;
        case O0_PE_DIR_EXCEPTION:
            img.exception_directory = dir;
            break;
        default:
            break;
        }
    }

    img.image_base = opt.image_base;
    img.entry_rva = opt.address_of_entry_point;
    img.section_alignment = opt.section_alignment;
    img.file_alignment = opt.file_alignment;
    img.size_of_image = opt.size_of_image;

    *out = img;
    return O0_OK;
}
