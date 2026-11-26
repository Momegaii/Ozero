/* Ozero kernel - MBR and GPT partition discovery host tests (Wave 4.4).
 *
 * Builds synthetic partition tables in RAM-backed block devices and checks that
 * o0_partition_discover parses and validates them correctly. Malformed and
 * truncated images are fuzzed to confirm the parser never accepts invalid input.
 *
 * Run it with `make test`.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/block.h"
#include "ozero/mm.h"
#include "ozero/partition.h"
#include "ozero/ramdisk.h"
#include "ozero/status.h"
#include "ozero/types.h"

/* Mock PMM backing for the RAM disk under the host test. */
#define MOCK_PAGES 256u /* 1 MiB of backing store */
static uint8_t mock_arena[MOCK_PAGES][4096];
static bool mock_used[MOCK_PAGES];

o0_paddr_t o0_pmm_alloc_pages(uint32_t order) {
    if (order != 0) {
        return 0;
    }
    for (uint32_t i = 0; i < MOCK_PAGES; ++i) {
        if (!mock_used[i]) {
            mock_used[i] = true;
            return (o0_paddr_t)(uintptr_t)&mock_arena[i][0];
        }
    }
    return 0;
}

void o0_pmm_free_pages(o0_paddr_t base, uint32_t order) {
    (void)order;
    for (uint32_t i = 0; i < MOCK_PAGES; ++i) {
        if ((o0_paddr_t)(uintptr_t)&mock_arena[i][0] == base) {
            mock_used[i] = false;
            return;
        }
    }
}

void *o0_phys_to_virt(o0_paddr_t phys) {
    return (void *)(uintptr_t)phys;
}

o0_paddr_t o0_virt_to_phys(const void *virt) {
    return (o0_paddr_t)(uintptr_t)virt;
}

#define TEST_DISK_BYTES (64u * 1024u) /* 64 KiB, 16 blocks of 4 KiB */

static int failures;
static int tests_run;

#define TEST(name)                                            \
    do {                                                      \
        ++tests_run;                                          \
        printf("  TEST: %s ... ", name);                      \
        fflush(stdout);                                       \
    } while (0)

#define PASS()                                                \
    do {                                                      \
        printf("PASS\n");                                     \
    } while (0)

#define FAIL(msg)                                             \
    do {                                                      \
        printf("FAIL: %s\n", msg);                            \
        ++failures;                                           \
    } while (0)

#define ASSERT(cond, msg)                                     \
    do {                                                      \
        if (!(cond)) {                                        \
            FAIL(msg);                                        \
            return;                                           \
        }                                                     \
    } while (0)

/* Write a 512-byte sector image into a block-aligned buffer and submit it to
 * the device at block 0. The ramdisk uses 4 KiB logical blocks. */
static void write_sector0(struct o0_block_dev *dev, const uint8_t *sector) {
    uint8_t block[4096];
    memset(block, 0, sizeof(block));
    memcpy(block, sector, 512);
    ASSERT(o0_block_write(dev, 0, block, sizeof(block)) == O0_OK,
           "write sector 0 failed");
}

static void write_gpt_at_block0(struct o0_block_dev *dev,
                                const uint8_t *mbr_sector,
                                const uint8_t *gpt_header,
                                const uint8_t *gpt_entries,
                                size_t entries_bytes) {
    uint8_t block[4096];
    memset(block, 0, sizeof(block));
    memcpy(block, mbr_sector, 512);            /* protective MBR at LBA 0 */
    memcpy(block + 512, gpt_header, 512);      /* GPT header at LBA 1 */
    memcpy(block + 1024, gpt_entries, entries_bytes); /* entries at LBA 2+ */
    ASSERT(o0_block_write(dev, 0, block, sizeof(block)) == O0_OK,
           "write GPT block 0 failed");
}

/* Build a valid MBR: two entries, bootable NTFS at LBA 2048 (2048 sectors),
 * FAT32 at LBA 4096 (4096 sectors), signature 0xAA55. */
