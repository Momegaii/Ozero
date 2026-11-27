/* Ozero kernel - OzeroFS crash recovery unit test (host build).
 *
 * Tests the redo journal added in Wave 4.5: ordered data writes, transaction
 * commits, mount-time journal recovery, and the consistency checker.
 *
 * Every test formats its own RAM disk, so failures cannot leak between tests.
 * Crash injection uses o0_ramdisk_inject_write_fault to fail a single block
 * write at a chosen LBA, simulating a power loss at that point. The mount is
 * then torn down and remounted, which runs the journal recovery pass.
 *
 * kernel/src/block.c, kernel/src/ramdisk.c and kernel/src/o0fs.c are included
 * so the filesystem's mount pool and on-disk structures are visible. The PMM
 * and the direct map are mocked with host memory, as in the other host tests.
 *
 * Run it with `make test`.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/block.h"
#include "ozero/fs.h"
#include "ozero/mm.h"
#include "ozero/ramdisk.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define MOCK_PAGES 512u /* 2 MiB: one 512 KiB disk plus mount buffers */

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

#include "../kernel/src/block.c"
#include "../kernel/src/ramdisk.c"
#include "../kernel/src/o0fs.c"

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

#define DISK_BYTES (512u * 1024u)
#define DISK_BLOCKS (DISK_BYTES / O0FS_BLOCK_SIZE)

struct fixture {
    struct o0_block_dev *dev;
    struct o0fs *fs;
};

static bool fixture_open(struct fixture *fixture) {
    fixture->dev = NULL;
    fixture->fs = NULL;
    if (o0_ramdisk_create("ram0", DISK_BYTES, &fixture->dev) != O0_OK) {
        return false;
    }
    if (o0fs_format(fixture->dev) != O0_OK) {
        (void)o0_ramdisk_destroy(fixture->dev);
        fixture->dev = NULL;
        return false;
    }
    if (o0fs_mount(fixture->dev, &fixture->fs) != O0_OK) {
        (void)o0_ramdisk_destroy(fixture->dev);
        fixture->dev = NULL;
        fixture->fs = NULL;
        return false;
    }
    return true;
}

static void fixture_close(struct fixture *fixture) {
    if (fixture->fs != NULL) {
        (void)o0fs_unmount(fixture->fs);
        fixture->fs = NULL;
    }
    if (fixture->dev != NULL) {
        (void)o0_ramdisk_destroy(fixture->dev);
        fixture->dev = NULL;
    }
}

/* A failed assertion returns from a test before its fixture cleanup. Rather
 * than threading cleanup through every assertion, each test starts from a
 * known world: any mount or device left behind is torn down and the mock page
 * accounting is cleared. */
static void reset_world(void) {
    for (size_t i = 0; i < O0FS_MAX_MOUNTS; ++i) {
        if (mounts[i].mounted) {
            (void)o0fs_unmount(&mounts[i]);
        }
    }
    for (size_t i = 0; i < O0_RAMDISK_MAX_DEVICES; ++i) {
        if (ramdisks[i].in_use) {
            (void)o0_ramdisk_destroy(&ramdisks[i].dev);
        }
    }
    for (size_t i = 0; i < MOCK_PAGES; ++i) {
        mock_used[i] = false;
    }
    block_device_total = 0;
}

/* Disable any active fault injection on the device. */
static void fault_disable(struct o0_block_dev *dev) {
    (void)o0_ramdisk_inject_fault(dev, O0_BLOCK_OK, 0, 0);
}

/* Build a deterministic buffer of the given length. */
static void fill_pattern(uint8_t *buf, size_t len, uint8_t base) {
    for (size_t i = 0; i < len; ++i) {
        buf[i] = (uint8_t)((base + i) & 0xffu);
    }
}

static void test_journal_normal_write_and_remount(void) {
    TEST("normal journaled write survives remount");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    static uint8_t old_data[64];
    fill_pattern(old_data, sizeof(old_data), 0x10u);
    size_t written = 0;
    ASSERT(o0fs_create_file(fixture.fs, "/a") == O0_OK, "create failed");
    ASSERT(o0fs_write(fixture.fs, "/a", 0, old_data, sizeof(old_data),
                      &written) == O0_OK,
           "initial write failed");
    ASSERT(written == sizeof(old_data), "short write");

    /* Remount: journal recovery runs on the clean volume. */
    ASSERT(o0fs_unmount(fixture.fs) == O0_OK, "unmount failed");
    fixture.fs = NULL;
    ASSERT(o0fs_mount(fixture.dev, &fixture.fs) == O0_OK, "remount failed");

    /* Checker must pass on the recovered volume. */
    ASSERT(o0fs_check(fixture.dev) == O0_OK, "checker failed after remount");

    char buffer[sizeof(old_data)];
    memset(buffer, 0, sizeof(buffer));
    size_t got = 0;
    ASSERT(o0fs_read(fixture.fs, "/a", 0, buffer, sizeof(buffer), &got) ==
               O0_OK,
           "read after remount failed");
    ASSERT(got == sizeof(old_data), "wrong read length after remount");
    ASSERT(memcmp(buffer, old_data, got) == 0, "data differs after remount");

    fixture_close(&fixture);
    PASS();
}

