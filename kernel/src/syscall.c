/* Ozero kernel - native syscall dispatcher (Wave 3 set).
 *
 * Every buffer argument goes through o0_copy_from_user / o0_copy_to_user; every
 * handle argument is resolved through the caller's handle table with an
 * explicit type and rights mask. The kernel never dereferences a user pointer
 * here.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/abi.h"
#include "ozero/budget.h"
#include "ozero/endpoint.h"
#include "ozero/handle.h"
#include "ozero/ipc.h"
#include "ozero/memory.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/syscall.h"
#include "ozero/thread.h"
#include "ozero/types.h"
#include "ozero/uaccess.h"
#include "ozero/uapi.h"
#include "ozero/user.h"
#include "ozero/vfs.h"

#include "serial.h"
#include "thread_internal.h"
#include "string.h"

#define O0_DEBUG_WRITE_MAX 4096u
#define O0_DEBUG_CHUNK 128u

static int64_t ret_status(o0_status status) {
    return status == O0_OK ? 0 : o0_uapi_error_from_status(status);
}

static int64_t sys_debug_write(const void *buf, uint64_t len) {
    if (len > O0_DEBUG_WRITE_MAX) {
        return O0_UE_INVAL;
    }
    char chunk[O0_DEBUG_CHUNK];
    uint64_t done = 0;
    while (done < len) {
        size_t take = (size_t)O0_MIN((uint64_t)O0_DEBUG_CHUNK, len - done);
        o0_status st = o0_copy_from_user(chunk, (const char *)buf + done, take);
        if (st != O0_OK) {
            return done > 0 ? (int64_t)done : o0_uapi_error_from_status(st);
        }
        for (size_t i = 0; i < take; ++i) {
            o0_serial_putc(chunk[i]);
        }
        done += take;
    }
    return (int64_t)done;
}

static int64_t sys_getpid(void) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_NOENT;
    }
    return (int64_t)proc->pid;
}

static int64_t sys_handle_close(uint64_t handle) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    struct o0_object *object = NULL;
    o0_status st = o0_handle_lookup_any(proc->handles, handle, &object, NULL);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    if (object->type == O0_OBJ_ENDPOINT) {
        o0_endpoint_holder_gone((struct o0_endpoint *)object);
    }
    return ret_status(o0_handle_close(proc->handles, handle));
}

static int64_t sys_handle_dup(uint64_t handle, uint32_t rights) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    uint64_t duplicate = 0;
    o0_status st = o0_handle_dup(proc->handles, handle, rights, &duplicate);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    return (int64_t)duplicate;
}

static int64_t sys_handle_info(uint64_t handle, struct o0_uapi_handle_info *user_out) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    o0_status st = o0_syscall_arg_check(user_out, sizeof(*user_out), true);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    struct o0_handle_info info;
    st = o0_handle_info(proc->handles, handle, &info);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    struct o0_uapi_handle_info out = {
        .handle = info.handle,
        .type = info.type,
        .rights = info.rights,
        .charge_bytes = info.charge_bytes,
        .reserved = 0,
    };
    st = o0_copy_to_user(user_out, &out, sizeof(out));
    return ret_status(st);
}

static int64_t sys_budget_split(uint64_t handle, uint64_t amount) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    struct o0_object *object = NULL;
    o0_status st = o0_handle_lookup(proc->handles, handle, O0_OBJ_BUDGET,
                                    O0_RIGHT_SPLIT, &object);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    struct o0_budget *child = NULL;
    st = o0_budget_split((struct o0_budget *)object, amount, &child);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    uint64_t child_handle = 0;
    st = o0_handle_install(proc->handles, &child->obj, O0_RIGHT_BUDGET_DEFAULT,
                           &child_handle);
    if (st != O0_OK) {
        o0_object_unref(&child->obj);
        return o0_uapi_error_from_status(st);
    }
    o0_object_unref(&child->obj);
    return (int64_t)child_handle;
}

static int64_t sys_budget_info(uint64_t handle, struct o0_uapi_budget_info *user_out) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    o0_status st = o0_syscall_arg_check(user_out, sizeof(*user_out), true);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    struct o0_object *object = NULL;
    st = o0_handle_lookup(proc->handles, handle, O0_OBJ_BUDGET, O0_RIGHT_INFO,
                          &object);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    struct o0_budget_snapshot snapshot;
    o0_budget_snapshot((struct o0_budget *)object, &snapshot);
    struct o0_uapi_budget_info out = {
        .limit = snapshot.limit,
        .used = snapshot.used,
        .remaining = snapshot.remaining,
        .reserved = 0,
    };
    st = o0_copy_to_user(user_out, &out, sizeof(out));
    return ret_status(st);
}

static int64_t sys_endpoint_create(void) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    struct o0_endpoint *endpoint = NULL;
    o0_status st = o0_endpoint_create(proc->budget, false, &endpoint);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    uint64_t handle = 0;
    st = o0_handle_install(proc->handles, &endpoint->obj, O0_RIGHT_ENDPOINT_DEFAULT,
                           &handle);
    if (st != O0_OK) {
        o0_object_unref(&endpoint->obj);
        return o0_uapi_error_from_status(st);
    }
    o0_object_unref(&endpoint->obj);
    return (int64_t)handle;
}

static int64_t sys_ipc_send(uint64_t endpoint_handle, const void *buf, uint64_t len,
                            const struct o0_uapi_handle_xfer *xfers, uint32_t nxfers) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    struct o0_object *object = NULL;
    o0_status st = o0_handle_lookup(proc->handles, endpoint_handle, O0_OBJ_ENDPOINT,
                                    O0_RIGHT_SEND, &object);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    return o0_ipc_send(proc, (struct o0_endpoint *)object, buf, len, xfers, nxfers);
}

static int64_t sys_ipc_recv(uint64_t endpoint_handle, void *buf, uint64_t maxlen,
                            uint64_t *handles, uint32_t max_handles,
                            uint32_t *out_count) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    struct o0_object *object = NULL;
    o0_status st = o0_handle_lookup(proc->handles, endpoint_handle, O0_OBJ_ENDPOINT,
                                    O0_RIGHT_RECV, &object);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    return o0_ipc_recv(proc, (struct o0_endpoint *)object, buf, maxlen, handles,
                       max_handles, out_count);
}

static int64_t sys_ipc_call(uint64_t endpoint_handle, const void *send_buf,
                            uint64_t send_len, void *recv_buf, uint64_t recv_len) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    struct o0_object *object = NULL;
    o0_status st = o0_handle_lookup(proc->handles, endpoint_handle, O0_OBJ_ENDPOINT,
                                    O0_RIGHT_SEND | O0_RIGHT_RECV, &object);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    return o0_ipc_call(proc, (struct o0_endpoint *)object, send_buf, send_len,
                       recv_buf, recv_len);
}

static int64_t sys_memory_create(uint64_t size) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    if (size == 0 || size > O0_MEMORY_MAX_BYTES) {
        return O0_UE_RANGE;
    }
    struct o0_memory *memory = NULL;
    o0_status st = o0_memory_create(proc->budget, (size_t)size,
                                    O0_RIGHT_MEMORY_DEFAULT, &memory);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    uint64_t handle = 0;
    st = o0_handle_install(proc->handles, &memory->obj, O0_RIGHT_MEMORY_DEFAULT,
                           &handle);
    if (st != O0_OK) {
        o0_object_unref(&memory->obj);
        return o0_uapi_error_from_status(st);
    }
    o0_object_unref(&memory->obj);
    return (int64_t)handle;
}

static int64_t sys_memory_map(uint64_t memory_handle, uint64_t vaddr, uint64_t len,
                              uint32_t prot, uint64_t offset) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    struct o0_object *object = NULL;
    o0_status st = o0_handle_lookup(proc->handles, memory_handle, O0_OBJ_MEMORY,
                                    O0_RIGHT_MAP, &object);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    st = o0_memory_map((struct o0_memory *)object, proc->as, vaddr, (size_t)len,
                       (size_t)offset, prot);
    return ret_status(st);
}

static int64_t sys_process_spawn(uint64_t memory_handle, uint64_t budget_handle,
                                 const struct o0_uapi_handle_xfer *user_xfers,
                                 uint32_t nxfers) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) {
        return O0_UE_INVAL;
    }
    if (nxfers > 8) {
        return O0_UE_INVAL;
    }
    struct o0_object *image_object = NULL;
    o0_status st = o0_handle_lookup(proc->handles, memory_handle, O0_OBJ_MEMORY,
                                    O0_RIGHT_READ | O0_RIGHT_MAP, &image_object);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }
    struct o0_object *budget_object = NULL;
    st = o0_handle_lookup(proc->handles, budget_handle, O0_OBJ_BUDGET,
                          O0_RIGHT_SPLIT, &budget_object);
    if (st != O0_OK) {
        return o0_uapi_error_from_status(st);
    }

    struct o0_uapi_handle_xfer xfers[8];
    if (nxfers != 0) {
        st = o0_copy_from_user(xfers, user_xfers, nxfers * sizeof(xfers[0]));
        if (st != O0_OK) {
            return o0_uapi_error_from_status(st);
        }
    }

    struct o0_object *objects[8];
    uint32_t rights[8];
    uint32_t taken = 0;
    for (uint32_t i = 0; i < nxfers; ++i) {
        struct o0_object *object = NULL;
        uint32_t entry_rights = 0;
        st = o0_handle_lookup_any(proc->handles, xfers[i].handle, &object,
                                  &entry_rights);
        if (st != O0_OK) {
            goto rollback;
        }
        if ((entry_rights & O0_RIGHT_TRANSFER) == 0) {
            st = O0_EPERM;
            goto rollback;
        }
        uint32_t requested = xfers[i].rights != 0 ? xfers[i].rights : entry_rights;
        if ((requested & ~entry_rights) != 0) {
            st = O0_EPERM;
            goto rollback;
        }
        for (uint32_t j = 0; j < i; ++j) {
            if (xfers[j].handle == xfers[i].handle) {
                st = O0_EINVAL;
                goto rollback;
            }
        }
        st = o0_handle_take(proc->handles, xfers[i].handle, O0_OBJ_NONE,
                            O0_RIGHT_TRANSFER, &objects[i], &entry_rights);
        if (st != O0_OK) {
            goto rollback;
        }
        rights[i] = requested;
        ++taken;
    }

    struct o0_spawn_grant grants[8];
    for (uint32_t i = 0; i < nxfers; ++i) {
        grants[i].object = objects[i];
        grants[i].rights = rights[i];
        grants[i].reserved = 0;
    }
    o0_thread_t *thread = NULL;
    st = o0_user_spawn_memory("spawned", (struct o0_memory *)image_object,
                              (struct o0_budget *)budget_object, grants, nxfers,
                              O0_SPAWN_DETACHED, &thread);
    if (st != O0_OK) {
        goto rollback;
    }
    o0_process_t *child = o0_thread_process(thread);
    return child != NULL ? (int64_t)child->pid : 0;

rollback:
    for (uint32_t i = 0; i < taken; ++i) {
        uint64_t ignored = 0;
        o0_status restore = o0_handle_install_owned(proc->handles, objects[i],
                                                    rights[i], &ignored);
        if (restore != O0_OK) {
            o0_object_unref(objects[i]);
        }
    }
    return o0_uapi_error_from_status(st);
}

static o0_status syscall_copy_path(char *path, const char *user_path,
                                    uint64_t path_length) {
    if (path == NULL || user_path == NULL) return O0_EINVAL;
    if (path_length == 0) return O0_EINVAL;
    if (path_length > O0_UAPI_PATH_MAX) return O0_ERANGE;
    o0_status st = o0_syscall_arg_check(user_path, (size_t)path_length, false);
    if (st != O0_OK) return st;
    st = o0_copy_from_user(path, user_path, (size_t)path_length);
    if (st != O0_OK) return st;
    for (size_t i = 0; i < (size_t)path_length; ++i) {
        if (path[i] == '\0') return O0_EINVAL;
    }
    path[path_length] = '\0';
    return O0_OK;
}

static int64_t sys_file_open(uint64_t directory_handle, const char *user_path,
                             uint64_t path_length, uint32_t flags,
                             uint32_t rights) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) return O0_UE_INVAL;
    char path[O0_UAPI_PATH_MAX + 1u];
    o0_status st = syscall_copy_path(path, user_path, path_length);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    uint32_t directory_rights = 0;
    struct o0_object *directory = NULL;
    st = o0_handle_lookup_any(proc->handles, directory_handle, &directory,
                              &directory_rights);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    if (directory->type != O0_OBJ_VNODE) return O0_UE_INVAL;
    if ((flags & ~(O0_VFS_OPEN_CREATE | O0_VFS_OPEN_EXCLUSIVE)) != 0 ||
        ((flags & O0_VFS_OPEN_EXCLUSIVE) != 0 &&
         (flags & O0_VFS_OPEN_CREATE) == 0) ||
        (rights & (O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_INFO |
                   O0_RIGHT_ENUM | O0_RIGHT_CREATE | O0_RIGHT_REMOVE)) == 0) {
        return O0_UE_INVAL;
    }
    if ((directory_rights & O0_RIGHT_READ) == 0 ||
        (rights & ~directory_rights) != 0 ||
        ((flags & O0_VFS_OPEN_CREATE) != 0 &&
         (directory_rights & O0_RIGHT_CREATE) == 0)) {
        return O0_UE_PERM;
    }

    /* Opening a directory returns another vnode capability; opening a file
     * returns an O0_OBJ_FILE capability. The object type is never ambiguous. */
    struct o0_vnode *node = NULL;
    st = o0_vfs_lookup((struct o0_vnode *)directory, path,
                       (size_t)path_length, &node);
    if (st == O0_OK && node != NULL && node->type == O0_VNODE_DIRECTORY) {
        if ((flags & (O0_VFS_OPEN_CREATE | O0_VFS_OPEN_EXCLUSIVE)) ==
            (O0_VFS_OPEN_CREATE | O0_VFS_OPEN_EXCLUSIVE)) {
            o0_object_unref(&node->obj);
            return O0_UE_EXIST;
        }
        uint64_t handle = 0;
        o0_status install = o0_handle_install(proc->handles, &node->obj,
                                              rights, &handle);
        o0_object_unref(&node->obj);
        return install == O0_OK ? (int64_t)handle
                                : o0_uapi_error_from_status(install);
    }
    if (node != NULL) o0_object_unref(&node->obj);
    if (st != O0_OK && st != O0_ENOENT)
        return o0_uapi_error_from_status(st);
    bool created = st == O0_ENOENT && (flags & O0_VFS_OPEN_CREATE) != 0;

    struct o0_vfs_file *file = NULL;
    st = o0_vfs_open((struct o0_vnode *)directory, path, (size_t)path_length,
                     flags, rights, proc->budget, &file);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    uint64_t handle = 0;
    st = o0_handle_install(proc->handles, &file->obj, rights, &handle);
    if (st != O0_OK) {
        o0_object_unref(&file->obj);
        if (created) { o0_status cleanup = o0_vfs_unlink((struct o0_vnode *)directory, path, (size_t)path_length); (void)cleanup; }
        return o0_uapi_error_from_status(st);
    }
    o0_object_unref(&file->obj);
    return (int64_t)handle;
}