static void build_valid_mbr(uint8_t *sector) {
    memset(sector, 0, 512);
    /* Entry 1: bootable, type 0x07, start=34, size=32 */
    sector[446] = 0x80;
    sector[450] = 0x07;
    sector[454] = 34; sector[455] = 0; sector[456] = 0; sector[457] = 0; /* start=34 */
    sector[458] = 32; sector[459] = 0; sector[460] = 0; sector[461] = 0; /* size=32 */
    /* Entry 2: not bootable, type 0x0B, start=66, size=32 */
    sector[462] = 0x00;
    sector[466] = 0x0B;
    sector[470] = 66; sector[471] = 0; sector[472] = 0; sector[473] = 0; /* start=66 */
    sector[474] = 32; sector[475] = 0; sector[476] = 0; sector[477] = 0; /* size=32 */
    /* Signature */
    sector[510] = 0x55;
    sector[511] = 0xAA;
}

static void test_mbr_valid_table(void) {
    TEST("mbr valid table parsed");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram0", TEST_DISK_BYTES, &dev) == O0_OK,
         "ramdisk create failed");

    uint8_t sector[512];
    build_valid_mbr(sector);
    write_sector0(dev, sector);

    struct o0_partition_table tbl;
    ASSERT(o0_partition_discover(dev, &tbl) == O0_OK, "discover failed");
    ASSERT(!tbl.gpt, "expected MBR, got GPT");
    ASSERT(tbl.nr_entries == 2, "expected 2 MBR entries");

    ASSERT(tbl.entries[0].bootable, "entry 1 not bootable");
    ASSERT(tbl.entries[0].mbr_type == 0x07, "entry 1 type wrong");
    ASSERT(tbl.entries[0].start_lba == 34u, "entry 1 start wrong");
    ASSERT(tbl.entries[0].nr_sectors == 32u, "entry 1 size wrong");
    ASSERT(!tbl.entries[0].gpt, "entry 1 flagged GPT");

    ASSERT(!tbl.entries[1].bootable, "entry 2 unexpectedly bootable");
    ASSERT(tbl.entries[1].mbr_type == 0x0B, "entry 2 type wrong");
    ASSERT(tbl.entries[1].start_lba == 66u, "entry 2 start wrong");
    ASSERT(tbl.entries[1].nr_sectors == 32u, "entry 2 size wrong");

    o0_ramdisk_destroy(dev);
    PASS();
}

static void test_mbr_no_signature(void) {
    TEST("mbr no signature returns ENOENT");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram0", TEST_DISK_BYTES, &dev) == O0_OK,
         "ramdisk create failed");

    uint8_t sector[512];
    memset(sector, 0, sizeof(sector));
    write_sector0(dev, sector);

    struct o0_partition_table tbl;
    o0_status st = o0_partition_discover(dev, &tbl);
    ASSERT(st == O0_ENOENT, "expected ENOENT for no signature");

    o0_ramdisk_destroy(dev);
    PASS();
}

static void test_mbr_overlap(void) {
    TEST("mbr overlapping entries rejected");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram0", TEST_DISK_BYTES, &dev) == O0_OK,
         "ramdisk create failed");

    uint8_t sector[512];
    build_valid_mbr(sector);
    /* Make entry 2 overlap entry 1: entry 2 starts at 40 (inside entry 1's 34-65). */
    sector[470] = 40; sector[471] = 0; sector[472] = 0; sector[473] = 0;
    sector[510] = 0x55;
    sector[511] = 0xAA;
    write_sector0(dev, sector);

    struct o0_partition_table tbl;
    o0_status st = o0_partition_discover(dev, &tbl);
    ASSERT(st == O0_EINVAL, "expected EINVAL for overlapping MBR entries");

    o0_ramdisk_destroy(dev);
    PASS();
}

static void test_mbr_out_of_range(void) {
    TEST("mbr out-of-range entry rejected");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram0", TEST_DISK_BYTES, &dev) == O0_OK,
         "ramdisk create failed");

    /* 64 KiB / 4 KiB block = 16 blocks; nr_sectors = 16 (4 KiB blocks). */
    uint8_t sector[512];
    build_valid_mbr(sector);
    /* Push entry 1 beyond the disk: start = 100000. */
    sector[454] = 0x80; sector[455] = 0x31; sector[456] = 0x01; sector[457] = 0x00;
    sector[510] = 0x55;
    sector[511] = 0xAA;
    write_sector0(dev, sector);

    struct o0_partition_table tbl;
    o0_status st = o0_partition_discover(dev, &tbl);
    ASSERT(st == O0_ERANGE, "expected ERANGE for out-of-range MBR entry");

    o0_ramdisk_destroy(dev);
    PASS();
}

