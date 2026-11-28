/* Ozero kernel - ABI helper implementations (host-testable).
 *
 * Deliberately free of inline assembly so tests/abi_test.c can assert the
 * descriptor bytes, selector arithmetic and error table on the build host.
 */

#include <stddef.h>
#include <stdint.h>

#include "ozero/abi.h"
#include "ozero/cpu_local.h"
#include "ozero/status.h"
#include "ozero/uapi.h"

_Static_assert(O0_CPU_LOCAL_OFFSET_SELF == 0u, "cpu_local.self offset");
_Static_assert(O0_CPU_LOCAL_OFFSET_KERNEL_RSP0 == 8u, "cpu_local.kernel_rsp0 offset");
_Static_assert(O0_CPU_LOCAL_OFFSET_CURRENT == 16u, "cpu_local.current offset");
_Static_assert(O0_CPU_LOCAL_OFFSET_USER_RSP == 24u, "cpu_local.user_rsp offset");
_Static_assert(O0_CPU_LOCAL_OFFSET_USERCOPY_FIXUP == 32u, "cpu_local fixup offset");
_Static_assert(O0_CPU_LOCAL_OFFSET_USERCOPY_EXPECTED == 40u, "cpu_local expected offset");
_Static_assert(O0_CPU_LOCAL_OFFSET_TSS_READY == 128u, "cpu_local.tss_ready offset");
_Static_assert(offsetof(struct o0_cpu_local, self) == 0u, "self is first");
_Static_assert(offsetof(struct o0_cpu_local, kernel_rsp0) == 8u, "kstack offset");
_Static_assert(offsetof(struct o0_cpu_local, current) == 16u, "current offset");
_Static_assert(offsetof(struct o0_cpu_local, user_rsp) == 24u, "user rsp offset");
_Static_assert(offsetof(struct o0_cpu_local, usercopy_fixup) == 32u, "fixup offset");
_Static_assert(offsetof(struct o0_cpu_local, usercopy_expected) == 40u, "expected offset");
_Static_assert(offsetof(struct o0_cpu_local, tss_ready) == 128u, "tss_ready offset");

int64_t o0_uapi_error_from_status(o0_status status) {
    switch (status) {
        case O0_OK: return 0;
        case O0_EINVAL: return O0_UE_INVAL;
        case O0_ENOMEM: return O0_UE_NOMEM;
        case O0_ENOENT: return O0_UE_NOENT;
        case O0_EBUSY: return O0_UE_BUSY;
        case O0_EPERM: return O0_UE_PERM;
        case O0_ENOSYS: return O0_UE_NOSYS;
        case O0_EFAULT: return O0_UE_FAULT;
        case O0_EIO: return O0_UE_IO;
        case O0_ERANGE: return O0_UE_RANGE;
        case O0_ETIMEDOUT: return O0_UE_TIMEDOUT;
        case O0_EAGAIN: return O0_UE_AGAIN;
        case O0_EOVERFLOW: return O0_UE_OVERFLOW;
        case O0_ENOTSUP: return O0_UE_NOTSUP;
        case O0_EEXIST: return O0_UE_EXIST;
        case O0_ENOTDIR: return O0_UE_NOTDIR;
        case O0_EISDIR: return O0_UE_ISDIR;
        case O0_ENOTEMPTY: return O0_UE_NOTEMPTY;
        case O0_ENOSPC: return O0_UE_NOSPC;
        case O0_EPEER: return O0_UE_PEER;
        default: return O0_UE_INVAL;
    }
    return O0_UE_INVAL;
}

static uint64_t gdt_entry(uint32_t access, uint32_t flags, uint32_t limit,
                          uint32_t base_low, uint32_t base_mid, uint32_t base_high) {
    uint64_t e = 0;
    e |= (uint64_t)(limit & 0xffffu);
    e |= (uint64_t)(base_low & 0xffffffu) << 16;
    e |= (uint64_t)(access & 0xffu) << 40;
    e |= (uint64_t)((limit >> 16) & 0xfu) << 48;
    e |= (uint64_t)(flags & 0xfu) << 52;
    e |= (uint64_t)(base_mid & 0xffu) << 56;
    (void)base_high;
    return e;
}

void o0_gdt_build(struct o0_gdt_view *out, uint64_t tss_base, uint32_t tss_limit) {
    if (out == NULL) {
        return;
    }
    /* Kernel code: P|DPL0|S|RX = 0x9A, G=1, L=1 (flags 0xA). */
    out->entries[0] = 0;
    out->entries[1] = gdt_entry(0x9au, 0xau, 0xfffffu, 0, 0, 0);
    /* Kernel data: P|DPL0|S|RW = 0x92, G=1, D/B=1 (flags 0xC). */
    out->entries[2] = gdt_entry(0x92u, 0xcu, 0xfffffu, 0, 0, 0);
    /* User data: P|DPL3|S|RW = 0xF2. */
    out->entries[3] = gdt_entry(0xf2u, 0xcu, 0xfffffu, 0, 0, 0);
    /* User code: P|DPL3|S|RX = 0xFA, G=1, L=1. */
    out->entries[4] = gdt_entry(0xfau, 0xau, 0xfffffu, 0, 0, 0);
    /* TSS: available 64-bit TSS, P|DPL0|type9 = 0x89, no flags. */
    uint64_t tss = gdt_entry(0x89u, 0x0u, tss_limit & 0xfffffu,
                             (uint32_t)(tss_base & 0xffffffu),
                             (uint32_t)((tss_base >> 24) & 0xffu),
                             0);
    out->entries[5] = tss;
    out->entries[6] = tss_base >> 32;
}