static int64_t sys_file_read(uint64_t file_handle, void *user_buffer,
                             uint64_t length, uint64_t offset) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) return O0_UE_INVAL;
    if (length > O0_UAPI_FILE_IO_MAX) return O0_UE_RANGE;
    if (length != 0) {
        o0_status check = o0_syscall_arg_check(user_buffer, (size_t)length, true);
        if (check != O0_OK) return o0_uapi_error_from_status(check);
    }
    struct o0_object *object = NULL;
    o0_status st = o0_handle_lookup(proc->handles, file_handle, O0_OBJ_FILE,
                                    O0_RIGHT_READ, &object);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    uint8_t buffer[O0_UAPI_FILE_IO_MAX];
    size_t got = 0;
    st = o0_vfs_read((struct o0_vfs_file *)object, offset, buffer,
                     (size_t)length, &got);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    if (got != 0) {
        st = o0_copy_to_user(user_buffer, buffer, got);
        if (st != O0_OK) return o0_uapi_error_from_status(st);
    }
    return (int64_t)got;
}

static int64_t sys_file_write(uint64_t file_handle, const void *user_buffer,
                              uint64_t length, uint64_t offset) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) return O0_UE_INVAL;
    if (length > O0_UAPI_FILE_IO_MAX) return O0_UE_RANGE;
    uint8_t buffer[O0_UAPI_FILE_IO_MAX];
    if (length != 0) {
        o0_status st = o0_syscall_arg_check(user_buffer, (size_t)length, false);
        if (st != O0_OK) return o0_uapi_error_from_status(st);
        st = o0_copy_from_user(buffer, user_buffer, (size_t)length);
        if (st != O0_OK) return o0_uapi_error_from_status(st);
    }
    struct o0_object *object = NULL;
    o0_status st = o0_handle_lookup(proc->handles, file_handle, O0_OBJ_FILE,
                                    O0_RIGHT_WRITE, &object);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    size_t written = 0;
    st = o0_vfs_write((struct o0_vfs_file *)object, offset, buffer,
                      (size_t)length, &written);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    return (int64_t)written;
}

