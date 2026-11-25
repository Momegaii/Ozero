#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/uaccess.h"

#define USER_BASE 0x0000000000020000ull
#define PAGE 0x1000ull

static int user_as;
static int kernel_as;
static uint8_t shadow[4 * PAGE];
static bool fail_from;
static bool fail_to;

/* Raw-core stand-ins. The real cores are assembly with exception-table fixups;
 * these exercise every decision the C wrapper makes around them. */
char o0_usercopy_from_fault[1];
char o0_usercopy_from_fixup[1];
char o0_usercopy_to_fault[1];
char o0_usercopy_to_fixup[1];

long o0_usercopy_raw_from_user(void *dst, const void *src, size_t len) {
    if (fail_from) return -1;
    memcpy(dst, shadow + ((uintptr_t)src - USER_BASE), len);
    return 0;
}

long o0_usercopy_raw_to_user(void *dst, const void *src, size_t len) {
    if (fail_to) return -1;
    memcpy(shadow + ((uintptr_t)dst - USER_BASE), src, len);
    return 0;
}

o0_as_t *o0_as_active(void) {
    return (o0_as_t *)&user_as;
}

o0_as_t *o0_as_kernel(void) {
    return (o0_as_t *)&kernel_as;
}

o0_status o0_as_query(o0_as_t *as, o0_vaddr_t vaddr, uint32_t *out_flags,
                      o0_paddr_t *out_phys) {
    if (as != (o0_as_t *)&user_as) return O0_EINVAL;
    const uint64_t page = vaddr & ~(PAGE - 1u);
    if (page == USER_BASE) {
        if (out_flags) *out_flags = O0_MAP_USER | O0_MAP_WRITE;
    } else if (page == USER_BASE + PAGE) {
        if (out_flags) *out_flags = O0_MAP_USER;
    } else if (page == USER_BASE + 2u * PAGE) {
        if (out_flags) *out_flags = O0_MAP_USER | O0_MAP_WRITE;
    } else {
        return O0_ENOENT;
    }
    if (out_phys) *out_phys = page;
    return O0_OK;
}

int main(void) {
    uint8_t kernel_buf[32];
    memset(shadow, 0x5a, sizeof(shadow));
    memset(kernel_buf, 0, sizeof(kernel_buf));

    assert(o0_user_range_check((void *)(uintptr_t)USER_BASE, 16, false) == O0_OK);
    assert(o0_user_range_check((void *)(uintptr_t)USER_BASE, 16, true) == O0_OK);
    assert(o0_user_range_check((void *)(uintptr_t)(USER_BASE + PAGE), 16, true) ==
           O0_EFAULT);
    assert(o0_user_range_check((void *)(uintptr_t)(USER_BASE + 3u * PAGE), 16, false) ==
           O0_EFAULT);
    assert(o0_user_range_check(NULL, 16, false) == O0_EFAULT);
    assert(o0_user_range_check((void *)(uintptr_t)0x10000ull, 1, false) ==
           O0_EFAULT);
    assert(o0_user_range_check((void *)(uintptr_t)0x800000000000ull, 1, false) ==
           O0_EFAULT);
    assert(o0_user_range_check((void *)(uintptr_t)(0x800000000000ull - 8u), 16,
                               false) == O0_EFAULT);
    assert(o0_user_range_check((void *)(uintptr_t)USER_BASE, 0, false) == O0_OK);
    assert(o0_syscall_arg_check((void *)(uintptr_t)USER_BASE, 8, false) == O0_OK);

    assert(o0_copy_from_user(kernel_buf, (void *)(uintptr_t)USER_BASE,
                             sizeof(kernel_buf)) == O0_OK);
    for (size_t i = 0; i < sizeof(kernel_buf); ++i) {
        assert(kernel_buf[i] == 0x5a);
    }
    assert(o0_copy_to_user((void *)(uintptr_t)(USER_BASE + 2u * PAGE), "ok", 2) ==
           O0_OK);
    assert(shadow[2u * PAGE] == 'o' && shadow[2u * PAGE + 1u] == 'k');

    assert(o0_copy_from_user(kernel_buf, (void *)(uintptr_t)USER_BASE, 0) == O0_OK);
    assert(o0_copy_from_user(NULL, (void *)(uintptr_t)USER_BASE, 1) == O0_EINVAL);
    assert(o0_copy_to_user((void *)(uintptr_t)USER_BASE, NULL, 1) == O0_EINVAL);
    assert(o0_copy_from_user(kernel_buf, (void *)(uintptr_t)USER_BASE, 16) ==
           O0_OK);
    fail_from = true;
    assert(o0_copy_from_user(kernel_buf, (void *)(uintptr_t)USER_BASE, 16) ==
           O0_EFAULT);
    fail_from = false;
    fail_to = true;
    assert(o0_copy_to_user((void *)(uintptr_t)USER_BASE, kernel_buf, 16) ==
           O0_EFAULT);
    fail_to = false;

    uint64_t fixup = 0;
    assert(o0_usercopy_fixup((uint64_t)(uintptr_t)o0_usercopy_from_fault, &fixup));
    assert(fixup == (uint64_t)(uintptr_t)o0_usercopy_from_fixup);
    assert(o0_usercopy_fixup((uint64_t)(uintptr_t)o0_usercopy_to_fault, &fixup));
    assert(fixup == (uint64_t)(uintptr_t)o0_usercopy_to_fixup);
    assert(!o0_usercopy_fixup(0x1234u, &fixup));
    assert(!o0_usercopy_fixup((uint64_t)(uintptr_t)o0_usercopy_to_fault, NULL));
    return 0;
}
