/* Host tests for Wave 3 object, budget, handle, memory and endpoint cores. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/budget.h"
#include "ozero/endpoint.h"
#include "ozero/handle.h"
#include "ozero/memory.h"
#include "ozero/mm.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/uapi.h"

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static uint8_t heap[2u << 20];
static size_t heap_used;
static uint8_t pages[64][O0_PAGE_SIZE];
static bool page_used[64];

void *o0_kalloc(size_t size, size_t align) {
    if (align == 0) {
        align = 8;
    }
    size_t at = (heap_used + align - 1u) & ~(align - 1u);
    if (at + size > sizeof(heap)) {
        return NULL;
    }
    heap_used = at + size;
    return heap + at;
}

void o0_kfree(void *ptr, size_t size) {
    (void)ptr;
    (void)size;
}

o0_paddr_t o0_pmm_alloc_pages(uint32_t order) {
    if (order != 0) {
        return 0;
    }
    for (size_t i = 0; i < 64; ++i) {
        if (!page_used[i]) {
            page_used[i] = true;
            return (o0_paddr_t)(uintptr_t)pages[i];
        }
    }
    return 0;
}

void o0_pmm_free_pages(o0_paddr_t base, uint32_t order) {
    (void)order;
    for (size_t i = 0; i < 64; ++i) {
        if ((o0_paddr_t)(uintptr_t)pages[i] == base) {
            page_used[i] = false;
            return;
        }
    }
}

void *o0_phys_to_virt(o0_paddr_t phys) {
    return (void *)(uintptr_t)phys;
}

/* memory.c map path is exercised on the emulator; host tests cover policy. */
struct o0_budget *o0_as_budget(o0_as_t *as) {
    (void)as;
    return NULL;
}
o0_status o0_as_query(o0_as_t *as, o0_vaddr_t vaddr, uint32_t *flags,
                      o0_paddr_t *phys) {
    (void)as;
    (void)vaddr;
    (void)flags;
    (void)phys;
    return O0_ENOENT;
}
o0_status o0_as_map(o0_as_t *as, o0_vaddr_t vaddr, o0_paddr_t paddr, size_t len,
                    uint32_t flags) {
    (void)as;
    (void)vaddr;
    (void)paddr;
    (void)len;
    (void)flags;
    return O0_OK;
}
o0_status o0_as_unmap(o0_as_t *as, o0_vaddr_t vaddr, size_t len) {
    (void)as;
    (void)vaddr;
    (void)len;
    return O0_OK;
}
o0_status o0_as_add_borrowed(o0_as_t *as, o0_vaddr_t vaddr, size_t len,
                             struct o0_object *owner) {
    (void)as;
    (void)vaddr;
    (void)len;
    (void)owner;
    return O0_OK;
}
struct o0_thread;
void o0_thread_wake_result(struct o0_thread *thread, int64_t status) {
    (void)thread;
    (void)status;
}