static void test_journal_crash_before_commit(void) {
    TEST("crash before commit header: uncommitted transaction discarded");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    static uint8_t old_data[64];
    fill_pattern(old_data, sizeof(old_data), 0x10u);
    size_t written = 0;
    ASSERT(o0fs_create_file(fixture.fs, "/a") == O0_OK, "create failed");
    ASSERT(o0fs_write(fixture.fs, "/a", 0, old_data, sizeof(old_data),
                      &written) == O0_OK,
           "initial write failed");

    /* Inject a fault at the first journal record block. The transaction's
     * journal records cannot be written, so the commit never happens. */
    uint32_t journal_record_lba =
        fixture.fs->super.journal_block + 1u;
    ASSERT(o0_ramdisk_inject_write_fault(fixture.dev, O0_BLOCK_ERR_IO,
                                         journal_record_lba, 1) == O0_OK,
           "journal record fault injection failed");

    static uint8_t new_data[5000];
    fill_pattern(new_data, sizeof(new_data), 0x20u);
    written = 0;
    o0_status st = o0fs_write(fixture.fs, "/a", 0, new_data,
                              sizeof(new_data), &written);
    ASSERT(st != O0_OK, "write should have failed with journal fault");
    fault_disable(fixture.dev);

    /* Remount: recovery finds no valid new commit header, discards the
     * uncommitted transaction. The volume must still be consistent. */
    ASSERT(o0fs_unmount(fixture.fs) == O0_OK, "unmount failed");
    fixture.fs = NULL;
    ASSERT(o0fs_mount(fixture.dev, &fixture.fs) == O0_OK, "remount failed");
    ASSERT(o0fs_check(fixture.dev) == O0_OK,
           "checker failed after pre-commit crash");

    /* The file must still be readable (data blocks may have been overwritten
     * by ordered data writes, but metadata reflects the last committed state). */
    char buffer[sizeof(old_data)];
    memset(buffer, 0, sizeof(buffer));
    size_t got = 0;
    ASSERT(o0fs_read(fixture.fs, "/a", 0, buffer, sizeof(buffer), &got) ==
               O0_OK,
           "read after pre-commit crash failed");
    ASSERT(got > 0, "read returned no data after pre-commit crash");

    fixture_close(&fixture);
    PASS();
}

static void test_journal_crash_commit_header(void) {
    TEST("crash at commit header: transaction not committed");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    static uint8_t old_data[64];
    fill_pattern(old_data, sizeof(old_data), 0x10u);
    size_t written = 0;
    ASSERT(o0fs_create_file(fixture.fs, "/a") == O0_OK, "create failed");
    ASSERT(o0fs_write(fixture.fs, "/a", 0, old_data, sizeof(old_data),
                      &written) == O0_OK,
           "initial write failed");

    /* Inject a fault at the journal header block (commit header LBA). Journal
     * records are written, then the commit header write fails. */
    uint32_t commit_header_lba = fixture.fs->super.journal_block;
    ASSERT(o0_ramdisk_inject_write_fault(fixture.dev, O0_BLOCK_ERR_IO,
                                         commit_header_lba, 1) == O0_OK,
           "commit header fault injection failed");

    static uint8_t new_data[5000];
    fill_pattern(new_data, sizeof(new_data), 0x20u);
    written = 0;
    o0_status st = o0fs_write(fixture.fs, "/a", 0, new_data,
                              sizeof(new_data), &written);
    ASSERT(st != O0_OK, "write should have failed with commit-header fault");
    fault_disable(fixture.dev);

    /* Remount: no valid commit header for the new sequence, transaction
     * discarded. Volume must be consistent. */
    ASSERT(o0fs_unmount(fixture.fs) == O0_OK, "unmount failed");
    fixture.fs = NULL;
    ASSERT(o0fs_mount(fixture.dev, &fixture.fs) == O0_OK, "remount failed");
    ASSERT(o0fs_check(fixture.dev) == O0_OK,
           "checker failed after commit-header crash");

    char buffer[sizeof(old_data)];
    memset(buffer, 0, sizeof(buffer));
    size_t got = 0;
    ASSERT(o0fs_read(fixture.fs, "/a", 0, buffer, sizeof(buffer), &got) ==
               O0_OK,
           "read after commit-header crash failed");
    ASSERT(got > 0, "read returned no data after commit-header crash");

    fixture_close(&fixture);
    PASS();
}

