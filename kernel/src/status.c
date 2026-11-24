/* Ozero kernel - status code names for diagnostics. */

#include "ozero/status.h"

const char *o0_status_name(o0_status status) {
    switch (status) {
        case O0_OK: return "OK";
        case O0_EINVAL: return "EINVAL";
        case O0_ENOMEM: return "ENOMEM";
        case O0_ENOENT: return "ENOENT";
        case O0_EBUSY: return "EBUSY";
        case O0_EPERM: return "EPERM";
        case O0_ENOSYS: return "ENOSYS";
        case O0_EFAULT: return "EFAULT";
        case O0_EIO: return "EIO";
        case O0_ERANGE: return "ERANGE";
        case O0_ETIMEDOUT: return "ETIMEDOUT";
        case O0_EAGAIN: return "EAGAIN";
        case O0_EOVERFLOW: return "EOVERFLOW";
        case O0_ENOTSUP: return "ENOTSUP";
        case O0_EEXIST: return "EEXIST";
        case O0_ENOTDIR: return "ENOTDIR";
        case O0_EISDIR: return "EISDIR";
        case O0_ENOTEMPTY: return "ENOTEMPTY";
        case O0_ENOSPC: return "ENOSPC";
        default: return "UNKNOWN";
    }
}