static int test_budget_and_memory(void) {
    CHECK(o0_budget_init_system() == O0_OK);
    CHECK(o0_budget_init_system() == O0_EBUSY);
    struct o0_budget *root = o0_budget_root();
    CHECK(root != NULL);
    struct o0_budget_snapshot snapshot;
    o0_budget_snapshot(root, &snapshot);
    CHECK(snapshot.limit == O0_BUDGET_ROOT_BYTES);
    CHECK(snapshot.used == 0);

    struct o0_budget *child = NULL;
    CHECK(o0_budget_split(root, O0_BUDGET_SPAWN_BYTES, &child) == O0_OK);
    o0_budget_snapshot(root, &snapshot);
    CHECK(snapshot.used >= O0_BUDGET_SPAWN_BYTES);
    o0_budget_snapshot(child, &snapshot);
    CHECK(snapshot.limit == O0_BUDGET_SPAWN_BYTES);
    CHECK(snapshot.used == 0);

    struct o0_budget *too_big = NULL;
    CHECK(o0_budget_split(child, O0_BUDGET_SPAWN_BYTES + 1, &too_big) == O0_ENOMEM);
    CHECK(o0_budget_charge(child, O0_BUDGET_SPAWN_BYTES + 1) == O0_ENOMEM);

    struct o0_memory *memory = NULL;
    CHECK(o0_memory_create(child, 8192, O0_RIGHT_MEMORY_DEFAULT, &memory) == O0_OK);
    CHECK(o0_memory_size(memory) == 8192);
    uint8_t data[8192];
    for (size_t i = 0; i < sizeof(data); ++i) {
        data[i] = (uint8_t)(i * 7u);
    }
    uint8_t readback[8192];
    CHECK(o0_memory_read(memory, 0, readback, sizeof(readback)) == O0_OK);
    for (size_t i = 0; i < sizeof(readback); ++i) {
        CHECK(readback[i] == 0);
    }
    struct o0_memory *image = NULL;
    CHECK(o0_memory_create_from(child, data, sizeof(data), O0_RIGHT_MEMORY_IMAGE,
                                &image) == O0_OK);
    CHECK(o0_memory_read(image, 0, readback, sizeof(readback)) == O0_OK);
    CHECK(memcmp(data, readback, sizeof(data)) == 0);
    CHECK(o0_memory_read(image, 8191, readback, 2) == O0_ERANGE);

    CHECK(o0_memory_prot_check(O0_RIGHT_MEMORY_DEFAULT, O0_PROT_READ | O0_PROT_WRITE) ==
          O0_OK);
    CHECK(o0_memory_prot_check(O0_RIGHT_MEMORY_DEFAULT,
                               O0_PROT_READ | O0_PROT_WRITE | O0_PROT_EXEC) ==
          O0_EPERM);
    CHECK(o0_memory_prot_check(O0_RIGHT_MEMORY_IMAGE, O0_PROT_READ | O0_PROT_EXEC) ==
          O0_OK);
    CHECK(o0_memory_prot_check(O0_RIGHT_MEMORY_IMAGE, O0_PROT_READ | O0_PROT_WRITE) ==
          O0_EPERM);
    CHECK(o0_memory_prot_check(O0_RIGHT_MEMORY_DEFAULT, O0_PROT_EXEC) == O0_EINVAL);
    CHECK(o0_memory_prot_check(O0_RIGHT_MEMORY_DEFAULT, 0x80) == O0_EINVAL);

    /* A partially charged allocation must unwind every page, the pages array
     * and the object header. The limit covers two of the four pages. */
    struct o0_budget *tight = NULL;
    size_t tight_limit = sizeof(struct o0_memory) + 4u * sizeof(o0_paddr_t) +
                         2u * O0_PAGE_SIZE;
    CHECK(o0_budget_split(root, tight_limit, &tight) == O0_OK);
    struct o0_memory *partial = NULL;
    CHECK(o0_memory_create(tight, 4u * O0_PAGE_SIZE, O0_RIGHT_MEMORY_DEFAULT,
                           &partial) == O0_ENOMEM);
    o0_budget_snapshot(tight, &snapshot);
    CHECK(snapshot.used == 0);
    o0_object_unref(&tight->obj);

    o0_budget_snapshot(child, &snapshot);
    uint64_t used_with_objects = snapshot.used;
    CHECK(used_with_objects > 16384);

    o0_object_unref(&memory->obj);
    o0_object_unref(&image->obj);
    o0_budget_snapshot(child, &snapshot);
    CHECK(snapshot.used < used_with_objects);

    o0_object_unref(&child->obj);
    o0_budget_snapshot(root, &snapshot);
    CHECK(snapshot.used == 0);
    return 0;
}

