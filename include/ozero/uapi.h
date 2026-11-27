/* Ozero kernel - native user-facing ABI (frozen).
 *
 * This header is the contract between native Ozero user programs and the
 * kernel. It is deliberately *not* a POSIX or NT header: the syscall numbers,
 * error codes and address-space layout are Ozero's own and their numbering is
 * independent of any other system. See docs/decisions/0007-user-abi-syscall-and-executable.md.
 *
 * Changing any value here is an ABI break and requires a new decision record.
 * A future Win32/PE layer performs its own translation in user space and must
 * not reuse these numbers.
 */
#ifndef O0_UAPI_H
#define O0_UAPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Syscall numbers (append-only native ABI; Wave 4.6 additions) ---- */

#define O0_SYS_INVALID 0u /* reserved: returns -O0_UE_INVAL */
#define O0_SYS_EXIT 1u
#define O0_SYS_YIELD 2u
#define O0_SYS_DEBUG_WRITE 3u
#define O0_SYS_GETPID 4u
#define O0_SYS_HANDLE_CLOSE 5u
#define O0_SYS_HANDLE_DUP 6u
#define O0_SYS_HANDLE_INFO 7u
#define O0_SYS_BUDGET_SPLIT 8u
#define O0_SYS_BUDGET_INFO 9u
#define O0_SYS_ENDPOINT_CREATE 10u
#define O0_SYS_IPC_SEND 11u
#define O0_SYS_IPC_RECV 12u
#define O0_SYS_IPC_CALL 13u
#define O0_SYS_MEMORY_CREATE 14u
#define O0_SYS_MEMORY_MAP 15u
#define O0_SYS_PROCESS_SPAWN 16u
#define O0_SYS_FILE_OPEN 17u
#define O0_SYS_FILE_READ 18u
#define O0_SYS_FILE_WRITE 19u
#define O0_SYS_FILE_STAT 20u
#define O0_SYS_FILE_READDIR 21u
#define O0_SYS_MAX 21u

/* ---- User-visible error codes ----
 *
 * `rax < 0` is the single failure test. Values are Ozero-native and dense:
 * exactly one code per kernel o0_status, mapped in one place
 * (o0_uapi_error_from_status). 0 is success.
 */
#define O0_UE_INVAL (-1)
#define O0_UE_NOMEM (-2)
#define O0_UE_NOENT (-3)
#define O0_UE_BUSY (-4)
#define O0_UE_PERM (-5)
#define O0_UE_NOSYS (-6)
#define O0_UE_FAULT (-7)
#define O0_UE_IO (-8)
#define O0_UE_RANGE (-9)
#define O0_UE_TIMEDOUT (-10)
#define O0_UE_AGAIN (-11)
#define O0_UE_OVERFLOW (-12)
#define O0_UE_NOTSUP (-13)
#define O0_UE_EXIST (-14)
#define O0_UE_NOTDIR (-15)
#define O0_UE_ISDIR (-16)
#define O0_UE_NOTEMPTY (-17)
#define O0_UE_NOSPC (-18)
#define O0_UE_PEER (-19) /* endpoint or process peer is gone */

/* ---- Wave 3 argument structures ---- */

/* A handle transfer entry for ipc_send and process_spawn. `rights` must be a
 * subset of the rights on `handle`; 0 means "same rights as the handle". */
struct o0_uapi_handle_xfer {
    uint64_t handle;
    uint32_t rights;
    uint32_t reserved;
};

struct o0_uapi_handle_info {
    uint64_t handle;
    uint32_t type;
    uint32_t rights;
    uint64_t charge_bytes;
    uint64_t reserved;
};

struct o0_uapi_budget_info {
    uint64_t limit;
    uint64_t used;
    uint64_t remaining;
    uint64_t reserved;
};

/* Protection bits accepted by memory_map. They match O0_MAP_READ/WRITE/EXEC
 * numerically and are re-validated against the memory handle's rights. */
#define O0_PROT_READ 1u
#define O0_PROT_WRITE 2u
#define O0_PROT_EXEC 4u

/* ---- Wave 4.6 file metadata ---- */