/* Build a minimal valid GPT: protective MBR, GPT header at LBA 1, one entry
 * at LBA 2 (type GPT, start=64, size=1024 sectors). */
static void build_valid_gpt(uint8_t *mbr_sector,
                            uint8_t *gpt_header,
                            uint8_t *gpt_entry,
                            uint64_t disk_sectors) {
    /* Protective MBR */
    memset(mbr_sector, 0, 512);
    mbr_sector[446] = 0x00; /* not bootable */
    mbr_sector[450] = 0xEE; /* GPT protective type */
    mbr_sector[454] = 0x01; mbr_sector[455] = 0x00; mbr_sector[456] = 0x00; mbr_sector[457] = 0x00; /* start=1 */
    mbr_sector[458] = 0xFE; mbr_sector[459] = 0xFF; mbr_sector[460] = 0xFF; mbr_sector[461] = 0xFF; /* size */
    mbr_sector[510] = 0x55;
    mbr_sector[511] = 0xAA;

    /* GPT header */
    memset(gpt_header, 0, 512);
    memcpy(gpt_header, "EFI PART", 8);
    uint32_t rev = 0x00010000;
    memcpy(gpt_header + 8, &rev, 4);
    uint32_t hdr_size = 92;
    memcpy(gpt_header + 12, &hdr_size, 4);
    uint64_t cur_lba = 1;
    memcpy(gpt_header + 24, &cur_lba, 8);
    uint64_t bak_lba = disk_sectors - 1;
    memcpy(gpt_header + 32, &bak_lba, 8);
    uint64_t first_usable = 34;
    memcpy(gpt_header + 40, &first_usable, 8);
    uint64_t last_usable = disk_sectors - 34;
    memcpy(gpt_header + 48, &last_usable, 8);
    uint8_t disk_guid[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                             0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00};
    memcpy(gpt_header + 56, disk_guid, 16);
    uint64_t entry_lba = 2;
    memcpy(gpt_header + 72, &entry_lba, 8);
    uint32_t num_entries = 1;
    memcpy(gpt_header + 80, &num_entries, 4);
    uint32_t entry_size = 128;
    memcpy(gpt_header + 84, &entry_size, 4);

    /* GPT entry: type GPT GUID, start=34, size=32 */
    memset(gpt_entry, 0, 128);
    uint8_t type_guid[16] = O0_PART_TYPE_GPT;
    memcpy(gpt_entry, type_guid, 16);
    uint8_t part_guid[16] = {0x01};
    memcpy(gpt_entry + 16, part_guid, 16);
    uint64_t first_lba = 34;
    memcpy(gpt_entry + 32, &first_lba, 8);
    uint64_t last_lba = 34 + 32 - 1;
    memcpy(gpt_entry + 40, &last_lba, 8);
    /* name: "testpart" in UTF-16LE */
    const char *name = "testpart";
    for (size_t i = 0; i < 8; ++i) {
        gpt_entry[56 + i * 2] = (uint8_t)name[i];
        gpt_entry[56 + i * 2 + 1] = 0;
    }
    /* Entry array CRC over 128 bytes (one entry), stored at header bytes 88-91
     * BEFORE the header CRC is computed so the header CRC covers it. */
    uint32_t ecrc = 0xFFFFFFFFu;
    for (size_t i = 0; i < 128; ++i) {
        ecrc ^= gpt_entry[i];
        for (int b = 0; b < 8; ++b) {
            ecrc = (ecrc & 1u) ? (0xEDB88320u ^ (ecrc >> 1)) : (ecrc >> 1);
        }
    }
    ecrc = ~ecrc;
    memcpy(gpt_header + 88, &ecrc, 4);

    /* Header CRC over first 92 bytes with CRC field zeroed. */
    uint8_t hdr_copy[92];
    memcpy(hdr_copy, gpt_header, 92);
    memset(hdr_copy + 16, 0, 4);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < 92; ++i) {
        crc ^= hdr_copy[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
    }
    crc = ~crc;
    memcpy(gpt_header + 16, &crc, 4);
}

