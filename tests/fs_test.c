/* Ozero kernel - OzeroFS unit test (host build).
 *
 * kernel/src/block.c, kernel/src/ramdisk.c and kernel/src/o0fs.c are included
 * so the filesystem's mount pool and on-disk structures are visible. The PMM
 * and the direct map are mocked with host memory, as in the other host tests.
 *
 * Every test formats its own RAM disk, so failures cannot leak between tests.
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

#define MOCK_PAGES 512u /* 2 MiB: two 512 KiB disks plus mount buffers */

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

static uint32_t baseline_free_blocks(const struct fixture *fixture) {
    return fixture->fs->super.data_block == 0
               ? 0
               : fixture->fs->super.block_count - fixture->fs->super.data_block;
}

static void test_format_and_mount(void) {
    TEST("format and mount a fresh volume");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    ASSERT(o0fs_block_count(fixture.fs) == DISK_BLOCKS, "wrong block count");
    ASSERT(o0fs_free_blocks(fixture.fs) ==
               baseline_free_blocks(&fixture),
           "free block count is wrong after format");
    ASSERT(o0fs_free_inodes(fixture.fs) ==
               fixture.fs->super.inode_count - 2u,
           "free inode count is wrong after format");

    struct o0fs_stat root;
    ASSERT(o0fs_stat(fixture.fs, "/", &root) == O0_OK, "root stat failed");
    ASSERT(root.type == O0FS_TYPE_DIR, "root is not a directory");
    ASSERT(root.inode == 1, "root inode should be 1");
    ASSERT(root.size == 0, "fresh root directory should be empty");

    fixture_close(&fixture);
    PASS();
}

static void test_format_rules(void) {
    TEST("format rules and mount validation");
    reset_world();
    ASSERT(o0fs_format(NULL) == O0_EINVAL, "NULL device accepted");

    struct o0_block_dev *tiny = NULL;
    ASSERT(o0_ramdisk_create("tiny", 64u * 1024u, &tiny) == O0_OK,
           "tiny disk creation failed");
    ASSERT(o0fs_format(tiny) == O0_ENOSPC, "too-small device accepted");
    ASSERT(o0_ramdisk_destroy(tiny) == O0_OK, "tiny disk destruction failed");

    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    /* Wipe the superblock. */
    static uint8_t zero[O0FS_BLOCK_SIZE];
    memset(zero, 0, sizeof(zero));
    ASSERT(o0_block_write(fixture.dev, 0, zero, sizeof(zero)) == O0_OK,
           "superblock wipe failed");
    struct o0fs *bad = NULL;
    ASSERT(o0fs_mount(fixture.dev, &bad) == O0_EINVAL,
           "mount accepted a zeroed superblock");
    ASSERT(bad == NULL, "failed mount returned a handle");

    /* Reformat, then corrupt the checksum. */
    ASSERT(o0fs_format(fixture.dev) == O0_OK, "reformat failed");
    static uint8_t super[O0FS_BLOCK_SIZE];
    ASSERT(o0_block_read(fixture.dev, 0, super, sizeof(super)) == O0_OK,
           "superblock read failed");
    super[20] ^= 0xffu;
    ASSERT(o0_block_write(fixture.dev, 0, super, sizeof(super)) == O0_OK,
           "superblock corruption failed");
    ASSERT(o0fs_mount(fixture.dev, &bad) == O0_EINVAL,
           "mount accepted a bad checksum");

    /* Recompute the checksum so the inode-count validator is actually tested. */
    ASSERT(o0fs_format(fixture.dev) == O0_OK, "second reformat failed");
    struct o0fs_super_disk malformed;
    ASSERT(o0_block_read(fixture.dev, 0, super, sizeof(super)) == O0_OK,
           "superblock reread failed");
    memcpy(&malformed, super, sizeof(malformed));
    malformed.inode_count = O0FS_INODES_PER_BLOCK + 1u;
    malformed.checksum = fs_checksum(&malformed);
    memset(super, 0, sizeof(super));
    memcpy(super, &malformed, sizeof(malformed));
    ASSERT(o0_block_write(fixture.dev, 0, super, sizeof(super)) == O0_OK,
           "malformed inode-count write failed");
    ASSERT(o0fs_mount(fixture.dev, &bad) == O0_EINVAL,
           "mount accepted a checksummed invalid inode count");

    /* Exercise overflow-safe layout validation with a valid checksum too. */
    ASSERT(o0fs_format(fixture.dev) == O0_OK, "third reformat failed");
    ASSERT(o0_block_read(fixture.dev, 0, super, sizeof(super)) == O0_OK,
           "layout superblock reread failed");
    memcpy(&malformed, super, sizeof(malformed));
    malformed.inode_table_blocks = UINT32_MAX;
    malformed.checksum = fs_checksum(&malformed);
    memset(super, 0, sizeof(super));
    memcpy(super, &malformed, sizeof(malformed));
    ASSERT(o0_block_write(fixture.dev, 0, super, sizeof(super)) == O0_OK,
           "malformed layout write failed");
    ASSERT(o0fs_mount(fixture.dev, &bad) == O0_EINVAL,
           "mount accepted a checksummed overflowing inode layout");

    fixture_close(&fixture);
    PASS();
}