#define O0_UAPI_PATH_MAX 256u
#define O0_UAPI_FILE_IO_MAX 4096u
#define O0_UAPI_VNODE_FILE 1u
#define O0_UAPI_VNODE_DIRECTORY 2u
#define O0_UAPI_FILE_CREATE (1u << 0)
#define O0_UAPI_FILE_EXCLUSIVE (1u << 1)

/* Native capability rights used by file_open and handle operations. */
#define O0_UAPI_RIGHT_READ (1u << 0)
#define O0_UAPI_RIGHT_WRITE (1u << 1)
#define O0_UAPI_RIGHT_DUP (1u << 5)
#define O0_UAPI_RIGHT_TRANSFER (1u << 4)
#define O0_UAPI_RIGHT_INFO (1u << 9)
#define O0_UAPI_RIGHT_ENUM (1u << 11)
#define O0_UAPI_RIGHT_CREATE (1u << 12)
#define O0_UAPI_RIGHT_REMOVE (1u << 13)

struct o0_uapi_vfs_stat {
    uint64_t size;
    uint64_t object_id;
    uint32_t type;
    uint32_t flags;
};

struct o0_uapi_vfs_dirent {
    uint64_t next_cursor;
    uint64_t object_id;
    uint64_t size;
    uint32_t type;
    uint32_t name_len;
    char name[64];
};

/* ---- Segment selectors (GDT layout) ---- */

#define O0_SEL_NULL 0x00u
#define O0_SEL_KERNEL_CODE 0x08u
#define O0_SEL_KERNEL_DATA 0x10u
#define O0_SEL_USER_DATA 0x18u
#define O0_SEL_USER_CODE 0x20u
#define O0_SEL_TSS 0x28u
#define O0_SEL_UDATA_RPL3 (O0_SEL_USER_DATA | 3u) /* 0x1B */
#define O0_SEL_UCODE_RPL3 (O0_SEL_USER_CODE | 3u) /* 0x23 */

/* STAR = (SYSRET base << 48) | (SYSCALL CS << 32).
 * SYSCALL: CS = 0x08, SS = 0x10.
 * SYSRET:  CS = 0x10 + 16 | 3 = 0x23, SS = 0x10 + 8 | 3 = 0x1B. */
#define O0_ABI_STAR (((uint64_t)0x10u << 48) | ((uint64_t)0x08u << 32))
/* FMASK clears TF (0x100), IF (0x200) and DF (0x400). */
#define O0_ABI_FMASK ((uint64_t)0x700u)
#define O0_ABI_USER_RFLAGS ((uint64_t)0x202u)

/* ---- TSS offsets (64-bit TSS, Intel SDM Vol. 3A) ---- */
#define O0_TSS_RSP0_OFFSET 0x04u
#define O0_TSS_IST1_OFFSET 0x24u

/* ---- User address space layout (ADR 0007 §2.6) ---- */

#define O0_USER_LIMIT 0x0000800000000000ull /* first non-canonical / kernel half */
#define O0_USER_NULL_GUARD_END 0x0000000000010000ull /* 64 KiB null guard */
#define O0_USER_IMAGE_START 0x0000000040000000ull /* 1 GiB */
#define O0_USER_IMAGE_END 0x000000007ffe0000ull /* one past the image window */
#define O0_USER_STACK_TOP 0x000000007fff0000ull
#define O0_USER_STACK_PAGES 4u
#define O0_USER_STACK_BASE \
    (O0_USER_STACK_TOP - (uint64_t)O0_USER_STACK_PAGES * 0x1000ull)

/* Null guard check: true when [addr, addr+len) is entirely inside the user
 * window, above the null guard, and cannot overflow. Page-table presence is a
 * separate check performed by the kernel with the active address space. */
static inline int o0_uapi_user_window_ok(uint64_t addr, uint64_t len) {
    if (addr < O0_USER_NULL_GUARD_END) {
        return 0;
    }
    if (addr >= O0_USER_LIMIT) {
        return 0;
    }
    if (len > O0_USER_LIMIT - addr) {
        return 0;
    }
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* O0_UAPI_H */
