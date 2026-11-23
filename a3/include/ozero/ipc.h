/* Ozero kernel - objects, handles and IPC.
 *
 * A handle is an opaque, process-scoped value with rights and a generation counter. It is
 * never a kernel pointer and never a bare file descriptor. The final syscall ABI, handle
 * encoding and NT-compatible semantics are deferred to A2/A4.
 */
#ifndef O0_IPC_H
#define O0_IPC_H

#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t o0_handle_t;

#define O0_HANDLE_INVALID ((o0_handle_t)0)

enum o0_object_type {
    O0_OBJ_NONE = 0,
    O0_OBJ_PROCESS = 1,
    O0_OBJ_THREAD = 2,
    O0_OBJ_EVENT = 3,
    O0_OBJ_MUTEX = 4,
    O0_OBJ_SECTION = 5, /* shared memory */
    O0_OBJ_CHANNEL = 6,
    O0_OBJ_DEVICE = 7,
    O0_OBJ_FILE = 8,
    O0_OBJ_TYPE_MAX = 9
};

/* Rights are a bitmask; the exact set is not frozen. */
#define O0_RIGHT_READ (1u << 0)
#define O0_RIGHT_WRITE (1u << 1)
#define O0_RIGHT_EXEC (1u << 2)
#define O0_RIGHT_WAIT (1u << 3)
#define O0_RIGHT_SIGNAL (1u << 4)
#define O0_RIGHT_DUP (1u << 5)
#define O0_RIGHT_TRANSFER (1u << 6)
#define O0_RIGHT_ALL (O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_EXEC | O0_RIGHT_WAIT | \
                      O0_RIGHT_SIGNAL | O0_RIGHT_DUP | O0_RIGHT_TRANSFER)

/* Opens a handle to a kernel object in the target process's handle table. */
O0_NODISCARD o0_status o0_handle_open(o0_process_t *proc, o0_object_t *object,
                                      uint32_t rights, o0_handle_t *out);
o0_status o0_handle_close(o0_process_t *proc, o0_handle_t handle);
O0_NODISCARD o0_status o0_handle_dup(o0_process_t *proc, o0_handle_t handle,
                                     uint32_t rights, o0_handle_t *out);
o0_status o0_handle_wait(o0_handle_t handle, uint64_t timeout_ns);

/* ---- Channels: a minimal message transport for Stage 2 IPC tests ---- */

O0_NODISCARD o0_status o0_channel_create(o0_process_t *owner, o0_handle_t *endpoint_a,
                                         o0_handle_t *endpoint_b);
o0_status o0_channel_send(o0_handle_t endpoint, const void *data, size_t len);
O0_NODISCARD o0_status o0_channel_recv(o0_handle_t endpoint, void *buffer, size_t capacity,
                                       size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* O0_IPC_H */