static void test_journal_crash_during_replay(void) {
    TEST("crash during live application: committed transaction replayed");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    static uint8_t old_data[64];
    fill_pattern(old_data, sizeof(old_data), 0x10u);
    size_t written = 0;
    ASSERT(o0fs_create_file(fixture.fs, "/a") == O0_OK, "create failed");
    ASSERT(o0fs_write(fixture.fs, "/a", 0, old_data, sizeof(old_data),
                      &written) == O0_OK,
           "initial write failed");

    /* Inject a fault at the block bitmap block, which is the first metadata
     * block applied during transaction live application. The commit header is
     * already durable, so recovery must replay the transaction. */
    uint32_t bitmap_lba = fixture.fs->super.block_bitmap_block;
    ASSERT(o0_ramdisk_inject_write_fault(fixture.dev, O0_BLOCK_ERR_IO,
                                         bitmap_lba, 1) == O0_OK,
           "block bitmap fault injection failed");

    static uint8_t new_data[5000];
    fill_pattern(new_data, sizeof(new_data), 0x20u);
    written = 0;
    o0_status st = o0fs_write(fixture.fs, "/a", 0, new_data,
                              sizeof(new_data), &written);
    ASSERT(st != O0_OK, "write should have failed with bitmap fault");
    fault_disable(fixture.dev);

    /* Remount: recovery finds the valid commit header and replays the
     * transaction, applying the staged metadata images to live locations. */
    ASSERT(o0fs_unmount(fixture.fs) == O0_OK, "unmount failed");
    fixture.fs = NULL;
    ASSERT(o0fs_mount(fixture.dev, &fixture.fs) == O0_OK, "remount failed");
    ASSERT(o0fs_check(fixture.dev) == O0_OK,
           "checker failed after replay crash");

    /* The committed transaction's data must be intact after replay. */
    char buffer[sizeof(new_data)];
    memset(buffer, 0, sizeof(buffer));
    size_t got = 0;
    ASSERT(o0fs_read(fixture.fs, "/a", 0, buffer, sizeof(buffer), &got) ==
               O0_OK,
           "read after replay crash failed");
    ASSERT(got == sizeof(new_data), "wrong read length after replay crash");
    ASSERT(memcmp(buffer, new_data, got) == 0,
           "data differs after replay crash");

    fixture_close(&fixture);
    PASS();
}

static void test_journal_checker_clean_volume(void) {
    TEST("consistency checker passes on a clean volume");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    ASSERT(o0fs_create_file(fixture.fs, "/a") == O0_OK, "create /a failed");
    static uint8_t data_a[100];
    fill_pattern(data_a, sizeof(data_a), 0x01u);
    size_t written = 0;
    ASSERT(o0fs_write(fixture.fs, "/a", 0, data_a, sizeof(data_a),
                      &written) == O0_OK,
           "write /a failed");

    ASSERT(o0fs_mkdir(fixture.fs, "/dir") == O0_OK, "mkdir /dir failed");
    ASSERT(o0fs_create_file(fixture.fs, "/dir/b") == O0_OK,
           "create /dir/b failed");
    static uint8_t data_b[200];
    fill_pattern(data_b, sizeof(data_b), 0x02u);
    written = 0;
    ASSERT(o0fs_write(fixture.fs, "/dir/b", 0, data_b, sizeof(data_b),
                      &written) == O0_OK,
           "write /dir/b failed");

    /* The public checker must pass on a fully consistent volume. */
    ASSERT(o0fs_check(fixture.dev) == O0_OK, "checker failed on clean volume");

    fixture_close(&fixture);
    PASS();
}

int main(void) {
    printf("Ozero OzeroFS crash recovery unit tests\n\n");

    test_journal_normal_write_and_remount();
    test_journal_crash_before_commit();
    test_journal_crash_commit_header();
    test_journal_crash_during_replay();
    test_journal_checker_clean_volume();

    printf("\n%d test(s) run, %d failure(s)\n", tests_run, failures);
    return failures == 0 ? 0 : 1;
}
