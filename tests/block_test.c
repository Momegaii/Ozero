/* Ozero kernel - block layer and RAM disk unit test (host build).
 *
 * kernel/src/block.c and kernel/src/ramdisk.c are included so their static
 * registry and device state are visible. The PMM and the direct map are
 * replaced by mocks that hand out host memory, exactly as in slab_test.c.
 *
 * Run it with `make test`.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/block.h"
#include "ozero/mm.h"
#include "ozero/ramdisk.h"
#include "ozero/status.h"
#include "ozero/types.h"

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

#include "../kernel/src/block.c"
#include "../kernel/src/ramdisk.c"

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

static struct o0_block_dev async_dev;
static struct o0_block_queue async_queue;
static struct o0_bio *late_bio;
static bool async_hold;
static bool async_cancel_on_timeout;

static o0_status async_queue_rq(struct o0_block_queue *queue,
                                struct o0_bio *bio) {
    (void)queue;
    if (async_hold) {
        late_bio = bio;
        return O0_OK;
    }
    if (bio->op == O0_BLOCK_READ) {
        memset((uint8_t *)bio->vecs[0].page + bio->vecs[0].offset,
               0x6c, bio->vecs[0].len);
        o0_block_complete_bio(bio, O0_BLOCK_OK, bio->vecs[0].len);
    } else {
        o0_block_complete_bio(bio, O0_BLOCK_OK, 0);
    }
    return O0_OK;
}

static o0_status async_timeout(struct o0_block_queue *queue,
                               struct o0_bio *bio) {
    (void)queue;
    (void)bio;
    return async_cancel_on_timeout ? O0_OK : O0_EAGAIN;
}

static const struct o0_block_queue_ops async_queue_ops = {
    .queue_rq = async_queue_rq,
    .poll = NULL,
    .timeout = async_timeout
};

static const struct o0_block_ops async_block_ops = { 0 };

static void test_timeout_late_completion(void) {
    TEST("timed-out request remains valid through late completion");
    memset(&async_dev, 0, sizeof(async_dev));
    memset(&async_queue, 0, sizeof(async_queue));
    memcpy(async_dev.name, "async0", 7);
    async_dev.geo.logical_block_size = 512;
    async_dev.geo.physical_block_size = 512;
    async_dev.geo.nr_sectors = 8;
    async_dev.geo.max_segments = 1;
    async_dev.geo.max_segment_size = 4096;
    async_dev.nr_queues = 1;
    async_dev.queues = &async_queue;
    async_dev.ops = &async_block_ops;
    async_queue.id = 0;
    async_queue.depth = 1;
    async_queue.ops = &async_queue_ops;
    async_hold = true;
    late_bio = NULL;

    ASSERT(o0_block_register(&async_dev) == O0_OK,
           "asynchronous test device registration failed");
    static uint8_t caller[512];
    memset(caller, 0x11, sizeof(caller));
    ASSERT(o0_block_read(&async_dev, 0, caller, sizeof(caller)) ==
               O0_ETIMEDOUT,
           "held request did not time out");
    ASSERT(late_bio != NULL, "driver did not retain the submitted bio");
    ASSERT(late_bio->vecs[0].page != caller,
           "driver retained the caller's buffer instead of the bounce page");

    memset(late_bio->vecs[0].page, 0x22, 512);
    o0_block_complete_bio(late_bio, O0_BLOCK_OK, 512);
    o0_block_complete_bio(late_bio, O0_BLOCK_OK, 512); /* duplicate is ignored */
    for (size_t i = 0; i < sizeof(caller); ++i) {
        ASSERT(caller[i] == 0x11,
               "late DMA/completion modified the expired caller buffer");
    }

    async_hold = true;
    async_cancel_on_timeout = true;
    late_bio = NULL;
    ASSERT(o0_block_read(&async_dev, 0, caller, sizeof(caller)) ==
               O0_ETIMEDOUT,
           "successful cancellation did not report the timeout");
    ASSERT(late_bio != NULL, "cancel test request was not submitted");
    /* O0_OK from timeout promises the driver will never touch this bio again. */
    late_bio = NULL;
    async_hold = false;
    async_cancel_on_timeout = false;

    ASSERT(o0_block_read(&async_dev, 0, caller, sizeof(caller)) == O0_OK,
           "request slot was not reusable after confirmed cancellation");
    for (size_t i = 0; i < sizeof(caller); ++i) {
        ASSERT(caller[i] == 0x6c, "bounce-buffer read copy-back failed");
    }
    o0_block_unregister(&async_dev);
    PASS();
}