static void test_gpt_valid_table(void) {
    TEST("gpt valid table parsed");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram0", TEST_DISK_BYTES, &dev) == O0_OK,
         "ramdisk create failed");

    uint64_t disk_sectors = TEST_DISK_BYTES / 512; /* 512 GPT sectors */
    uint8_t mbr_sector[512], gpt_header[512], gpt_entry[128];
    build_valid_gpt(mbr_sector, gpt_header, gpt_entry, disk_sectors);
    write_gpt_at_block0(dev, mbr_sector, gpt_header, gpt_entry, 128);

    struct o0_partition_table tbl;
    ASSERT(o0_partition_discover(dev, &tbl) == O0_OK, "discover failed");
    ASSERT(tbl.gpt, "expected GPT");
    ASSERT(tbl.nr_entries == 1, "expected 1 GPT entry");

    ASSERT(tbl.entries[0].gpt, "entry not flagged GPT");
    uint8_t expected_type[16] = {0xC1,0x2A,0x73,0x28,0xF8,0x1F,0x11,0xD2,
                                 0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
    ASSERT(memcmp(tbl.entries[0].type.b, expected_type, 16) == 0,
         "entry type GUID wrong");    ASSERT(tbl.entries[0].start_lba == 34u, "entry start wrong");
    ASSERT(tbl.entries[0].nr_sectors == 32u, "entry size wrong");
    ASSERT(memcmp(tbl.entries[0].name, "testpart", 8) == 0, "entry name wrong");

    char guid_str[64];
    long n = o0_part_guid_to_string(&tbl.entries[0].guid, guid_str, sizeof(guid_str));
    ASSERT(n > 0, "guid to string failed");
    ASSERT(memcmp(guid_str, "01000000-0000-0000-0000-000000000000", 36) == 0 &&
               guid_str[36] == '\0',
         "guid string wrong");

    o0_ramdisk_destroy(dev);
    PASS();
}

static void test_gpt_bad_header_crc(void) {
    TEST("gpt bad header CRC rejected");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram0", TEST_DISK_BYTES, &dev) == O0_OK,
         "ramdisk create failed");

    uint64_t disk_sectors = TEST_DISK_BYTES / 512;
    uint8_t mbr_sector[512], gpt_header[512], gpt_entry[128];
    build_valid_gpt(mbr_sector, gpt_header, gpt_entry, disk_sectors);
    /* Corrupt the header CRC. */
    gpt_header[16] ^= 0xFF;
    write_gpt_at_block0(dev, mbr_sector, gpt_header, gpt_entry, 128);

    struct o0_partition_table tbl;
    o0_status st = o0_partition_discover(dev, &tbl);
    ASSERT(st == O0_EINVAL, "expected EINVAL for bad GPT header CRC");

    o0_ramdisk_destroy(dev);
    PASS();
}

static void test_gpt_bad_entry_crc(void) {
    TEST("gpt bad entry array CRC rejected");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram0", TEST_DISK_BYTES, &dev) == O0_OK,
         "ramdisk create failed");

    uint64_t disk_sectors = TEST_DISK_BYTES / 512;
    uint8_t mbr_sector[512], gpt_header[512], gpt_entry[128];
    build_valid_gpt(mbr_sector, gpt_header, gpt_entry, disk_sectors);
    /* Corrupt an entry byte, which invalidates the entry array CRC. */
    gpt_entry[0] ^= 0xFF;
    write_gpt_at_block0(dev, mbr_sector, gpt_header, gpt_entry, 128);

    struct o0_partition_table tbl;
    o0_status st = o0_partition_discover(dev, &tbl);
    ASSERT(st == O0_EINVAL, "expected EINVAL for bad GPT entry CRC");

    o0_ramdisk_destroy(dev);
    PASS();
}

