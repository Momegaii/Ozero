/* Ozero kernel - common types and conventions.
 *
 * This header is a Stage 0 interface sketch. Signatures are not a frozen ABI.
 */
#ifndef O0_TYPES_H
#define O0_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define O0_NORETURN __attribute__((noreturn))
#define O0_NODISCARD __attribute__((warn_unused_result))
#define O0_PACKED __attribute__((packed))
#define O0_ALIGN(n) __attribute__((aligned(n)))
#else
#define O0_NORETURN
#define O0_NODISCARD
#define O0_PACKED
#define O0_ALIGN(n)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t o0_paddr_t;
typedef uint64_t o0_vaddr_t;

#define O0_PAGE_SHIFT 12u
#define O0_PAGE_SIZE ((size_t)1u << O0_PAGE_SHIFT)
#define O0_PAGE_MASK (~(o0_paddr_t)(O0_PAGE_SIZE - 1u))

/* Alignment helpers. The mask arithmetic is done in o0_paddr_t (64-bit) so a
 * 32-bit alignment argument cannot truncate the high half of a 64-bit value:
 * ~((a) - 1u) with a plain int would otherwise yield 0x00000000fffffff0 and
 * clear bits 32..63 of a pointer. */
#define O0_ALIGN_UP(x, a)                                                  \
    (((x) + ((o0_paddr_t)(a) - 1u)) & ~((o0_paddr_t)(a) - 1u))
#define O0_ALIGN_DOWN(x, a) ((x) & ~((o0_paddr_t)(a) - 1u))
#define O0_IS_ALIGNED(x, a) (((x) & ((o0_paddr_t)(a) - 1u)) == 0u)

#define O0_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define O0_MIN(a, b) ((a) < (b) ? (a) : (b))
#define O0_MAX(a, b) ((a) > (b) ? (a) : (b))

#define O0_CACHE_LINE_MIN 64u

/* Opaque kernel objects. Definitions live in the owning subsystem. */
typedef struct o0_as o0_as_t;
typedef struct o0_process o0_process_t;
typedef struct o0_thread o0_thread_t;
typedef struct o0_object o0_object_t;
typedef struct o0_device o0_device_t;

#ifdef __cplusplus
}
#endif

#endif /* O0_TYPES_H */
