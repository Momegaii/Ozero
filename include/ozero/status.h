/* Ozero kernel - status codes.
 *
 * Deliberately not errno and not NTSTATUS. Translation to a user-visible ABI is a
 * later decision (A2/A4).
 */
#ifndef O0_STATUS_H
#define O0_STATUS_H

#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum o0_status {
    O0_OK = 0,
    O0_EINVAL = 1,   /* bad argument or alignment */
    O0_ENOMEM = 2,   /* out of physical or virtual memory */
    O0_ENOENT = 3,   /* no such object, device or mapping */
    O0_EBUSY = 4,    /* resource is in use */
    O0_EPERM = 5,    /* operation not permitted */
    O0_ENOSYS = 6,   /* not implemented */
    O0_EFAULT = 7,   /* invalid user pointer */
    O0_EIO = 8,      /* hardware or transport error */
    O0_ERANGE = 9,   /* value outside the supported range */
    O0_ETIMEDOUT = 10,
    O0_EAGAIN = 11,  /* retry later */
    O0_EOVERFLOW = 12,
    O0_ENOTSUP = 13, /* valid request the current interface cannot express */
    O0_EEXIST = 14,  /* object already exists */
    O0_ENOTDIR = 15, /* path component is not a directory */
    O0_EISDIR = 16,  /* operation requires a non-directory */
    O0_ENOTEMPTY = 17, /* directory still has entries */
    O0_ENOSPC = 18   /* no space left on the storage object */
} o0_status;

const char *o0_status_name(o0_status status);

#ifdef __cplusplus
}
#endif

#endif /* O0_STATUS_H */
