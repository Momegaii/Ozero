/* Ozero kernel - native syscall dispatcher.
 *
 * Wave 2 exposes exactly four calls. Every buffer argument goes through
 * o0_copy_from_user; the kernel never dereferences a user pointer here.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/abi.h"
#include "ozero/status.h"
#include "ozero/syscall.h"
#include "ozero/thread.h"
#include "ozero/types.h"
#include "ozero/uaccess.h"
#include "ozero/uapi.h"

#include "serial.h"
#include "thread_internal.h"

#define O0_DEBUG_WRITE_MAX 4096u
#define O0_DEBUG_CHUNK 128u

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
        case O0_SYS_INVALID:
            return O0_UE_INVAL;
        default:
            return O0_UE_NOSYS;
    }
}