static int64_t sys_file_stat(uint64_t directory_handle, const char *user_path,
                             uint64_t path_length,
                             struct o0_uapi_vfs_stat *user_out) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) return O0_UE_INVAL;
    char path[O0_UAPI_PATH_MAX + 1u];
    o0_status st = syscall_copy_path(path, user_path, path_length);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    st = o0_syscall_arg_check(user_out, sizeof(*user_out), true);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    struct o0_object *directory = NULL;
    st = o0_handle_lookup(proc->handles, directory_handle, O0_OBJ_VNODE,
                          O0_RIGHT_READ | O0_RIGHT_INFO, &directory);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    struct o0_vfs_stat info;
    st = o0_vfs_stat((struct o0_vnode *)directory, path,
                     (size_t)path_length, &info);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    struct o0_uapi_vfs_stat out = {
        .size = info.size, .object_id = info.object_id,
        .type = info.type, .flags = info.flags
    };
    st = o0_copy_to_user(user_out, &out, sizeof(out));
    return ret_status(st);
}

static int64_t sys_file_readdir(uint64_t directory_handle, uint64_t cursor,
                                struct o0_uapi_vfs_dirent *user_out) {
    o0_process_t *proc = o0_process_current();
    if (proc == NULL) return O0_UE_INVAL;
    o0_status st = o0_syscall_arg_check(user_out, sizeof(*user_out), true);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    struct o0_object *directory = NULL;
    st = o0_handle_lookup(proc->handles, directory_handle, O0_OBJ_VNODE,
                          O0_RIGHT_ENUM, &directory);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    struct o0_vfs_dirent entry;
    uint64_t next = cursor;
    st = o0_vfs_readdir((struct o0_vnode *)directory, &next, &entry);
    if (st != O0_OK) return o0_uapi_error_from_status(st);
    struct o0_uapi_vfs_dirent out = {
        .next_cursor = next, .object_id = entry.object_id, .size = entry.size,
        .type = entry.type, .name_len = entry.name_len
    };
    memcpy(out.name, entry.name, sizeof(out.name));
    st = o0_copy_to_user(user_out, &out, sizeof(out));
    return ret_status(st);
}

