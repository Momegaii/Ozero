/* Ozero kernel - synchronous IPC cores. See ADR 0008 section 6. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/abi.h"
#include "ozero/endpoint.h"
#include "ozero/handle.h"
#include "ozero/ipc.h"
#include "ozero/irq.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/uaccess.h"
#include "ozero/uapi.h"

#include "thread_internal.h"

static void receiver_remove(struct o0_endpoint *endpoint, struct o0_thread *thread) {
    for (uint32_t i = 0; i < endpoint->receiver_count; ++i) {
        if (endpoint->receivers[i] == thread) {
            endpoint->receivers[i] =
                endpoint->receivers[endpoint->receiver_count - 1u];
            endpoint->receivers[endpoint->receiver_count - 1u] = NULL;
            --endpoint->receiver_count;
            return;
        }
    }
}

static struct o0_thread *receiver_pop(struct o0_endpoint *endpoint) {
    if (endpoint->receiver_count == 0) {
        return NULL;
    }
    struct o0_thread *thread = endpoint->receivers[0];
    for (uint32_t i = 1; i < endpoint->receiver_count; ++i) {
        endpoint->receivers[i - 1u] = endpoint->receivers[i];
    }
    endpoint->receivers[endpoint->receiver_count - 1u] = NULL;
    --endpoint->receiver_count;
    return thread;
}

static bool endpoint_gone(const struct o0_endpoint *endpoint) {
    return endpoint->closed || endpoint->peer_dead;
}

static int64_t ipc_send_core(struct o0_process *proc,
                             struct o0_endpoint *endpoint, const uint8_t *data,
                             uint64_t len,
                             const struct o0_uapi_handle_xfer *xfers,
                             uint32_t nxfers) {
    if (proc == NULL || endpoint == NULL) {
        return O0_UE_INVAL;
    }
    if (len > O0_IPC_MAX_MESSAGE || nxfers > O0_IPC_MAX_HANDLES) {
        return O0_UE_RANGE;
    }
    if (endpoint->reply_only && nxfers != 0) {
        return O0_UE_PERM;
    }

    for (uint32_t i = 0; i < nxfers; ++i) {
        struct o0_object *object = NULL;
        uint32_t entry_rights = 0;
        o0_status st = o0_handle_lookup_any(proc->handles, xfers[i].handle, &object,
                                            &entry_rights);
        if (st != O0_OK) {
            return o0_uapi_error_from_status(st);
        }
        if ((entry_rights & O0_RIGHT_TRANSFER) == 0) {
            return O0_UE_PERM;
        }
        uint32_t requested = xfers[i].rights != 0 ? xfers[i].rights : entry_rights;
        if ((requested & ~entry_rights) != 0) {
            return O0_UE_PERM;
        }
        for (uint32_t j = 0; j < i; ++j) {
            if (xfers[j].handle == xfers[i].handle) {
                return O0_UE_INVAL;
            }
        }
    }

    struct o0_thread *self = o0_sched_current();
    if (self == NULL) {
        return O0_UE_INVAL;
    }

    o0_irq_state_t irq = o0_irq_save();
    if (endpoint_gone(endpoint)) {
        o0_irq_restore(irq);
        return O0_UE_PEER;
    }
    if (endpoint->count >= O0_IPC_QUEUE_DEPTH) {
        o0_irq_restore(irq);
        return O0_UE_AGAIN;
    }

    struct o0_ipc_message *message = &endpoint->messages[endpoint->tail];
    for (uint32_t i = 0; i < len; ++i) {
        message->data[i] = data[i];
    }
    message->len = (size_t)len;
    message->xfer_count = 0;
    message->sender = self;

    for (uint32_t i = 0; i < nxfers; ++i) {
        struct o0_object *object = NULL;
        uint32_t entry_rights = 0;
        o0_status st = o0_handle_take(proc->handles, xfers[i].handle, O0_OBJ_NONE,
                                      O0_RIGHT_TRANSFER, &object, &entry_rights);
        if (st != O0_OK) {
            for (uint32_t j = 0; j < i; ++j) {
                uint64_t ignored = 0;
                o0_status restore = o0_handle_install_owned(
                    proc->handles, message->xfers[j].object,
                    message->xfers[j].rights, &ignored);
                (void)restore;
                message->xfers[j].object = NULL;
            }
            message->xfer_count = 0;
            message->sender = NULL;
            message->len = 0;
            o0_irq_restore(irq);
            return o0_uapi_error_from_status(st);
        }
        uint32_t requested = xfers[i].rights != 0 ? xfers[i].rights : entry_rights;
        message->xfers[i].object = object;
        message->xfers[i].rights = requested;
        message->xfer_count = i + 1u;
    }

    endpoint->tail = (endpoint->tail + 1u) % O0_IPC_QUEUE_DEPTH;
    ++endpoint->count;

    struct o0_thread *receiver = receiver_pop(endpoint);
    if (receiver != NULL) {
        o0_thread_wake_result(receiver, 0);
    }

    self->wake_status = 0;
    o0_sched_block(O0_BLOCK_IPC);
    o0_irq_restore(irq);

    int64_t result = o0_thread_block_result();
    return result < 0 ? result : 0;
}

int64_t o0_ipc_send(struct o0_process *proc, struct o0_endpoint *endpoint,
                    const void *user_buf, uint64_t len,
                    const struct o0_uapi_handle_xfer *user_xfers, uint32_t nxfers) {
    if (proc == NULL || endpoint == NULL) {
        return O0_UE_INVAL;
    }
    if (len > O0_IPC_MAX_MESSAGE || nxfers > O0_IPC_MAX_HANDLES) {
        return O0_UE_RANGE;
    }
    uint8_t data[O0_IPC_MAX_MESSAGE];
    if (len != 0) {
        o0_status st = o0_copy_from_user(data, user_buf, (size_t)len);
        if (st != O0_OK) {
            return o0_uapi_error_from_status(st);
        }
    }
    struct o0_uapi_handle_xfer xfers[O0_IPC_MAX_HANDLES];
    if (nxfers != 0) {
        o0_status st = o0_copy_from_user(xfers, user_xfers,
                                         nxfers * sizeof(xfers[0]));
        if (st != O0_OK) {
            return o0_uapi_error_from_status(st);
        }
    }
    return ipc_send_core(proc, endpoint, data, len, xfers, nxfers);
}

static int64_t ipc_recv_core(struct o0_process *proc,
                             struct o0_endpoint *endpoint, void *user_buf,
                             uint64_t maxlen, uint64_t *user_handles,
                             uint32_t max_handles, uint32_t *user_count,
                             bool user_output) {
    if (proc == NULL || endpoint == NULL || user_count == NULL) {
        return O0_UE_INVAL;
    }
    if (maxlen > O0_IPC_MAX_MESSAGE || max_handles > O0_IPC_MAX_HANDLES) {
        return O0_UE_RANGE;
    }
    o0_status st = O0_OK;
    if (user_output) {
        st = o0_syscall_arg_check(user_count, sizeof(*user_count), true);
        if (st != O0_OK) {
            return o0_uapi_error_from_status(st);
        }
    }
    if (maxlen != 0) {
        st = o0_syscall_arg_check(user_buf, (size_t)maxlen, true);
        if (st != O0_OK) {
            return o0_uapi_error_from_status(st);
        }
    }
    if (max_handles != 0) {
        st = o0_syscall_arg_check(user_handles,
                                  max_handles * sizeof(uint64_t), true);
        if (st != O0_OK) {
            return o0_uapi_error_from_status(st);
        }
    }

    struct o0_thread *self = o0_sched_current();
    if (self == NULL) {
        return O0_UE_INVAL;
    }

    for (;;) {
        o0_irq_state_t irq = o0_irq_save();
        receiver_remove(endpoint, self);

        if (endpoint->count == 0 && endpoint_gone(endpoint)) {
            o0_irq_restore(irq);
            return O0_UE_PEER;
        }
        if (endpoint->count != 0) {
            struct o0_ipc_message *message = &endpoint->messages[endpoint->head];
            if (message->len > maxlen) {
                o0_irq_restore(irq);
                return O0_UE_RANGE;
            }
            if (message->xfer_count > max_handles) {
                o0_irq_restore(irq);
                return O0_UE_NOMEM;
            }
            if (o0_handle_free_slots(proc->handles) < message->xfer_count) {
                o0_irq_restore(irq);
                return O0_UE_NOMEM;
            }
            if (message->len != 0) {
                st = o0_copy_to_user(user_buf, message->data, message->len);
                if (st != O0_OK) {
                    o0_irq_restore(irq);
                    return o0_uapi_error_from_status(st);
                }
            }
            uint64_t installed[O0_IPC_MAX_HANDLES];
            uint32_t installed_count = 0;
            for (uint32_t i = 0; i < message->xfer_count; ++i) {
                st = o0_handle_install_owned(proc->handles, message->xfers[i].object,
                                             message->xfers[i].rights,
                                             &installed[i]);
                if (st != O0_OK) {
                    for (uint32_t j = 0; j < installed_count; ++j) {
                        o0_status closed = o0_handle_close(proc->handles, installed[j]);
                        (void)closed;
                    }
                    o0_irq_restore(irq);
                    return o0_uapi_error_from_status(st);
                }
                message->xfers[i].object = NULL;
                ++installed_count;
            }
            if (message->xfer_count != 0) {
                st = o0_copy_to_user(user_handles, installed,
                                     message->xfer_count * sizeof(uint64_t));
                if (st != O0_OK) {
                    for (uint32_t i = 0; i < installed_count; ++i) {
                        o0_status closed = o0_handle_close(proc->handles, installed[i]);
                        (void)closed;
                    }
                    o0_irq_restore(irq);
                    return o0_uapi_error_from_status(st);
                }
            }
            uint32_t count = message->xfer_count;
            if (user_output) {
                st = o0_copy_to_user(user_count, &count, sizeof(count));
            } else {
                *user_count = count;
            }
            if (st != O0_OK) {
                for (uint32_t i = 0; i < installed_count; ++i) {
                    o0_status closed = o0_handle_close(proc->handles, installed[i]);
                        (void)closed;
                }
                o0_irq_restore(irq);
                return o0_uapi_error_from_status(st);
            }

            uint64_t message_len = message->len;
            struct o0_thread *sender = message->sender;
            message->sender = NULL;
            message->len = 0;
            message->xfer_count = 0;
            endpoint->head = (endpoint->head + 1u) % O0_IPC_QUEUE_DEPTH;
            --endpoint->count;
            if (sender != NULL) {
                o0_thread_wake_result(sender, 0);
            }
            o0_irq_restore(irq);
            return (int64_t)message_len;
        }

        if (endpoint->receiver_count >= O0_IPC_MAX_RECEIVERS) {
            o0_irq_restore(irq);
            return O0_UE_AGAIN;
        }
        endpoint->receivers[endpoint->receiver_count++] = self;
        self->wake_status = 0;
        o0_sched_block(O0_BLOCK_IPC);
        o0_irq_restore(irq);

        int64_t wake = o0_thread_block_result();
        if (wake < 0) {
            return wake;
        }
    }
}

int64_t o0_ipc_recv(struct o0_process *proc, struct o0_endpoint *endpoint,
                    void *user_buf, uint64_t maxlen, uint64_t *user_handles,
                    uint32_t max_handles, uint32_t *user_count) {
    return ipc_recv_core(proc, endpoint, user_buf, maxlen, user_handles,
                         max_handles, user_count, true);
}

int64_t o0_ipc_call(struct o0_process *proc, struct o0_endpoint *endpoint,
                    const void *user_send, uint64_t send_len, void *user_recv,
                    uint64_t recv_len) {
    if (proc == NULL || endpoint == NULL) {
        return O0_UE_INVAL;
    }
    struct o0_endpoint *reply = NULL;
    o0_status st = o0_endpoint_create(proc->budget, true, &reply);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }

    uint64_t reply_handle = 0;
    st = o0_handle_install(proc->handles, &reply->obj,
                           O0_RIGHT_SEND | O0_RIGHT_RECV | O0_RIGHT_WAIT |
                               O0_RIGHT_TRANSFER,
                           &reply_handle);
    if (st != O0_OK) {
        o0_object_unref(&reply->obj);
        return o0_uapi_error_from_status(st);
    }
    struct o0_uapi_handle_xfer xfer;
    xfer.handle = reply_handle;
    xfer.rights = O0_RIGHT_SEND;
    xfer.reserved = 0;

    if (send_len > O0_IPC_MAX_MESSAGE) {
        o0_object_unref(&reply->obj);
        return O0_UE_RANGE;
    }
    uint8_t data[O0_IPC_MAX_MESSAGE];
    if (send_len != 0) {
        o0_status copy = o0_copy_from_user(data, user_send, (size_t)send_len);
        if (copy != O0_OK) {
            o0_object_unref(&reply->obj);
            return o0_uapi_error_from_status(copy);
        }
    }
    int64_t sent = ipc_send_core(proc, endpoint, data, send_len, &xfer, 1);
    if (sent < 0) {
        o0_object_unref(&reply->obj);
        return sent;
    }

    uint32_t count = 0;
    int64_t received = ipc_recv_core(proc, reply, user_recv, recv_len, NULL, 0,
                                     &count, false);
    o0_object_unref(&reply->obj);
    return received;
}