static void test_gpt_overlap(void) {
    TEST("gpt overlapping entries rejected");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram0", TEST_DISK_BYTES, &dev) == O0_OK,
         "ramdisk create failed");

    uint64_t disk_sectors = TEST_DISK_BYTES / 512;
    uint8_t mbr_sector[512], gpt_header[512];
    uint8_t gpt_entries[256];
    build_valid_gpt(mbr_sector, gpt_header, gpt_entries, disk_sectors);
    /* Second entry overlapping the first: start=40 (inside first entry 34-65). */
    memset(gpt_entries + 128, 0, 128);
    uint8_t type_guid[16] = O0_PART_TYPE_GPT;
    memcpy(gpt_entries + 128, type_guid, 16);
    uint64_t first_lba = 40;
    memcpy(gpt_entries + 128 + 32, &first_lba, 8);
    uint64_t last_lba = 40 + 32 - 1;
    memcpy(gpt_entries + 128 + 40, &last_lba, 8);
    /* Recompute entry array CRC over 256 bytes. */
    uint32_t ecrc = 0xFFFFFFFFu;
    for (size_t i = 0; i < 256; ++i) {
        ecrc ^= gpt_entries[i];
        for (int b = 0; b < 8; ++b) {
            ecrc = (ecrc & 1u) ? (0xEDB88320u ^ (ecrc >> 1)) : (ecrc >> 1);
        }
    }
    ecrc = ~ecrc;
    memcpy(gpt_header + 88, &ecrc, 4);
    /* Update num_entries to 2. */
    uint32_t num_entries = 2;
    memcpy(gpt_header + 80, &num_entries, 4);
    write_gpt_at_block0(dev, mbr_sector, gpt_header, gpt_entries, 256);

    struct o0_partition_table tbl;
    o0_status st = o0_partition_discover(dev, &tbl);
    ASSERT(st == O0_EINVAL, "expected EINVAL for overlapping GPT entries");

    o0_ramdisk_destroy(dev);
    PASS();
}

static void test_gpt_out_of_range(void) {
    TEST("gpt out-of-range entry rejected");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram0", TEST_DISK_BYTES, &dev) == O0_OK,
         "ramdisk create failed");

    uint64_t disk_sectors = TEST_DISK_BYTES / 512;
    uint8_t mbr_sector[512], gpt_header[512], gpt_entry[128];
    build_valid_gpt(mbr_sector, gpt_header, gpt_entry, disk_sectors);
    /* Entry extends beyond disk: last_lba = disk_sectors (exclusive bound). */
    uint64_t last_lba = disk_sectors;
    memcpy(gpt_entry + 40, &last_lba, 8);
    /* Recompute entry array CRC. */
    uint32_t ecrc = 0xFFFFFFFFu;
    for (size_t i = 0; i < 128; ++i) {
        ecrc ^= gpt_entry[i];
        for (int b = 0; b < 8; ++b) {
            ecrc = (ecrc & 1u) ? (0xEDB88320u ^ (ecrc >> 1)) : (ecrc >> 1);
        }
    }
    ecrc = ~ecrc;
    memcpy(gpt_header + 88, &ecrc, 4);
    /* Recompute header CRC over bytes 0-91, which now includes the updated
     * entry array CRC at bytes 88-91. */
    {
        uint8_t hdr_copy[92];
        memcpy(hdr_copy, gpt_header, 92);
        memset(hdr_copy + 16, 0, 4);
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < 92; ++i) {
            crc ^= hdr_copy[i];
            for (int b = 0; b < 8; ++b) {
                crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
            }
        }
        crc = ~crc;
        memcpy(gpt_header + 16, &crc, 4);
    }
    write_gpt_at_block0(dev, mbr_sector, gpt_header, gpt_entry, 128);

    struct o0_partition_table tbl;
    o0_status st = o0_partition_discover(dev, &tbl);
    ASSERT(st == O0_ERANGE, "expected ERANGE for out-of-range GPT entry");

    o0_ramdisk_destroy(dev);
    PASS();
}

/* Fuzz: flip bytes in a valid MBR image and confirm the parser never crashes
 * and either accepts a still-valid table or rejects with an error. */
