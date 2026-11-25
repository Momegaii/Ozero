/* Ozero kernel - IPC endpoints. See ADR 0008 section 6. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/budget.h"
#include "ozero/endpoint.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/types.h"

#include "thread_internal.h"

static void message_release(struct o0_ipc_message *message) {
    for (uint32_t i = 0; i < message->xfer_count; ++i) {
        o0_object_unref(message->xfers[i].object);
        message->xfers[i].object = NULL;
        message->xfers[i].rights = 0;
    }
    message->xfer_count = 0;
    message->len = 0;
    message->sender = NULL;
}

static void endpoint_fail_waiters(struct o0_endpoint *endpoint) {
    for (uint32_t i = 0; i < endpoint->receiver_count; ++i) {
        struct o0_thread *receiver = endpoint->receivers[i];
        endpoint->receivers[i] = NULL;
        if (receiver != NULL) {
            o0_thread_wake_result(receiver, O0_EPEER);
        }
    }
    endpoint->receiver_count = 0;
    for (uint32_t i = 0; i < O0_IPC_QUEUE_DEPTH; ++i) {
        struct o0_ipc_message *message = &endpoint->messages[i];
        struct o0_thread *sender = message->sender;
        if (sender != NULL) {
            message_release(message);
            o0_thread_wake_result(sender, O0_EPEER);
        }
    }
    endpoint->head = 0;
    endpoint->tail = 0;
    endpoint->count = 0;
}

static void endpoint_destroy(struct o0_object *object) {
    struct o0_endpoint *endpoint = (struct o0_endpoint *)object;
    endpoint->closed = true;
    endpoint_fail_waiters(endpoint);
}

O0_NODISCARD o0_status o0_endpoint_create(struct o0_budget *budget, bool reply_only,
                                          struct o0_endpoint **out) {
    if (budget == NULL || out == NULL) {
        return O0_EINVAL;
    }
    struct o0_object *object = NULL;
    o0_status st = o0_object_alloc(O0_OBJ_ENDPOINT, sizeof(struct o0_endpoint),
                                   budget, endpoint_destroy, &object);
    if (st != O0_OK) {
        return st;
    }
    struct o0_endpoint *endpoint = (struct o0_endpoint *)object;
    for (uint32_t i = 0; i < O0_IPC_QUEUE_DEPTH; ++i) {
        endpoint->messages[i].len = 0;
        endpoint->messages[i].xfer_count = 0;
        endpoint->messages[i].sender = NULL;
        endpoint->messages[i].reserved = 0;
        for (uint32_t j = 0; j < O0_IPC_MAX_HANDLES; ++j) {
            endpoint->messages[i].xfers[j].object = NULL;
            endpoint->messages[i].xfers[j].rights = 0;
            endpoint->messages[i].xfers[j].reserved = 0;
        }
    }
    for (uint32_t i = 0; i < O0_IPC_MAX_RECEIVERS; ++i) {
        endpoint->receivers[i] = NULL;
    }
    endpoint->head = 0;
    endpoint->tail = 0;
    endpoint->count = 0;
    endpoint->receiver_count = 0;
    endpoint->closed = false;
    endpoint->peer_dead = false;
    endpoint->reply_only = reply_only;
    endpoint->reserved = 0;
    *out = endpoint;
    return O0_OK;
}

void o0_endpoint_holder_gone(struct o0_endpoint *endpoint) {
    if (endpoint == NULL) {
        return;
    }
    endpoint->peer_dead = true;
    endpoint_fail_waiters(endpoint);
}