static void test_file_roundtrip(void) {
    TEST("file create, write, read and stat");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    ASSERT(o0fs_create_file(fixture.fs, "/hello") == O0_OK,
           "file creation failed");
    static const char text[] = "hello o0fs";
    size_t written = 0;
    ASSERT(o0fs_write(fixture.fs, "/hello", 0, text, sizeof(text) - 1u,
                      &written) == O0_OK,
           "write failed");
    ASSERT(written == sizeof(text) - 1u, "short write");

    struct o0fs_stat info;
    ASSERT(o0fs_stat(fixture.fs, "/hello", &info) == O0_OK, "stat failed");
    ASSERT(info.type == O0FS_TYPE_FILE, "not a file");
    ASSERT(info.size == sizeof(text) - 1u, "wrong file size");

    char buffer[32];
    memset(buffer, 0, sizeof(buffer));
    size_t got = 0;
    ASSERT(o0fs_read(fixture.fs, "/hello", 0, buffer, sizeof(buffer), &got) ==
               O0_OK,
           "read failed");
    ASSERT(got == sizeof(text) - 1u, "wrong read length");
    ASSERT(memcmp(buffer, text, got) == 0, "read data does not match");

    got = 0;
    ASSERT(o0fs_read(fixture.fs, "/hello", 6, buffer, 4, &got) == O0_OK,
           "offset read failed");
    ASSERT(got == 4 && memcmp(buffer, "o0fs", 4) == 0, "offset read mismatch");

    got = 1;
    ASSERT(o0fs_read(fixture.fs, "/hello", 1000, buffer, sizeof(buffer), &got) ==
               O0_OK,
           "read past EOF should succeed");
    ASSERT(got == 0, "read past EOF should return zero bytes");

    fixture_close(&fixture);
    PASS();
}

