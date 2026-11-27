/*
 * Ozero kernel - MBR and GPT partition table discovery.
 *
 * Parses the partition table on top of the block layer. The block layer owns
 * device registration and synchronous read; this module owns table parsing,
 * header/CRC validation, overlap detection and out-of-range LBA rejection.
 *
 * Wave 4.4 scope. See docs/decisions/0016-mbr-gpt-partition-discovery.md.
 */
#ifndef O0_PARTITION_H
#define O0_PARTITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ozero/block.h>
#include <ozero/status.h>
#include <ozero/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define O0_PART_MAX_ENTRIES 128u
#define O0_PART_NAME_MAX 36u

/* MBR constants. */
#define O0_MBR_SIGNATURE_OFFSET 510u
#define O0_MBR_SIGNATURE 0xAA55u
#define O0_MBR_PART_OFFSET 446u
#define O0_MBR_PART_ENTRY_SIZE 16u
#define O0_MBR_PART_COUNT 4u
#define O0_MBR_SECTOR_SIZE 512u

/* MBR partition entry fields (byte offsets within the 16-byte entry). */
#define O0_MBR_BOOT_OFFSET 0u
#define O0_MBR_TYPE_OFFSET 4u
#define O0_MBR_LBA_OFFSET 8u
#define O0_MBR_SIZE_OFFSET 12u

/* GPT constants. */
#define O0_GPT_PRIMARY_HEADER_LBA 1u
#define O0_GPT_ENTRY_START_LBA 2u
#define O0_GPT_ENTRY_SIZE 128u
#define O0_GPT_HEADER_SIGNATURE 0x5452415020494645ull /* "EFI PART" */
#define O0_GPT_HEADER_REVISION 0x00010000ull
#define O0_GPT_HEADER_SIZE_MIN 92u
#define O0_GPT_ENTRY_ARRAY_LBA_OFS 2u /* partition entries start at LBA 2 */

/* GPT entry attribute bits. */
#define O0_GPT_ATTR_REQUIRED (1ull << 0)
#define O0_GPT_ATTR_NO_BLOCK_IO (1ull << 1)
#define O0_GPT_ATTR_LEGACY_BIOS (1ull << 2)

/* Partition type GUIDs (well-known values). */
#define O0_PART_TYPE_UNUSED \
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define O0_PART_TYPE_MBR \
    { 0x02, 0x4D, 0xE6, 0x00, 0xD3, 0xBE, 0x11, 0xCE, \
      0xB8, 0x7C, 0x29, 0x44, 0xFF, 0xD6, 0x1F, 0x9C }
#define O0_PART_TYPE_GPT \
    { 0xC1, 0x2A, 0x73, 0x28, 0xF8, 0x1F, 0x11, 0xD2, \
      0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B }

struct o0_part_guid {
    uint8_t b[16];
};

/* A single partition entry, in the device's logical block units. */
struct o0_partition {
    struct o0_part_guid type;
    struct o0_part_guid guid;       /* GPT unique partition GUID; zero for MBR */
    char name[O0_PART_NAME_MAX];    /* GPT partition name (UTF-16LE decoded); zero for MBR */
    uint64_t start_lba;
    uint64_t nr_sectors;
    uint64_t attributes;            /* GPT attributes; zero for MBR */
    uint8_t mbr_type;               /* MBR type byte; zero for GPT */
    bool bootable;                  /* MBR bootable flag; false for GPT */
    bool gpt;                       /* true when this entry came from a GPT table */
};

/* A discovered partition table: either MBR or GPT, never both. */
struct o0_partition_table {
    bool gpt;
    uint32_t nr_entries;
    struct o0_partition entries[O0_PART_MAX_ENTRIES];
};

/*
 * Discover and validate the partition table on `dev`.
 *
 * Reads LBA 0 to decide between MBR and GPT (protective MBR). For GPT, also
 * validates the primary header CRC, the partition entry array CRC, and checks
 * every entry for overlapping LBAs and out-of-range extents against the
 * device geometry.
 *
 * Returns O0_OK and fills `out` on success. Returns O0_ENOENT when no
 * recognizable table is present, O0_EINVAL when the table is malformed, and
 * O0_ERANGE when an entry extends beyond the device.
 */
O0_NODISCARD o0_status o0_partition_discover(const struct o0_block_dev *dev,
                                             struct o0_partition_table *out);

/*
 * Format a partition type GUID as a canonical lowercase UUID string
 * (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx). Returns the number of bytes written
 * including the NUL, or a negative value if `size` is too small.
 */
long o0_part_guid_to_string(const struct o0_part_guid *guid,
                            char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* O0_PARTITION_H */
