/* Host tests for capability-relative VFS and tmpfs. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/budget.h"
#include "ozero/mm.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/sync.h"
#include "ozero/vfs.h"

static uint8_t heap[2u << 20];
static size_t heap_used;

void *o0_kalloc(size_t size, size_t align) {
    if (align == 0) align = 8;
    size_t at = (heap_used + align - 1u) & ~(align - 1u);
    if (at > sizeof(heap) || size > sizeof(heap) - at) return NULL;
    heap_used = at + size;
    return heap + at;
}
void o0_kfree(void *ptr, size_t size) { (void)ptr; (void)size; }

void o0_spin_init(o0_spinlock_t *lock) {
    lock->locked = 0; lock->reserved = 0; lock->order = 0; lock->debug_owner = 0;
}
void o0_spin_init_ordered(o0_spinlock_t *lock, uint32_t order) {
    o0_spin_init(lock); lock->order = order;
}
void o0_spin_lock(o0_spinlock_t *lock) {
    while (__atomic_exchange_n(&lock->locked, 1u, __ATOMIC_ACQUIRE) != 0) { }
}
void o0_spin_unlock(o0_spinlock_t *lock) {
    __atomic_store_n(&lock->locked, 0u, __ATOMIC_RELEASE);
}
bool o0_spin_trylock(o0_spinlock_t *lock) {
    return __atomic_exchange_n(&lock->locked, 1u, __ATOMIC_ACQUIRE) == 0;
}

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; \
} } while (0)

int main(void) {
    CHECK(o0_budget_init_system() == O0_OK);
    struct o0_budget *budget = o0_budget_root();
    CHECK(budget != NULL);

    struct o0_vnode *root = NULL;
    CHECK(o0_tmpfs_create(budget, 8192u, &root) == O0_OK);
    CHECK(root != NULL && root->type == O0_VNODE_DIRECTORY);
    CHECK(o0_vfs_mount_root(root) == O0_OK);

    CHECK(o0_vfs_mkdir(root, "docs", 4u) == O0_OK);
    struct o0_vnode *docs = NULL;
    CHECK(o0_vfs_lookup(root, "docs", 4u, &docs) == O0_OK);
    CHECK(docs != NULL && docs->type == O0_VNODE_DIRECTORY);

    struct o0_vfs_file *file = NULL;
    CHECK(o0_vfs_open(docs, "note", 4u, O0_VFS_OPEN_CREATE,
                      O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_INFO,
                      budget, &file) == O0_OK);
    static const char payload[] = "ok";
    size_t count = 0;
    CHECK(o0_vfs_write(file, 5u, payload, 2u, &count) == O0_OK);
    CHECK(count == 2u);

    uint8_t data[16];
    memset(data, 0xCC, sizeof(data));
    CHECK(o0_vfs_read(file, 0, data, 7u, &count) == O0_OK);
    CHECK(count == 7u);
    for (size_t i = 0; i < 5u; ++i) CHECK(data[i] == 0);
    CHECK(data[5] == 'o' && data[6] == 'k');
    memset(data, 0xCC, sizeof(data));
    CHECK(o0_vfs_read(file, 0, data, sizeof(data), &count) == O0_OK);
    CHECK(count == 7u); /* EOF is not a zero-filled successful read. */

    /* Write-through page-cache coherence. */
    static const char changed[] = "NO";
    CHECK(o0_vfs_write(file, 5u, changed, 2u, &count) == O0_OK);
    memset(data, 0, sizeof(data));
    CHECK(o0_vfs_read(file, 0, data, 7u, &count) == O0_OK);
    CHECK(data[5] == 'N' && data[6] == 'O');

    struct o0_vfs_stat info;
    struct o0_vfs_file *missing = NULL;
    struct o0_vfs_file *read_only = NULL;
    CHECK(o0_vfs_open(docs, "note", 4u, 0, O0_RIGHT_READ, budget,
                      &read_only) == O0_OK);
    CHECK(o0_vfs_write(read_only, 0, changed, 2u, &count) == O0_EPERM);
    CHECK(o0_vfs_stat(docs, "note", 4u, &info) == O0_OK);
    CHECK(info.type == O0_VNODE_FILE && info.size == 7u);

    uint64_t cursor = 0;
    struct o0_vfs_dirent entry;
    CHECK(o0_vfs_readdir(docs, &cursor, &entry) == O0_OK);
    CHECK(entry.name_len == 4u && strcmp(entry.name, "note") == 0);
    CHECK(o0_vfs_readdir(docs, &cursor, &entry) == O0_ENOENT);
    CHECK(o0_vfs_open(docs, "note", 4u, O0_VFS_OPEN_CREATE | O0_VFS_OPEN_EXCLUSIVE,
                      O0_RIGHT_READ, budget, &missing) == O0_EEXIST);

    /* Open vnode survives unlink until its final file reference is released. */
    CHECK(o0_vfs_unlink(docs, "note", 4u) == O0_OK);
    CHECK(o0_vfs_read(file, 0, data, 7u, &count) == O0_OK && count == 7u);
    o0_object_unref(&read_only->obj);
    CHECK(o0_vfs_open(docs, "note", 4u, 0, O0_RIGHT_READ, budget, &missing) ==
          O0_ENOENT);
    o0_object_unref(&file->obj);

    /* Mount traversal, mountpoint lifetime and busy-unmount semantics. */
    CHECK(o0_vfs_mkdir(docs, "mnt", 3u) == O0_OK);
    struct o0_vnode *mountpoint = NULL;
    CHECK(o0_vfs_lookup(docs, "mnt", 3u, &mountpoint) == O0_OK);
    CHECK(o0_vfs_mount(mountpoint, root) == O0_EINVAL); /* no ancestor cycle */
    struct o0_vnode *mounted_root = NULL;
    CHECK(o0_tmpfs_create(budget, 4096u, &mounted_root) == O0_OK);
    struct o0_vfs_file *mounted_file = NULL;
    CHECK(o0_vfs_open(mounted_root, "inside", 6u, O0_VFS_OPEN_CREATE,
                      O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_INFO,
                      budget, &mounted_file) == O0_OK);
    CHECK(o0_vfs_write(mounted_file, 0, "mount", 5u, &count) == O0_OK);
    o0_object_unref(&mounted_file->obj);
    CHECK(o0_vfs_mount(mountpoint, mounted_root) == O0_OK);
    o0_object_unref(&mounted_root->obj);
    CHECK(o0_vfs_unlink(docs, "mnt", 3u) == O0_EBUSY);
    CHECK(o0_vfs_open(root, "docs/mnt/inside", 15u, 0, O0_RIGHT_READ,
                      budget, &mounted_file) == O0_OK);
    memset(data, 0, sizeof(data));
    CHECK(o0_vfs_read(mounted_file, 0, data, 5u, &count) == O0_OK);
    CHECK(count == 5u && memcmp(data, "mount", 5u) == 0);
    o0_object_unref(&mounted_file->obj);
    CHECK(o0_vfs_unmount(mountpoint) == O0_OK);
    o0_object_unref(&mountpoint->obj);
    struct o0_vnode *gone = NULL;
    CHECK(o0_vfs_lookup(root, "docs/mnt/inside", 15u, &gone) == O0_ENOENT);

    CHECK(o0_vfs_open(root, "", 0u, 0, O0_RIGHT_READ, budget, &missing) == O0_EINVAL);
    char raw_name[2] = { (char)0xff, '\0' };
    CHECK(o0_vfs_open(docs, raw_name, 1u, O0_VFS_OPEN_CREATE,
                      O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_INFO,
                      budget, &file) == O0_OK);
    o0_object_unref(&file->obj);
    CHECK(o0_vfs_open(docs, raw_name, 1u, 0, O0_RIGHT_READ,
                      budget, &file) == O0_OK);
    o0_object_unref(&file->obj);
    CHECK(o0_vfs_unlink(docs, raw_name, 1u) == O0_OK);
    CHECK(o0_vfs_mkdir(root, "../escape", 8u) == O0_EPERM);
    CHECK(o0_vfs_open(root, "../escape", 8u, 0, O0_RIGHT_READ,
                      budget, &missing) == O0_EPERM);
    char long_path[O0_VFS_PATH_MAX + 2u];
    memset(long_path, 'x', sizeof(long_path) - 1u);
    long_path[sizeof(long_path) - 1u] = '\0';
    CHECK(o0_vfs_open(root, long_path, O0_VFS_PATH_MAX + 1u, 0,
                      O0_RIGHT_READ, budget, &missing) == O0_EINVAL);

    /* Backend capacity is a hard limit and failure leaves no partial write. */
    CHECK(o0_vfs_open(docs, "full", 4u, O0_VFS_OPEN_CREATE,
                      O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_INFO,
                      budget, &file) == O0_OK);
    uint8_t too_much[9000];
    memset(too_much, 0xA5, sizeof(too_much));
    CHECK(o0_vfs_write(file, 0, too_much, sizeof(too_much), &count) == O0_ERANGE);
    CHECK(o0_vfs_write(file, 8191u, too_much, 2u, &count) == O0_ENOSPC);
    o0_object_unref(&file->obj);

    o0_object_unref(&docs->obj);
    o0_object_unref(&root->obj);
    puts("PASS VFS capability-relative paths, tmpfs, cache and lifetime");
    return 0;
}