static void test_transfer_limit_validation(void) {
    TEST("block transfer sector and segment limits");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("limits", 256u * 1024u, &dev) == O0_OK,
           "ramdisk creation failed");

    static uint8_t page_a[4096] __attribute__((aligned(4096)));
    static uint8_t page_b[4096] __attribute__((aligned(4096)));
    struct o0_bio_vec vecs[2] = {
        { page_a, 0, sizeof(page_a) },
        { page_b, 0, sizeof(page_b) }
    };
    struct o0_bio bio;
    memset(&bio, 0, sizeof(bio));
    bio.op = O0_BLOCK_READ;
    bio.nr_vecs = 2;
    bio.vecs = vecs;
    bio.end_io = block_sync_end; /* validation rejects before dispatch */

    dev->geo.max_segments = 1;
    dev->geo.max_hw_sectors = 2;
    ASSERT(o0_block_submit_bio(dev, &bio) == O0_EINVAL,
           "request exceeded max_segments but was accepted");

    dev->geo.max_segments = 2;
    dev->geo.max_hw_sectors = 1;
    ASSERT(o0_block_submit_bio(dev, &bio) == O0_ERANGE,
           "request exceeded max_hw_sectors but was accepted");

    ASSERT(o0_ramdisk_destroy(dev) == O0_OK, "ramdisk destruction failed");
    PASS();
}

static void test_registration_validation(void) {
    TEST("registration rejects invalid devices");
    static struct o0_block_dev dev;
    static struct o0_block_queue queue;
    static const struct o0_block_queue_ops queue_ops = { 0 };

    ASSERT(o0_block_register(NULL) == O0_EINVAL, "NULL device accepted");

    memset(&dev, 0, sizeof(dev));
    ASSERT(o0_block_register(&dev) == O0_EINVAL, "empty device accepted");

    memset(&dev, 0, sizeof(dev));
    memcpy(dev.name, "bad", 4);
    dev.geo.logical_block_size = 3000; /* not a power of two */
    dev.geo.nr_sectors = 8;
    dev.nr_queues = 1;
    dev.queues = &queue;
    dev.ops = (const struct o0_block_ops *)&queue_ops;
    ASSERT(o0_block_register(&dev) == O0_EINVAL, "bad block size accepted");

    dev.geo.logical_block_size = 4096;
    dev.geo.nr_sectors = 0;
    ASSERT(o0_block_register(&dev) == O0_EINVAL, "zero sectors accepted");

    dev.geo.nr_sectors = 16;
    queue.ops = NULL;
    ASSERT(o0_block_register(&dev) == O0_EINVAL, "queue without ops accepted");

    queue.ops = &queue_ops; /* queue_rq is NULL */
    ASSERT(o0_block_register(&dev) == O0_EINVAL, "queue without queue_rq accepted");

    ASSERT(o0_block_device_count() == 0, "rejected device was registered");
    PASS();
}

static void test_read_write_roundtrip(void) {
    TEST("ramdisk read/write round-trip");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram0", 256u * 1024u, &dev) == O0_OK,
           "ramdisk creation failed");
    ASSERT(dev != NULL, "no device returned");
    ASSERT(o0_block_device_count() == 1, "device not registered");
    ASSERT(o0_block_device_by_index(0) == dev, "registry lookup mismatch");
    ASSERT(dev->geo.logical_block_size == 4096, "unexpected block size");
    ASSERT(dev->geo.nr_sectors == 64, "unexpected sector count");

    static uint8_t written[4096];
    static uint8_t read_back[4096];
    for (size_t i = 0; i < sizeof(written); ++i) {
        written[i] = (uint8_t)(i * 7u + 3u);
    }

    ASSERT(o0_block_write(dev, 3, written, sizeof(written)) == O0_OK,
           "block write failed");
    ASSERT(o0_block_read(dev, 3, read_back, sizeof(read_back)) == O0_OK,
           "block read failed");
    ASSERT(memcmp(written, read_back, sizeof(written)) == 0,
           "read data does not match written data");
    ASSERT(o0_block_flush(dev) == O0_OK, "flush failed");

    ASSERT(o0_block_read(dev, 64, read_back, sizeof(read_back)) == O0_ERANGE,
           "read beyond the device should be ERANGE");
    ASSERT(o0_block_read(dev, 3, read_back, 100) == O0_EINVAL,
           "read of a partial block should be EINVAL");
    ASSERT(o0_block_read(dev, 3, NULL, 4096) == O0_EINVAL,
           "NULL buffer should be EINVAL");
    ASSERT(o0_block_write(dev, 3, NULL, 4096) == O0_EINVAL,
           "NULL write buffer should be EINVAL");

    ASSERT(o0_ramdisk_destroy(dev) == O0_OK, "ramdisk destruction failed");
    ASSERT(o0_block_device_count() == 0, "device left registered");
    PASS();
}

