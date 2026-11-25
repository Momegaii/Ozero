/* Ozero kernel - handles and synchronous IPC.
 *
 * A handle is an opaque, process-scoped value with rights and a generation
 * counter. It is never a kernel pointer. See ADR 0008 and object.h/handle.h.
 */
#ifndef O0_IPC_H
#define O0_IPC_H

#include <stdint.h>

#include "ozero/endpoint.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/uapi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t o0_handle_t;

#define O0_HANDLE_INVALID ((o0_handle_t)0)

struct o0_process;

/* Each core validates the user pointers with o0_syscall_arg_check and moves
 * bytes with o0_copy_*; the caller only resolves the endpoint handle. */
int64_t o0_ipc_send(struct o0_process *proc, struct o0_endpoint *endpoint,
                    const void *user_buf, uint64_t len,
                    const struct o0_uapi_handle_xfer *user_xfers, uint32_t nxfers);
int64_t o0_ipc_recv(struct o0_process *proc, struct o0_endpoint *endpoint,
                    void *user_buf, uint64_t maxlen, uint64_t *user_handles,
                    uint32_t max_handles, uint32_t *user_count);
int64_t o0_ipc_call(struct o0_process *proc, struct o0_endpoint *endpoint,
                    const void *user_send, uint64_t send_len, void *user_recv,
                    uint64_t recv_len);

#ifdef __cplusplus
}
#endif

#endif /* O0_IPC_H */
