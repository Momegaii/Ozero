/* Ozero kernel - IPC endpoints. See ADR 0008 section 6. */
#ifndef O0_ENDPOINT_H
#define O0_ENDPOINT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/budget.h"
#include "ozero/object.h"
#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_IPC_MAX_MESSAGE 64u
#define O0_IPC_MAX_HANDLES 4u
#define O0_IPC_QUEUE_DEPTH 8u
#define O0_IPC_MAX_RECEIVERS 8u

struct o0_thread;

struct o0_ipc_xfer {
    struct o0_object *object;
    uint32_t rights;
    uint32_t reserved;
};

struct o0_ipc_message {
    uint8_t data[O0_IPC_MAX_MESSAGE];
    size_t len;
    struct o0_ipc_xfer xfers[O0_IPC_MAX_HANDLES];
    uint32_t xfer_count;
    uint32_t reserved;
    struct o0_thread *sender;
};

struct o0_endpoint {
    struct o0_object obj;
    struct o0_ipc_message messages[O0_IPC_QUEUE_DEPTH];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    struct o0_thread *receivers[O0_IPC_MAX_RECEIVERS];
    uint32_t receiver_count;
    bool closed;
    bool peer_dead;
    bool reply_only;
    uint32_t reserved;
};

O0_NODISCARD o0_status o0_endpoint_create(struct o0_budget *budget, bool reply_only,
                                          struct o0_endpoint **out);
/* Reports that a process which held this endpoint has gone away. Blocked peers
 * are woken with O0_EPEER; queued messages release their references. */
void o0_endpoint_holder_gone(struct o0_endpoint *endpoint);

#ifdef __cplusplus
}
#endif

#endif /* O0_ENDPOINT_H */