static void test_mbr_fuzz(void) {
    TEST("mbr fuzz malformed images");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram_fuzz_mbr", TEST_DISK_BYTES, &dev) == O0_OK,
         "ramdisk create failed");

    uint8_t base[512];
    build_valid_mbr(base);

    for (uint32_t flip = 0; flip < 512; ++flip) {
        uint8_t sector[512];
        memcpy(sector, base, sizeof(sector));
        sector[flip] ^= 0xFF;

        write_sector0(dev, sector);
        struct o0_partition_table tbl;
        o0_status st = o0_partition_discover(dev, &tbl);
        /* Must be OK, ENOENT, EINVAL, or ERANGE - never a crash. */
        ASSERT(st == O0_OK || st == O0_ENOENT || st == O0_EINVAL ||
                   st == O0_ERANGE,
               "fuzz returned unexpected status");
    }

    o0_ramdisk_destroy(dev);
    PASS();
}

/* Fuzz: flip bytes in a valid GPT image. */
static void test_gpt_fuzz(void) {
    TEST("gpt fuzz malformed images");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram_fuzz_gpt", TEST_DISK_BYTES, &dev) == O0_OK,
         "ramdisk create failed");

    uint64_t disk_sectors = TEST_DISK_BYTES / 512;
    uint8_t mbr_sector[512], gpt_header[512], gpt_entry[128];
    build_valid_gpt(mbr_sector, gpt_header, gpt_entry, disk_sectors);

    uint8_t block[4096];
    memset(block, 0, sizeof(block));
    memcpy(block, mbr_sector, 512);
    memcpy(block + 512, gpt_header, 512);
    memcpy(block + 1024, gpt_entry, 128);

    for (uint32_t flip = 0; flip < 4096; ++flip) {
        uint8_t test_block[4096];
        memcpy(test_block, block, sizeof(test_block));
        test_block[flip] ^= 0xFF;

        ASSERT(o0_block_write(dev, 0, test_block, sizeof(test_block)) ==
                   O0_OK,
               "fuzz write failed");
        struct o0_partition_table tbl;
        o0_status st = o0_partition_discover(dev, &tbl);
        ASSERT(st == O0_OK || st == O0_ENOENT || st == O0_EINVAL ||
                   st == O0_ERANGE,
               "fuzz returned unexpected status");
    }

    o0_ramdisk_destroy(dev);
    PASS();
}

static void test_guid_to_string(void) {
    TEST("guid to string formatting");
    struct o0_part_guid g;
    memset(g.b, 0, sizeof(g));
    g.b[0] = 0x12; g.b[1] = 0x34; g.b[2] = 0x56; g.b[3] = 0x78;
    g.b[4] = 0x9A; g.b[5] = 0xBC;
    g.b[6] = 0xDE; g.b[7] = 0xF0;
    g.b[8] = 0x11; g.b[9] = 0x22;
    g.b[10] = 0x33; g.b[11] = 0x44; g.b[12] = 0x55; g.b[13] = 0x66;
    g.b[14] = 0x77; g.b[15] = 0x88;

    char out[64];
    long n = o0_part_guid_to_string(&g, out, sizeof(out));
    ASSERT(n == 37, "guid string length wrong");
    ASSERT(memcmp(out, "12345678-9abc-def0-1122-334455667788", 36) == 0 &&
               out[36] == '\0',
         "guid string wrong");

    /* Buffer too small. */
    char small[8];
    n = o0_part_guid_to_string(&g, small, sizeof(small));
    ASSERT(n < 0, "expected failure for small buffer");

    struct o0_part_guid zero;
    memset(&zero, 0, sizeof(zero));
    n = o0_part_guid_to_string(&zero, out, sizeof(out));
    ASSERT(n == 37, "zero guid string length wrong");
    ASSERT(memcmp(out, "00000000-0000-0000-0000-000000000000", 36) == 0 &&
               out[36] == '\0',
         "zero guid string wrong");

    PASS();
}

int main(void) {
    printf("Ozero MBR/GPT partition discovery unit tests\n\n");

    test_guid_to_string();
    test_mbr_valid_table();
    test_mbr_no_signature();
    test_mbr_overlap();
    test_mbr_out_of_range();
    test_gpt_valid_table();
    test_gpt_bad_header_crc();
    test_gpt_bad_entry_crc();
    test_gpt_overlap();
    test_gpt_out_of_range();
    test_mbr_fuzz();
    test_gpt_fuzz();

    printf("\n%d test(s) run, %d failure(s)\n", tests_run, failures);
    return failures == 0 ? 0 : 1;
}
