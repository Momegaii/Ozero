/* Ozero kernel - virtual filesystem layer (Wave 4.6).
 *
 * The VFS sits above concrete filesystems (OzeroFS at Stage 4, a read-only or
 * tmpfs-style filesystem later) and below the file syscalls. It provides:
 *
 *   - a mount table (block device + filesystem handle + root vnode),
 *   *   - a vnode abstraction: a refcounted kernel object naming an inode in a
 *         particular mounted filesystem,
 *   *   - path resolution against mount roots,
 *   *   - open file descriptions (vnode reference + offset + access flags),
 *   *   - file descriptors implemented as handles to open-file objects.
 *
 * Every user pointer goes through the copy helpers; the kernel never
 * dereferences user memory directly.
 *
 * Ozero defines its own native execution architecture and does not default to
 * Linux conventions. No ELF, ELF64, Linux syscall, glibc or Linux userspace
 * assumption appears here. The vnode/mount/open-file design is Ozero's own.
 */
#ifndef O0_VFS_H
#define O0_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/block.h"
#include "ozero/fs.h"
#include "ozero/handle.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ---- vnode object type ---- */

#define O0_OBJ_VNODE 5   /* vnode: an inode in a mounted filesystem */
#define O0_OBJ_FILE 6    /* open file description: vnode + offset + flags */

/* Open flags. Ozero-native, not POSIX. */
#define O0_O_RDONLY 0x00000001u
#define O0_O_WRONLY 0x00000002u
#define O0_O_RDWR (O0_O_RDONLY | O0_O_WRONLY)
#define O0_O_CREAT 0x00000004u   /* create the file if it does not exist */
#define O0_O_DIRECTORY 0x00000008u /* fail if the path is not a directory */

/* ---- vnode ---- */

/* Forward declarations: vnode and filesystem are defined below / in fs.h. */
struct o0_vnode;
struct o0fs;

/* Operations on a vnode. Filesystems provide implementations; the VFS
 * dispatches through this vector. */
struct o0_vnode_ops
{
    o0_status (*read)(struct o0_vnode *vn, uint64_t offset, void *buffer,
                      size_t capacity, size_t *out_len);
    o0_status (*write)(struct o0_vnode *vn, uint64_t offset,
                       const void *buffer, size_t len, size_t *out_len);
    o0_status (*stat)(struct o0_vnode *vn, struct o0fs_stat *out);
    o0_status (*lookup)(struct o0_vnode *dir, const char *name,
                        struct o0_vnode **out);
    o0_status (*list)(struct o0_vnode *dir, o0fs_iter_fn fn, void *ctx);
};

/* A vnode names an inode in a mounted filesystem. It is an o0_object of type
 * O0_OBJ_VNODE, so handle rights and refcounting are inherited from the object
 * system. */
struct o0_vnode
{
    struct o0_object obj;   /* embedded object header, type == O0_OBJ_VNODE */
    struct o0fs *fs;        /* owning filesystem (not refcounted separately) */
    uint32_t inode;         /* inode number within fs */
    const struct o0_vnode_ops *ops;
    uint32_t reserved;
};

/* Allocate and initialise a vnode. Returns the object; the caller installs it
 * into a handle table if desired. */
O0_NODISCARD o0_status o0_vnode_create(struct o0fs *fs, uint32_t inode,
                                       const struct o0_vnode_ops *ops,
                                       struct o0_vnode **out);

/* Look up a vnode's filesystem block device. */
struct o0_block_dev *o0_vnode_dev(const struct o0_vnode *vn);

/* ---- mount ---- */

/* A mounted filesystem. The mount table is a fixed kernel array. */
struct o0_mount
{
    struct o0_block_dev *dev;
    struct o0fs *fs;
    struct o0_vnode *root;   /* root vnode (holds a reference) */
    bool used;
};

#define O0_MAX_MOUNTS 8u

/* Find a free mount slot and install it. */
O0_NODISCARD o0_status o0_mount_install(struct o0_block_dev *dev,
                                        struct o0fs *fs,
                                        struct o0_vnode *root,
                                        struct o0_mount **out);

/* Look up a mount by index. Returns O0_ENOENT if the slot is unused. */
o0_status o0_mount_get(size_t index, struct o0_mount **out);

/* Release a mount (called on unmount). */
void o0_mount_release(struct o0_mount *mnt);

/* ---- open file description ---- */

/* An open file: a vnode reference, a cursor offset, and access flags. It is an
 * o0_object of type O0_OBJ_FILE. */
struct o0_file
{
    struct o0_object obj;    /* embedded object header, type == O0_OBJ_FILE */
    struct o0_vnode *vn;     /* referenced vnode */
    uint64_t offset;         /* read/write cursor */
    uint32_t flags;          /* O0_O_* */
    uint32_t reserved;
};

/* Allocate and initialise an open file. */
O0_NODISCARD o0_status o0_file_create(struct o0_vnode *vn, uint32_t flags,
                                      struct o0_file **out);

/* Resolve an absolute path starting at the root mount, returning the vnode. */
O0_NODISCARD o0_status o0_vfs_lookup_path(const char *path,
                                          struct o0_vnode **out);

/* Open or create a file at an absolute path. On success installs a handle to
 * the open-file object in the caller's handle table. */
O0_NODISCARD o0_status o0_vfs_open(const char *path, uint32_t flags,
                                   struct o0_process *proc,
                                   uint64_t *out_handle);

/* Read from an open file described by a handle. */
O0_NODISCARD o0_status o0_vfs_read(struct o0_process *proc, uint64_t handle,
                                   void *buffer, size_t capacity,
                                   size_t *out_len);

/* Write to an open file described by a handle. */
O0_NODISCARD o0_status o0_vfs_write(struct o0_process *proc, uint64_t handle,
                                    const void *buffer, size_t len,
                                    size_t *out_len);

/* Stat a path. */
O0_NODISCARD o0_status o0_vfs_stat(const char *path, struct o0fs_stat *out);

/* List a directory at a path. */
O0_NODISCARD o0_status o0_vfs_listdir(const char *path, o0fs_iter_fn fn,
                                      void *ctx);

/* Initialise the VFS mount table (call once at boot). */
void o0_vfs_init(void);

#ifdef __cplusplus
}
#endif

#endif /* O0_VFS_H */