static void test_write_rollback(void) {
    TEST("write rollback restores existing data and metadata failures");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    ASSERT(o0fs_create_file(fixture.fs, "/txn") == O0_OK, "create failed");
    static uint8_t original[8192];
    static uint8_t replacement[8192];
    static uint8_t readback[8192];
    memset(original, 0x31, sizeof(original));
    memset(replacement, 0xa7, sizeof(replacement));
    size_t written = 0;
    ASSERT(o0fs_write(fixture.fs, "/txn", 0, original, sizeof(original),
                      &written) == O0_OK,
           "initial file write failed");
    ASSERT(written == sizeof(original), "initial write was short");

    uint32_t inode_no = 0;
    struct o0fs_inode_disk inode;
    ASSERT(o0fs_lookup(fixture.fs, "/txn", &inode_no) == O0_OK,
           "inode lookup failed");
    ASSERT(fs_read_inode(fixture.fs, inode_no, &inode) == O0_OK,
           "inode read failed");
    ASSERT(inode.direct[0] != 0 && inode.direct[1] != 0,
           "two data blocks were not allocated");
    uint32_t free_before = o0fs_free_blocks(fixture.fs);

    ASSERT(o0_ramdisk_inject_fault(fixture.dev, O0_BLOCK_ERR_IO,
                                   inode.direct[1], 1) == O0_OK,
           "second-block fault injection failed");
    ASSERT(o0fs_write(fixture.fs, "/txn", 0, replacement,
                      sizeof(replacement), &written) == O0_EIO,
           "partial overwrite did not report its injected failure");
    ASSERT(o0fs_free_blocks(fixture.fs) == free_before,
           "failed overwrite changed free-space accounting");
    memset(readback, 0, sizeof(readback));
    size_t got = 0;
    ASSERT(o0fs_read(fixture.fs, "/txn", 0, readback, sizeof(readback),
                     &got) == O0_OK,
           "read after partial overwrite failure failed");
    ASSERT(got == sizeof(original) &&
               memcmp(readback, original, sizeof(original)) == 0,
           "partial overwrite failure did not restore original bytes");

    /* Fail only the inode-table write, after the new data block was written. */
    ASSERT(o0_ramdisk_inject_write_fault(fixture.dev, O0_BLOCK_ERR_IO,
                                         fs_inode_lba(fixture.fs, inode_no),
                                         1) == O0_OK,
           "inode-write fault injection failed");
    ASSERT(o0fs_write(fixture.fs, "/txn", 0, replacement, 512,
                      &written) == O0_EIO,
           "final inode persistence failure was not reported");
    memset(readback, 0, sizeof(readback));
    got = 0;
    ASSERT(o0fs_read(fixture.fs, "/txn", 0, readback, sizeof(readback),
                     &got) == O0_OK,
           "read after inode persistence failure failed");
    ASSERT(got == sizeof(original) &&
               memcmp(readback, original, sizeof(original)) == 0,
           "inode persistence failure did not restore original bytes");

    fixture_close(&fixture);
    PASS();
}

static void test_indirect_blocks_and_truncate(void) {
    TEST("indirect blocks, truncate and zero fill");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");
    uint32_t baseline = o0fs_free_blocks(fixture.fs);

    static uint8_t big[64u * 1024u];
    static uint8_t back[64u * 1024u];
    for (size_t i = 0; i < sizeof(big); ++i) {
        big[i] = (uint8_t)(i * 13u + 5u);
    }

    ASSERT(o0fs_create_file(fixture.fs, "/big") == O0_OK, "create failed");
    size_t written = 0;
    ASSERT(o0fs_write(fixture.fs, "/big", 0, big, sizeof(big), &written) ==
               O0_OK,
           "large write failed");
    ASSERT(written == sizeof(big), "short large write");

    memset(back, 0, sizeof(back));
    size_t got = 0;
    ASSERT(o0fs_read(fixture.fs, "/big", 0, back, sizeof(back), &got) == O0_OK,
           "large read failed");
    ASSERT(got == sizeof(big), "large read short");
    ASSERT(memcmp(big, back, sizeof(big)) == 0, "large file data mismatch");

    /* 16 data blocks, one indirect block and the root directory block. */
    ASSERT(o0fs_free_blocks(fixture.fs) == baseline - 18u,
           "unexpected block usage for a 64 KiB file");

    ASSERT(o0fs_truncate(fixture.fs, "/big", 4096) == O0_OK, "shrink failed");
    struct o0fs_stat info;
    ASSERT(o0fs_stat(fixture.fs, "/big", &info) == O0_OK, "stat failed");
    ASSERT(info.size == 4096, "shrink size wrong");
    /* Only the root directory block and the file's first block remain. */
    ASSERT(o0fs_free_blocks(fixture.fs) == baseline - 2u,
           "shrink did not release blocks");

    ASSERT(o0fs_truncate(fixture.fs, "/big", 8192) == O0_OK, "grow failed");
    memset(back, 0xff, sizeof(back));
    got = 0;
    ASSERT(o0fs_read(fixture.fs, "/big", 4096, back, 4096, &got) == O0_OK,
           "read after grow failed");
    ASSERT(got == 4096, "grown region has the wrong length");
    for (size_t i = 0; i < got; ++i) {
        ASSERT(back[i] == 0, "grown region was not zero-filled");
    }

    ASSERT(o0fs_unlink(fixture.fs, "/big") == O0_OK, "unlink failed");
    /* The root directory keeps its allocated block. */
    ASSERT(o0fs_free_blocks(fixture.fs) == baseline - 1u,
           "unlink did not release the file's blocks");

    fixture_close(&fixture);
    PASS();
}

