/* Ozero kernel - user-memory access: validation and fault-recovering copy.
 *
 * The copy cores are in kernel/arch/x86_64/usercopy.S. Each faulting
 * instruction has a fixup label that the exception handler can jump to. Even
 * though the range is validated first, a concurrent unmap or a stale TLB entry
 * can still fault, so the recovery path is mandatory rather than decorative.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"
#include "ozero/uaccess.h"
#include "ozero/uapi.h"

/* Raw cores: 0 on success, O0_EFAULT if the fixup was taken. */
extern long o0_usercopy_raw_from_user(void *dst, const void *src, size_t len);
extern long o0_usercopy_raw_to_user(void *dst, const void *src, size_t len);
extern long o0_usercopy_raw_from_user_smap(void *dst, const void *src, size_t len)
    __attribute__((weak));
extern long o0_usercopy_raw_to_user_smap(void *dst, const void *src, size_t len)
    __attribute__((weak));

extern char o0_usercopy_from_fault[];
extern char o0_usercopy_from_fixup[];
extern char o0_usercopy_to_fault[];
extern char o0_usercopy_to_fixup[];
extern char o0_usercopy_from_fault_smap[] __attribute__((weak));
extern char o0_usercopy_from_fixup_smap[] __attribute__((weak));
extern char o0_usercopy_to_fault_smap[] __attribute__((weak));
extern char o0_usercopy_to_fixup_smap[] __attribute__((weak));

extern bool o0_cpu_smap_enabled(void) __attribute__((weak));

static bool use_smap_cores(void) {
    return o0_cpu_smap_enabled != NULL && o0_cpu_smap_enabled() &&
           o0_usercopy_raw_from_user_smap != NULL &&
           o0_usercopy_raw_to_user_smap != NULL;
}

o0_status o0_user_range_check(const void *ptr, size_t len, bool write) {
    uint64_t addr = (uint64_t)(uintptr_t)ptr;
    if (len == 0) {
        return O0_OK;
    }
    if (!o0_uapi_user_window_ok(addr, len)) {
        return O0_EFAULT;
    }

    o0_as_t *as = o0_as_active();
    if (as == NULL || as == o0_as_kernel()) {
        return O0_EFAULT;
    }

    uint64_t first = O0_ALIGN_DOWN(addr, O0_PAGE_SIZE);
    uint64_t last = O0_ALIGN_DOWN(addr + len - 1u, O0_PAGE_SIZE);
    for (uint64_t page = first;; page += O0_PAGE_SIZE) {
        uint32_t flags = 0;
        if (o0_as_query(as, page, &flags, NULL) != O0_OK) {
            return O0_EFAULT;
        }
        if ((flags & O0_MAP_USER) == 0) {
            return O0_EFAULT;
        }
        if (write && (flags & O0_MAP_WRITE) == 0) {
            return O0_EFAULT;
        }
        if (page == last) {
            break;
        }
    }
    return O0_OK;
}

o0_status o0_syscall_arg_check(const void *ptr, size_t len, bool write) {
    return o0_user_range_check(ptr, len, write);
}

o0_status o0_copy_from_user(void *dst, const void *user_src, size_t len) {
    if (dst == NULL) {
        return O0_EINVAL;
    }
    if (len == 0) {
        return O0_OK;
    }
    o0_status st = o0_user_range_check(user_src, len, false);
    if (st != O0_OK) {
        return st;
    }
    long rc = use_smap_cores()
                  ? o0_usercopy_raw_from_user_smap(dst, user_src, len)
                  : o0_usercopy_raw_from_user(dst, user_src, len);
    if (rc != 0) {
        return O0_EFAULT;
    }
    return O0_OK;
}

o0_status o0_copy_to_user(void *user_dst, const void *src, size_t len) {
    if (src == NULL) {
        return O0_EINVAL;
    }
    if (len == 0) {
        return O0_OK;
    }
    o0_status st = o0_user_range_check(user_dst, len, true);
    if (st != O0_OK) {
        return st;
    }
    long rc = use_smap_cores()
                  ? o0_usercopy_raw_to_user_smap(user_dst, src, len)
                  : o0_usercopy_raw_to_user(user_dst, src, len);
    if (rc != 0) {
        return O0_EFAULT;
    }
    return O0_OK;
}

bool o0_usercopy_fixup(uint64_t fault_rip, uint64_t *fixup_rip) {
    if (fixup_rip == NULL) {
        return false;
    }
    if (fault_rip == (uint64_t)(uintptr_t)o0_usercopy_from_fault) {
        *fixup_rip = (uint64_t)(uintptr_t)o0_usercopy_from_fixup;
        return true;
    }
    if (fault_rip == (uint64_t)(uintptr_t)o0_usercopy_to_fault) {
        *fixup_rip = (uint64_t)(uintptr_t)o0_usercopy_to_fixup;
        return true;
    }
    if (o0_usercopy_from_fault_smap != NULL &&
        fault_rip == (uint64_t)(uintptr_t)o0_usercopy_from_fault_smap) {
        *fixup_rip = (uint64_t)(uintptr_t)o0_usercopy_from_fixup_smap;
        return true;
    }
    if (o0_usercopy_to_fault_smap != NULL &&
        fault_rip == (uint64_t)(uintptr_t)o0_usercopy_to_fault_smap) {
        *fixup_rip = (uint64_t)(uintptr_t)o0_usercopy_to_fixup_smap;
        return true;
    }
    return false;
}