static int test_handles(void) {
    struct o0_budget *root = o0_budget_root();
    struct o0_budget *budget = NULL;
    CHECK(o0_budget_split(root, 1u << 20, &budget) == O0_OK);
    struct o0_handle_table *table = NULL;
    CHECK(o0_handle_table_create(budget, &table) == O0_OK);
    CHECK(o0_handle_free_slots(table) == O0_HANDLE_MAX);

    struct o0_memory *memory = NULL;
    CHECK(o0_memory_create(budget, 4096, O0_RIGHT_MEMORY_DEFAULT, &memory) == O0_OK);
    uint64_t handle = 0;
    CHECK(o0_handle_install(table, &memory->obj, O0_RIGHT_MEMORY_DEFAULT, &handle) ==
          O0_OK);
    o0_object_unref(&memory->obj);
    CHECK(handle != 0);

    struct o0_object *object = NULL;
    CHECK(o0_handle_lookup(table, handle, O0_OBJ_MEMORY, O0_RIGHT_READ, &object) ==
          O0_OK);
    CHECK(object == &memory->obj);
    CHECK(o0_handle_lookup(table, handle, O0_OBJ_ENDPOINT, O0_RIGHT_READ, &object) ==
          O0_ENOENT);
    CHECK(o0_handle_lookup(table, handle, O0_OBJ_MEMORY, O0_RIGHT_EXEC, &object) ==
          O0_EPERM);

    uint64_t duplicate = 0;
    CHECK(o0_handle_dup(table, handle, O0_RIGHT_READ, &duplicate) == O0_OK);
    CHECK(duplicate != handle);
    CHECK(o0_handle_dup(table, handle, O0_RIGHT_EXEC, &duplicate) == O0_EPERM);
    CHECK(o0_handle_dup(table, handle, 0x80000000u, &duplicate) == O0_EPERM);

    struct o0_handle_info info;
    CHECK(o0_handle_info(table, handle, &info) == O0_OK);
    CHECK(info.type == O0_OBJ_MEMORY);
    CHECK(info.rights == O0_RIGHT_MEMORY_DEFAULT);
    CHECK(info.charge_bytes > 0);

    CHECK(o0_handle_close(table, handle) == O0_OK);
    CHECK(o0_handle_close(table, handle) == O0_ENOENT);
    CHECK(o0_handle_lookup(table, handle, O0_OBJ_MEMORY, 0, &object) == O0_ENOENT);

    struct o0_memory *second = NULL;
    CHECK(o0_memory_create(budget, 4096, O0_RIGHT_MEMORY_DEFAULT, &second) == O0_OK);
    uint64_t reused = 0;
    CHECK(o0_handle_install(table, &second->obj, O0_RIGHT_MEMORY_DEFAULT, &reused) ==
          O0_OK);
    o0_object_unref(&second->obj);
    CHECK(reused != handle);
    CHECK(o0_handle_close(table, handle) == O0_ENOENT);
    CHECK(o0_handle_close(table, reused) == O0_OK);

    struct o0_memory *filler = NULL;
    CHECK(o0_memory_create(budget, 4096, O0_RIGHT_MEMORY_DEFAULT, &filler) == O0_OK);
    uint64_t handles[O0_HANDLE_MAX];
    uint32_t count = 0;
    while (count < O0_HANDLE_MAX) {
        o0_status st = o0_handle_install(table, &filler->obj, O0_RIGHT_READ,
                                         &handles[count]);
        if (st == O0_ENOMEM) {
            break;
        }
        CHECK(st == O0_OK);
        ++count;
    }
    CHECK(count > 0);
    CHECK(count < O0_HANDLE_MAX);
    for (uint32_t i = 0; i < count; ++i) {
        CHECK(o0_handle_close(table, handles[i]) == O0_OK);
    }
    o0_object_unref(&filler->obj);

    struct o0_endpoint *endpoint = NULL;
    CHECK(o0_endpoint_create(budget, false, &endpoint) == O0_OK);
    uint64_t endpoint_handle = 0;
    CHECK(o0_handle_install(table, &endpoint->obj, O0_RIGHT_ENDPOINT_DEFAULT,
                            &endpoint_handle) == O0_OK);
    o0_object_unref(&endpoint->obj);
    CHECK(o0_handle_lookup(table, endpoint_handle, O0_OBJ_ENDPOINT, O0_RIGHT_SEND,
                           &object) == O0_OK);
    CHECK(o0_handle_lookup(table, endpoint_handle, O0_OBJ_ENDPOINT, O0_RIGHT_SPLIT,
                           &object) == O0_EPERM);
    CHECK(o0_handle_close(table, endpoint_handle) == O0_OK);

    o0_handle_table_destroy(table);
    o0_object_unref(&budget->obj);
    return 0;
}

int main(void) {
    if (test_budget_and_memory() != 0) {
        return 1;
    }
    if (test_handles() != 0) {
        return 1;
    }
    /* The root budget is process-lifetime state; everything else must be gone. */
    if (o0_object_live_count() != 1u) {
        fprintf(stderr, "FAIL: object leak: live=%u\n", o0_object_live_count());
        return 1;
    }
    printf("cap_test: PASS\n");
    return 0;
}