struct listing {
    int count;
    bool saw_dir;
    bool saw_file;
    uint64_t file_size;
};

static void list_callback(void *ctx, const char *name,
                          const struct o0fs_stat *stat) {
    struct listing *listing_state = (struct listing *)ctx;
    ++listing_state->count;
    if (strcmp(name, "dir") == 0 && stat->type == O0FS_TYPE_DIR) {
        listing_state->saw_dir = true;
    }
    if (strcmp(name, "file") == 0 && stat->type == O0FS_TYPE_FILE) {
        listing_state->saw_file = true;
        listing_state->file_size = stat->size;
    }
}

static void test_directories(void) {
    TEST("directories and directory listing");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    ASSERT(o0fs_mkdir(fixture.fs, "/dir") == O0_OK, "mkdir failed");
    ASSERT(o0fs_create_file(fixture.fs, "/dir/file") == O0_OK,
           "nested create failed");
    size_t written = 0;
    ASSERT(o0fs_write(fixture.fs, "/dir/file", 0, "data", 4, &written) == O0_OK,
           "nested write failed");
    ASSERT(written == 4, "nested write short");

    struct listing state;
    memset(&state, 0, sizeof(state));
    ASSERT(o0fs_list(fixture.fs, "/", list_callback, &state) == O0_OK,
           "root listing failed");
    ASSERT(state.count == 1 && state.saw_dir, "root listing is wrong");

    memset(&state, 0, sizeof(state));
    ASSERT(o0fs_list(fixture.fs, "/dir", list_callback, &state) == O0_OK,
           "nested listing failed");
    ASSERT(state.count == 1 && state.saw_file, "nested listing is wrong");
    ASSERT(state.file_size == 4, "listed file size is wrong");

    ASSERT(o0fs_rmdir(fixture.fs, "/dir") == O0_ENOTEMPTY,
           "non-empty directory was removed");
    ASSERT(o0fs_unlink(fixture.fs, "/dir/file") == O0_OK, "file unlink failed");
    ASSERT(o0fs_rmdir(fixture.fs, "/dir") == O0_OK, "rmdir failed");
    ASSERT(o0fs_lookup(fixture.fs, "/dir", NULL) == O0_EINVAL,
           "NULL inode output accepted");
    uint32_t inode = 0;
    ASSERT(o0fs_lookup(fixture.fs, "/dir", &inode) == O0_ENOENT,
           "removed directory still resolves");

    fixture_close(&fixture);
    PASS();
}

