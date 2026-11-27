/* Ozero VFS: vnode operations and capability-relative file access.
 * Native ABI and lifetime rules are defined by ADR 0022.
 */
#ifndef O0_VFS_H
#define O0_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/budget.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_VFS_PATH_MAX 256u
#define O0_VFS_NAME_MAX 63u
#define O0_VFS_MAX_DEPTH 16u
#define O0_VFS_IO_MAX 4096u
#define O0_VFS_MOUNT_MAX 8u

enum o0_vnode_type {
    O0_VNODE_FILE = 1,
    O0_VNODE_DIRECTORY = 2
};

enum o0_vfs_open_flags {
    O0_VFS_OPEN_CREATE = 1u << 0,
    O0_VFS_OPEN_EXCLUSIVE = 1u << 1
};

struct o0_vfs_stat {
    uint64_t size;
    uint64_t object_id;
    uint32_t type;
    uint32_t flags;
};

struct o0_vfs_dirent {
    uint64_t object_id;
    uint64_t size;
    uint32_t type;
    uint32_t name_len;
    char name[O0_VFS_NAME_MAX + 1u];
};

struct o0_vnode;
struct o0_vfs_file;

/* Backends receive kernel buffers only. A successful lookup returns one
 * referenced vnode. readdir's cursor is an opaque backend-owned position. */
struct o0_vnode_ops {
    o0_status (*lookup)(struct o0_vnode *dir, const char *name, size_t name_len,
                        struct o0_vnode **out);
    o0_status (*create)(struct o0_vnode *dir, const char *name, size_t name_len,
                        uint32_t type, struct o0_vnode **out);
    o0_status (*read)(struct o0_vnode *node, uint64_t offset, void *buffer,
                      size_t length, size_t *out_length);
    o0_status (*write)(struct o0_vnode *node, uint64_t offset,
                       const void *buffer, size_t length, size_t *out_length);
    o0_status (*stat)(struct o0_vnode *node, struct o0_vfs_stat *out);
    o0_status (*readdir)(struct o0_vnode *dir, uint64_t *cursor,
                         struct o0_vfs_dirent *out);
    o0_status (*unlink)(struct o0_vnode *dir, const char *name, size_t name_len);
};

struct o0_vnode {
    struct o0_object obj; /* O0_OBJ_VNODE */
    const struct o0_vnode_ops *ops;
    void *private_data;
    void *fs_identity; /* opaque backend identity used for mount-cycle checks */
    uint32_t type;
    uint32_t reserved;
    struct o0_vnode *mounted_root; /* owned mount reference, if mounted here */
};

struct o0_vfs_file {
    struct o0_object obj; /* O0_OBJ_FILE */
    struct o0_vnode *node;
    uint32_t rights;
    uint32_t reserved;
};

O0_NODISCARD o0_status o0_vfs_mount_root(struct o0_vnode *root);
O0_NODISCARD o0_status o0_vfs_mount(struct o0_vnode *directory,
                                    struct o0_vnode *root);
O0_NODISCARD o0_status o0_vfs_unmount(struct o0_vnode *directory);
O0_NODISCARD o0_status o0_vfs_root(struct o0_vnode **out);
O0_NODISCARD o0_status o0_vfs_lookup(struct o0_vnode *directory,
                                     const char *path, size_t path_length,
                                     struct o0_vnode **out);

O0_NODISCARD o0_status o0_vfs_open(struct o0_vnode *directory,
                                   const char *path, size_t path_length,
                                   uint32_t flags, uint32_t rights,
                                   struct o0_budget *budget,
                                   struct o0_vfs_file **out);
O0_NODISCARD o0_status o0_vfs_read(struct o0_vfs_file *file, uint64_t offset,
                                   void *buffer, size_t length,
                                   size_t *out_length);
O0_NODISCARD o0_status o0_vfs_write(struct o0_vfs_file *file, uint64_t offset,
                                    const void *buffer, size_t length,
                                    size_t *out_length);
O0_NODISCARD o0_status o0_vfs_stat(struct o0_vnode *directory,
                                   const char *path, size_t path_length,
                                   struct o0_vfs_stat *out);
O0_NODISCARD o0_status o0_vfs_readdir(struct o0_vnode *directory,
                                      uint64_t *cursor,
                                      struct o0_vfs_dirent *out);
O0_NODISCARD o0_status o0_vfs_unlink(struct o0_vnode *directory,
                                     const char *path, size_t path_length);

/* tmpfs is the first writable backend. Its root vnode is returned with one
 * reference; capacity is a hard per-instance limit, not a hint. */
struct o0fs;
O0_NODISCARD o0_status o0_vfs_o0fs_create(struct o0fs *fs,
                                         struct o0_budget *budget,
                                         struct o0_vnode **root_out);
O0_NODISCARD o0_status o0_vfs_mkdir(struct o0_vnode *directory,
                                    const char *path, size_t path_length);

O0_NODISCARD o0_status o0_tmpfs_create(struct o0_budget *budget,
                                       size_t capacity,
                                       struct o0_vnode **root_out);

#ifdef __cplusplus
}
#endif
#endif /* O0_VFS_H */