int64_t o0_syscall_dispatch(struct o0_syscall_frame *frame) {
    if (frame == NULL) {
        return O0_UE_INVAL;
    }
    switch (frame->rax) {
        case O0_SYS_EXIT:
            o0_thread_exit((int)frame->rdi);
        case O0_SYS_YIELD:
            o0_thread_yield();
            return 0;
        case O0_SYS_DEBUG_WRITE:
            return sys_debug_write((const void *)(uintptr_t)frame->rdi, frame->rsi);
        case O0_SYS_GETPID:
            return sys_getpid();
        case O0_SYS_HANDLE_CLOSE:
            return sys_handle_close(frame->rdi);
        case O0_SYS_HANDLE_DUP:
            return sys_handle_dup(frame->rdi, (uint32_t)frame->rsi);
        case O0_SYS_HANDLE_INFO:
            return sys_handle_info(
                frame->rdi, (struct o0_uapi_handle_info *)(uintptr_t)frame->rsi);
        case O0_SYS_BUDGET_SPLIT:
            return sys_budget_split(frame->rdi, frame->rsi);
        case O0_SYS_BUDGET_INFO:
            return sys_budget_info(
                frame->rdi, (struct o0_uapi_budget_info *)(uintptr_t)frame->rsi);
        case O0_SYS_ENDPOINT_CREATE:
            return sys_endpoint_create();
        case O0_SYS_IPC_SEND:
            return sys_ipc_send(
                frame->rdi, (const void *)(uintptr_t)frame->rsi, frame->rdx,
                (const struct o0_uapi_handle_xfer *)(uintptr_t)frame->r10,
                (uint32_t)frame->r8);
        case O0_SYS_IPC_RECV:
            return sys_ipc_recv(frame->rdi, (void *)(uintptr_t)frame->rsi, frame->rdx,
                                (uint64_t *)(uintptr_t)frame->r10, (uint32_t)frame->r8,
                                (uint32_t *)(uintptr_t)frame->r9);
        case O0_SYS_IPC_CALL:
            return sys_ipc_call(frame->rdi, (const void *)(uintptr_t)frame->rsi,
                                frame->rdx, (void *)(uintptr_t)frame->r10, frame->r8);
        case O0_SYS_MEMORY_CREATE:
            return sys_memory_create(frame->rdi);
        case O0_SYS_MEMORY_MAP:
            return sys_memory_map(frame->rdi, frame->rsi, frame->rdx,
                                  (uint32_t)frame->r10, frame->r8);
        case O0_SYS_PROCESS_SPAWN:
            return sys_process_spawn(
                frame->rdi, frame->rsi,
                (const struct o0_uapi_handle_xfer *)(uintptr_t)frame->rdx,
                (uint32_t)frame->r10);
        case O0_SYS_FILE_OPEN:
            return sys_file_open(frame->rdi,
                                 (const char *)(uintptr_t)frame->rsi,
                                 frame->rdx, (uint32_t)frame->r10,
                                 (uint32_t)frame->r8);
        case O0_SYS_FILE_READ:
            return sys_file_read(frame->rdi, (void *)(uintptr_t)frame->rsi,
                                 frame->rdx, frame->r10);
        case O0_SYS_FILE_WRITE:
            return sys_file_write(frame->rdi,
                                  (const void *)(uintptr_t)frame->rsi,
                                  frame->rdx, frame->r10);
        case O0_SYS_FILE_STAT:
            return sys_file_stat(frame->rdi,
                                 (const char *)(uintptr_t)frame->rsi,
                                 frame->rdx,
                                 (struct o0_uapi_vfs_stat *)(uintptr_t)frame->r10);
        case O0_SYS_FILE_READDIR:
            return sys_file_readdir(frame->rdi, frame->rsi,
                                    (struct o0_uapi_vfs_dirent *)(uintptr_t)frame->rdx);
        case O0_SYS_INVALID:
            return O0_UE_INVAL;
        default:
            return O0_UE_NOSYS;
    }
}