static void test_fault_injection(void) {
    TEST("fault injection propagates and clears");
    struct o0_block_dev *dev = NULL;
    ASSERT(o0_ramdisk_create("ram0", 256u * 1024u, &dev) == O0_OK,
           "ramdisk creation failed");

    static uint8_t buffer[4096];
    memset(buffer, 0x5a, sizeof(buffer));
    ASSERT(o0_block_write(dev, 5, buffer, sizeof(buffer)) == O0_OK,
           "initial write failed");

    ASSERT(o0_ramdisk_inject_fault(dev, O0_BLOCK_ERR_IO, 5, 1) == O0_OK,
           "fault injection failed");
    ASSERT(o0_block_read(dev, 5, buffer, sizeof(buffer)) == O0_EIO,
           "injected read fault was not reported");
    ASSERT(o0_block_read(dev, 5, buffer, sizeof(buffer)) == O0_OK,
           "fault was not cleared after one transfer");

    ASSERT(o0_ramdisk_inject_fault(dev, O0_BLOCK_ERR_MEDIUM, 2, -1) == O0_OK,
           "unlimited fault injection failed");
    ASSERT(o0_block_read(dev, 2, buffer, sizeof(buffer)) == O0_EIO,
           "medium error was not reported as EIO");
    ASSERT(o0_block_read(dev, 2, buffer, sizeof(buffer)) == O0_EIO,
           "unlimited fault did not persist");
    ASSERT(o0_ramdisk_inject_fault(dev, O0_BLOCK_OK, 0, 0) == O0_OK,
           "fault disable failed");
    ASSERT(o0_block_read(dev, 2, buffer, sizeof(buffer)) == O0_OK,
           "device did not recover");

    ASSERT(o0_ramdisk_destroy(dev) == O0_OK, "ramdisk destruction failed");
    PASS();
}

static void test_duplicate_and_limits(void) {
    TEST("duplicate names and device limits");
    struct o0_block_dev *first = NULL;
    struct o0_block_dev *second = NULL;
    ASSERT(o0_ramdisk_create("ram0", 256u * 1024u, &first) == O0_OK,
           "first ramdisk creation failed");
    ASSERT(o0_ramdisk_create("ram0", 256u * 1024u, &second) == O0_EBUSY,
           "duplicate device name was accepted");
    ASSERT(second == NULL, "failed creation returned a device");
    ASSERT(o0_ramdisk_create("ram1", 256u * 1024u, &second) == O0_OK,
           "second ramdisk creation failed");

    struct o0_block_dev *third = NULL;
    ASSERT(o0_ramdisk_create("ram2", 256u * 1024u, &third) == O0_EBUSY,
           "third ramdisk should exhaust the pool");
    ASSERT(o0_ramdisk_destroy(first) == O0_OK, "destroy first failed");
    ASSERT(o0_ramdisk_destroy(second) == O0_OK, "destroy second failed");

    struct o0_block_dev *small = NULL;
    ASSERT(o0_ramdisk_create("small", 4096, &small) == O0_EINVAL,
           "tiny ramdisk was accepted");
    struct o0_block_dev *huge = NULL;
    ASSERT(o0_ramdisk_create("huge", O0_RAMDISK_MAX_BYTES + 4096u, &huge) ==
               O0_EINVAL,
           "oversized ramdisk was accepted");

    ASSERT(o0_block_device_count() == 0, "devices leaked after destruction");
    PASS();
}

int main(void) {
    printf("Ozero block layer unit tests\n\n");

    test_registration_validation();
    test_timeout_late_completion();
    test_transfer_limit_validation();
    test_read_write_roundtrip();
    test_fault_injection();
    test_duplicate_and_limits();

    printf("\n%d test(s) run, %d failure(s)\n", tests_run, failures);
    return failures == 0 ? 0 : 1;
}
