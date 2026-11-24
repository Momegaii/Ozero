#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/abi.h"
#include "ozero/cpu_local.h"
#include "ozero/gdt.h"
#include "ozero/status.h"
#include "ozero/uapi.h"

static uint64_t access_byte(uint64_t entry) {
    return (entry >> 40) & 0xffu;
}

static int bit(uint64_t entry, unsigned index) {
    return (int)((entry >> index) & 1u);
}

static void test_selectors_and_star(void) {
    assert(O0_SEL_NULL == 0x00u);
    assert(O0_SEL_KERNEL_CODE == 0x08u);
    assert(O0_SEL_KERNEL_DATA == 0x10u);
    assert(O0_SEL_USER_DATA == 0x18u);
    assert(O0_SEL_USER_CODE == 0x20u);
    assert(O0_SEL_TSS == 0x28u);
    assert(O0_SEL_UDATA_RPL3 == 0x1bu);
    assert(O0_SEL_UCODE_RPL3 == 0x23u);

    assert(O0_ABI_STAR == 0x0010000800000000ull);
    uint64_t syscall_cs = (O0_ABI_STAR >> 32) & 0xffffu;
    uint64_t syscall_ss = syscall_cs + 8u;
    assert(syscall_cs == O0_SEL_KERNEL_CODE);
    assert(syscall_ss == O0_SEL_KERNEL_DATA);

    uint64_t sysret_base = (O0_ABI_STAR >> 48) & 0xffffu;
    assert(((sysret_base + 16u) | 3u) == O0_SEL_UCODE_RPL3);
    assert(((sysret_base + 8u) | 3u) == O0_SEL_UDATA_RPL3);

    assert(O0_ABI_FMASK == 0x700u);
    /* TF, IF and DF are exactly the bits FMASK must clear. */
    assert((O0_ABI_FMASK & 0x100u) != 0u);
    assert((O0_ABI_FMASK & 0x200u) != 0u);
    assert((O0_ABI_FMASK & 0x400u) != 0u);
}

static void test_gdt_bytes(void) {
    struct o0_gdt_view gdt;
    const uint64_t tss_base = 0x00000000deadbe00ull;
    o0_gdt_build(&gdt, tss_base, 0x67u);
    assert(O0_GDT_ENTRIES == 7u);

    assert(gdt.entries[0] == 0u);
    assert(access_byte(gdt.entries[1]) == 0x9au); /* kernel code */
    assert(bit(gdt.entries[1], 53) == 1);          /* L = 1 */
    assert(bit(gdt.entries[1], 55) == 1);          /* G = 1 */
    assert(access_byte(gdt.entries[2]) == 0x92u); /* kernel data */
    assert(access_byte(gdt.entries[3]) == 0xf2u); /* user data (DPL3) */
    assert(access_byte(gdt.entries[4]) == 0xfau); /* user code (DPL3) */
    assert(bit(gdt.entries[4], 53) == 1);
    assert(((gdt.entries[3] >> 45) & 3u) == 3u); /* DPL3 */
    assert(((gdt.entries[4] >> 45) & 3u) == 3u);

    assert(access_byte(gdt.entries[5]) == 0x89u); /* TSS type 9, present */
    assert((gdt.entries[5] & 0xffffu) == 0x67u); /* limit */
    uint64_t base_low = ((gdt.entries[5] >> 16) & 0xffffffu) |
                        (((gdt.entries[5] >> 56) & 0xffu) << 24);
    uint64_t base = base_low | (gdt.entries[6] << 32);
    assert(base == tss_base);
}

static void test_tss_offsets(void) {
    assert(O0_TSS_RSP0_OFFSET == offsetof(struct o0_tss, rsp0));
    assert(O0_TSS_IST1_OFFSET == offsetof(struct o0_tss, ist1));
    assert(sizeof(struct o0_tss) == 104u);
    assert(offsetof(struct o0_cpu_local, self) == 0u);
    assert(offsetof(struct o0_cpu_local, kernel_rsp0) == 8u);
}

static void test_error_table(void) {
    assert(o0_uapi_error_from_status(O0_OK) == 0);
    int64_t expect = -1;
    for (int i = O0_EINVAL; i <= O0_ENOSPC; ++i) {
        int64_t mapped = o0_uapi_error_from_status((o0_status)i);
        assert(mapped == expect);
        expect -= 1;
    }
    assert(expect == -19);
    assert(o0_uapi_error_from_status(O0_EFAULT) == O0_UE_FAULT);
    assert(o0_uapi_error_from_status(O0_ENOSYS) == O0_UE_NOSYS);
}

static void test_user_layout(void) {
    /* Null guard is never a valid user range. */
    assert(o0_uapi_user_window_ok(0, 1) == 0);
    assert(o0_uapi_user_window_ok(O0_USER_NULL_GUARD_END - 1u, 1) == 0);
    assert(o0_uapi_user_window_ok(O0_USER_NULL_GUARD_END, 1) == 1);
    /* Kernel half is rejected. */
    assert(o0_uapi_user_window_ok(O0_USER_LIMIT, 1) == 0);
    assert(o0_uapi_user_window_ok(O0_USER_LIMIT - 1u, 1) == 1);
    /* Overflow is rejected. */
    assert(o0_uapi_user_window_ok(O0_USER_LIMIT - 4u, 8) == 0);
    /* Zero length at the limit base is still in-window (nothing is read). */
    assert(o0_uapi_user_window_ok(O0_USER_LIMIT - 4u, 4) == 1);
    /* Stack window sits above the image window and below the limit. */
    assert(O0_USER_STACK_BASE > O0_USER_IMAGE_END);
    assert(O0_USER_STACK_TOP <= O0_USER_LIMIT);
    assert(O0_USER_STACK_TOP - O0_USER_STACK_BASE ==
           (uint64_t)O0_USER_STACK_PAGES * 4096u);
}

int main(void) {
    test_selectors_and_star();
    test_gdt_bytes();
    test_tss_offsets();
    test_error_table();
    test_user_layout();
    return 0;
}