static void test_error_paths(void) {
    TEST("path and type error paths");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    uint32_t inode = 0;
    ASSERT(o0fs_lookup(fixture.fs, "relative", &inode) == O0_EINVAL,
           "relative path accepted");
    ASSERT(o0fs_lookup(fixture.fs, "/missing", &inode) == O0_ENOENT,
           "missing path resolved");
    ASSERT(o0fs_create_file(fixture.fs, "/") == O0_EINVAL,
           "root was recreated");
    ASSERT(o0fs_create_file(fixture.fs, "/a/b") == O0_ENOENT,
           "creation below a missing directory succeeded");
    ASSERT(o0fs_create_file(fixture.fs, "/..") == O0_EINVAL,
           "'..' was accepted");

    char long_name[64];
    memset(long_name, 'x', sizeof(long_name));
    long_name[0] = '/';
    long_name[25] = '\0'; /* 24-character name, one too long */
    ASSERT(o0fs_create_file(fixture.fs, long_name) == O0_EINVAL,
           "overlong name accepted");

    char long_path[O0FS_PATH_MAX + 8u];
    memset(long_path, 'a', sizeof(long_path));
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1u] = '\0';
    ASSERT(o0fs_lookup(fixture.fs, long_path, &inode) == O0_EINVAL,
           "overlong path accepted");

    ASSERT(o0fs_create_file(fixture.fs, "/present") == O0_OK,
           "lookup-I/O fixture create failed");
    struct o0fs_inode_disk root_inode;
    ASSERT(fs_read_inode(fixture.fs, fixture.fs->super.root_inode,
                         &root_inode) == O0_OK,
           "root inode read failed");
    ASSERT(root_inode.direct[0] != 0, "root directory block missing");
    ASSERT(o0_ramdisk_inject_fault(fixture.dev, O0_BLOCK_ERR_IO,
                                   root_inode.direct[0], 1) == O0_OK,
           "directory lookup fault injection failed");
    ASSERT(o0fs_lookup(fixture.fs, "/present", &inode) == O0_EIO,
           "directory I/O failure was misreported as missing file");

    ASSERT(o0fs_mkdir(fixture.fs, "/dir") == O0_OK, "mkdir failed");
    ASSERT(o0fs_mkdir(fixture.fs, "/dir") == O0_EEXIST,
           "duplicate directory was accepted");
    ASSERT(o0fs_unlink(fixture.fs, "/dir") == O0_EISDIR,
           "directory was unlinked as a file");
    ASSERT(o0fs_rmdir(fixture.fs, "/dir/") == O0_OK,
           "trailing slash broke rmdir");
    ASSERT(o0fs_create_file(fixture.fs, "/file") == O0_OK, "create failed");

    size_t written = 0;
    ASSERT(o0fs_write(fixture.fs, "/file", 0, "x", 1, &written) == O0_OK,
           "write failed");
    ASSERT(o0fs_mkdir(fixture.fs, "/file/child") == O0_ENOTDIR,
           "path below a file was accepted");
    ASSERT(o0fs_read(fixture.fs, "/file/child", 0, NULL, 0, &written) ==
               O0_ENOTDIR,
           "read below a file was accepted");
    ASSERT(o0fs_rmdir(fixture.fs, "/file") == O0_ENOTDIR,
           "file was removed as a directory");

    fixture_close(&fixture);
    PASS();
}

static void test_disk_full(void) {
    TEST("ENOSPC when the volume fills");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");
    uint32_t baseline = o0fs_free_blocks(fixture.fs);

    static uint8_t block[O0FS_BLOCK_SIZE];
    memset(block, 0xa5, sizeof(block));
    ASSERT(o0fs_create_file(fixture.fs, "/fill") == O0_OK, "create failed");

    uint64_t offset = 0;
    size_t written = 0;
    o0_status st = O0_OK;
    for (;;) {
        st = o0fs_write(fixture.fs, "/fill", offset, block, sizeof(block),
                        &written);
        if (st != O0_OK) {
            break;
        }
        offset += sizeof(block);
        if (offset > 8u * 1024u * 1024u) {
            break;
        }
    }
    ASSERT(st == O0_ENOSPC, "filling the disk did not report ENOSPC");
    ASSERT(o0fs_free_blocks(fixture.fs) <= 1, "free blocks remain after ENOSPC");

    ASSERT(o0fs_unlink(fixture.fs, "/fill") == O0_OK, "unlink failed");
    ASSERT(o0fs_free_blocks(fixture.fs) == baseline - 1u,
           "disk space was not recovered after unlink");

    fixture_close(&fixture);
    PASS();
}

static void test_persistence_and_faults(void) {
    TEST("persistence across remount and injected I/O faults");
    reset_world();
    struct fixture fixture;
    ASSERT(fixture_open(&fixture), "fixture setup failed");

    ASSERT(o0fs_mkdir(fixture.fs, "/data") == O0_OK, "mkdir failed");
    ASSERT(o0fs_create_file(fixture.fs, "/data/note") == O0_OK,
           "create failed");
    static const char note[] = "persistent o0fs";
    size_t written = 0;
    ASSERT(o0fs_write(fixture.fs, "/data/note", 0, note, sizeof(note) - 1u,
                      &written) == O0_OK,
           "write failed");

    /* Inject a fault in the file's data block. The first free data block is
     * taken by the root directory, the second by /data, so the file data is at
     * data_block + 3. Use the recorded inode block map instead of guessing. */
    struct o0fs_inode_disk inode;
    uint32_t inode_no = 0;
    ASSERT(o0fs_lookup(fixture.fs, "/data/note", &inode_no) == O0_OK,
           "lookup failed");
    ASSERT(fs_read_inode(fixture.fs, inode_no, &inode) == O0_OK,
           "inode read failed");
    ASSERT(inode.direct[0] != 0, "file has no data block");
    ASSERT(o0_ramdisk_inject_fault(fixture.dev, O0_BLOCK_ERR_IO,
                                   inode.direct[0], 1) == O0_OK,
           "fault injection failed");
    char buffer[32];
    size_t got = 0;
    ASSERT(o0fs_read(fixture.fs, "/data/note", 0, buffer, sizeof(buffer),
                     &got) == O0_EIO,
           "injected read fault was not reported");
    ASSERT(o0fs_read(fixture.fs, "/data/note", 0, buffer, sizeof(buffer),
                     &got) == O0_OK,
           "read did not recover after the fault");

    ASSERT(o0fs_unmount(fixture.fs) == O0_OK, "unmount failed");
    fixture.fs = NULL;
    ASSERT(o0fs_mount(fixture.dev, &fixture.fs) == O0_OK,
           "remount failed");

    memset(buffer, 0, sizeof(buffer));
    got = 0;
    ASSERT(o0fs_read(fixture.fs, "/data/note", 0, buffer, sizeof(buffer),
                     &got) == O0_OK,
           "read after remount failed");
    ASSERT(got == sizeof(note) - 1u, "remounted file has the wrong size");
    ASSERT(memcmp(buffer, note, got) == 0, "remounted file data differs");

    struct o0fs_stat info;
    ASSERT(o0fs_stat(fixture.fs, "/data/note", &info) == O0_OK,
           "stat after remount failed");
    ASSERT(info.size == sizeof(note) - 1u, "stat size after remount is wrong");

    fixture_close(&fixture);
    PASS();
}

static void test_mount_pool_limit(void) {
    TEST("mount pool limit");
    reset_world();
    struct o0_block_dev *first_dev = NULL;
    struct o0_block_dev *second_dev = NULL;
    struct o0fs *first = NULL;
    struct o0fs *second = NULL;

    ASSERT(o0_ramdisk_create("ram0", 128u * 1024u, &first_dev) == O0_OK,
           "first disk creation failed");
    ASSERT(o0_ramdisk_create("ram1", 128u * 1024u, &second_dev) == O0_OK,
           "second disk creation failed");
    ASSERT(o0fs_format(first_dev) == O0_OK, "first format failed");
    ASSERT(o0fs_format(second_dev) == O0_OK, "second format failed");
    ASSERT(o0fs_mount(first_dev, &first) == O0_OK, "first mount failed");
    ASSERT(o0fs_mount(second_dev, &second) == O0_OK, "second mount failed");

    struct o0fs *third = NULL;
    ASSERT(o0fs_mount(first_dev, &third) == O0_EBUSY,
           "mount beyond the pool limit succeeded");
    ASSERT(third == NULL, "failed mount returned a handle");

    ASSERT(o0fs_unmount(first) == O0_OK, "first unmount failed");
    ASSERT(o0fs_mount(first_dev, &third) == O0_OK,
           "mount did not succeed after unmount");

    ASSERT(o0fs_unmount(third) == O0_OK, "third unmount failed");
    ASSERT(o0fs_unmount(second) == O0_OK, "second unmount failed");
    ASSERT(o0_ramdisk_destroy(first_dev) == O0_OK, "first destroy failed");
    ASSERT(o0_ramdisk_destroy(second_dev) == O0_OK, "second destroy failed");
    PASS();
}

int main(void) {
    printf("Ozero OzeroFS unit tests\n\n");

    test_format_and_mount();
    test_format_rules();
    test_file_roundtrip();
    test_write_rollback();
    test_indirect_blocks_and_truncate();
    test_directories();
    test_error_paths();
    test_disk_full();
    test_persistence_and_faults();
    test_mount_pool_limit();

    printf("\n%d test(s) run, %d failure(s)\n", tests_run, failures);
    return failures == 0 ? 0 : 1;
}
