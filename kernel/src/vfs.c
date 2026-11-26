/* Ozero kernel - virtual filesystem layer implementation (Wave 4.6).
 *
 * Mount table, vnode abstraction, path resolution and open-file descriptions
 * built on top of concrete filesystems (OzeroFS). File descriptors are handles
 * to open-file objects; vnodes are objects naming inodes in mounted
 * filesystems.
 *
 * Ozero defines its own native execution architecture and does not default to
 * Linux conventions. No ELF, ELF64, Linux syscall, glibc or Linux userspace
 * assumption appears here.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ozero/block.h"
#include "ozero/fs.h"
#include "ozero/handle.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/thread.h"
#include "ozero/types.h"
#include "ozero/uaccess.h"
#include "ozero/vfs.h"

#include "thread_internal.h"

/* ---- mount table ----

 * Fixed kernel array of mounts. Index 0 is conventionally the root mount.
 */
static struct o0_mount vfs_mounts[O0_MAX_MOUNTS];

void o0_vfs_init(void) {
    memset(vfs_mounts, 0, sizeof(vfs_mounts));
}

o0_status o0_mount_install(struct o0_block_dev *dev, struct o0fs *fs,
                           struct o0_vnode *root, struct o0_mount **out) {
    if (dev == NULL || fs == NULL || root == NULL || out == NULL) {
        return O0_EINVAL;
    }
    for (size_t i = 0; i < O0_MAX_MOUNTS; ++i) {
        if (!vfs_mounts[i].used) {
            vfs_mounts[i].dev = dev;
            vfs_mounts[i].fs = fs;
            vfs_mounts[i].root = root;
            vfs_mounts[i].used = true;
            *out = &vfs_mounts[i];
            return O0_OK;
        }
    }
    return O0_ENOMEM;
}

o0_status o0_mount_get(size_t index, struct o0_mount **out) {
    if (out == NULL || index >= O0_MAX_MOUNTS) {
        return O0_EINVAL;
    }
    if (!vfs_mounts[index].used) {
        return O0_ENOENT;
    }
    *out = &vfs_mounts[index];
    return O0_OK;
}

void o0_mount_release(struct o0_mount *mnt) {
    if (mnt == NULL) {
        return;
    }
    mnt->used = false;
    mnt->dev = NULL;
    mnt->fs = NULL;
    mnt->root = NULL;
}

/* ---- vnode ---- */

static void vnode_destroy(struct o0_object *obj) {
    (void)obj;
    /* The vnode does not own its filesystem; nothing to release beyond the
     * object header itself, which o0_object_unref frees. */
}

static o0_status o0fs_vnode_read(struct o0_vnode *vn, uint64_t offset,
                                 void *buffer, size_t capacity,
                                 size_t *out_len) {
    return o0fs_inode_read(vn->fs, vn->inode, offset, buffer, capacity,
                           out_len);
}

static o0_status o0fs_vnode_write(struct o0_vnode *vn, uint64_t offset,
                                  const void *buffer, size_t len,
                                  size_t *out_len) {
    return o0fs_inode_write(vn->fs, vn->inode, offset, buffer, len, out_len);
}

static o0_status o0fs_vnode_stat(struct o0_vnode *vn, struct o0fs_stat *out) {
    return o0fs_inode_stat(vn->fs, vn->inode, out);
}

static o0_status o0fs_vnode_lookup(struct o0_vnode *dir, const char *name,
                                   struct o0_vnode **out) {
    if (dir == NULL || name == NULL || out == NULL) {
        return O0_EINVAL;
    }
    /* Resolve the name within the parent directory by constructing a path
     * from the mount root. For V1, walk from the root using the existing
     * OzeroFS path resolver. */
    uint32_t parent_inode = dir->inode;
    uint32_t child_inode = 0;
    o0_status st = o0fs_lookup(dir->fs, "/", &child_inode);
    if (st != O0_OK) {
        return st;
    }
    /* For now, re-resolve from root. A proper implementation resolves the
     * single component within dir's data blocks. */
    (void)parent_inode;
    (void)name;
    return O0_ENOSYS;
}

static o0_status o0fs_vnode_list(struct o0_vnode *dir, o0fs_iter_fn fn,
                                 void *ctx) {
    if (dir == NULL || fn == NULL) {
        return O0_EINVAL;
    }
    /* Build the path for this directory. For V1, only the root directory is
     * listable from the VFS; subdirectories require path construction. */
    if (dir->inode != 1) {
        return O0_ENOSYS;
    }
    return o0fs_list(dir->fs, "/", fn, ctx);
}

static const struct o0_vnode_ops o0fs_vnode_ops = {
    .read = o0fs_vnode_read,
    .write = o0fs_vnode_write,
    .stat = o0fs_vnode_stat,
    .lookup = o0fs_vnode_lookup,
    .list = o0fs_vnode_list,
};

o0_status o0_vnode_create(struct o0fs *fs, uint32_t inode,
                          const struct o0_vnode_ops *ops,
                          struct o0_vnode **out) {
    if (fs == NULL || ops == NULL || out == NULL) {
        return O0_EINVAL;
    }
    struct o0_object *obj = NULL;
    o0_status st = o0_object_alloc(O0_OBJ_VNODE, sizeof(struct o0_vnode), NULL,
                                   vnode_destroy, &obj);
    if (st != O0_OK) {
        return st;
    }
    struct o0_vnode *vn = (struct o0_vnode *)obj;
    vn->fs = fs;
    vn->inode = inode;
    vn->ops = ops;
    vn->reserved = 0;
    *out = vn;
    return O0_OK;
}

struct o0_block_dev *o0_vnode_dev(const struct o0_vnode *vn) {
    if (vn == NULL || vn->fs == NULL) {
        return NULL;
    }
    return o0fs_dev(vn->fs);
}

/* ---- open file ---- */

static void file_destroy(struct o0_object *obj) {
    (void)obj;
    /* The open file does not own its vnode; the vnode reference is released
     * separately when the handle is closed. */
}

o0_status o0_file_create(struct o0_vnode *vn, uint32_t flags,
                         struct o0_file **out) {
    if (vn == NULL || out == NULL) {
        return O0_EINVAL;
    }
    struct o0_object *obj = NULL;
    o0_status st = o0_object_alloc(O0_OBJ_FILE, sizeof(struct o0_file), NULL,
                                   file_destroy, &obj);
    if (st != O0_OK) {
        return st;
    }
    struct o0_file *file = (struct o0_file *)obj;
    file->vn = vn;
    file->offset = 0;
    file->flags = flags;
    file->reserved = 0;
    *out = file;
    return O0_OK;
}

/* ---- path resolution ---- */

o0_status o0_vfs_lookup_path(const char *path, struct o0_vnode **out) {
    if (path == NULL || out == NULL || path[0] != '/') {
        return O0_EINVAL;
    }
    struct o0_mount *mnt = NULL;
    o0_status st = o0_mount_get(0, &mnt);
    if (st != O0_OK) {
        return st;
    }
    uint32_t inode_no = 0;
    st = o0fs_lookup(mnt->fs, path, &inode_no);
    if (st != O0_OK) {
        return st;
    }
    struct o0_vnode *vn = NULL;
    st = o0_vnode_create(mnt->fs, inode_no, &o0fs_vnode_ops, &vn);
    if (st != O0_OK) {
        return st;
    }
    *out = vn;
    return O0_OK;
}

/* ---- syscall helpers ---- */

o0_status o0_vfs_open(const char *path, uint32_t flags,
                      struct o0_process *proc, uint64_t *out_handle) {
    if (path == NULL || proc == NULL || out_handle == NULL) {
        return O0_EINVAL;
    }
    struct o0_vnode *vn = NULL;
    o0_status st = o0_vfs_lookup_path(path, &vn);
    if (st != O0_OK) {
        return st;
    }
    struct o0_file *file = NULL;
    st = o0_file_create(vn, flags, &file);
    if (st != O0_OK) {
        o0_object_unref(&vn->obj);
        return st;
    }
    uint64_t handle = 0;
    st = o0_handle_install(proc->handles, &file->obj,
                           O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_DUP,
                           &handle);
    if (st != O0_OK) {
        o0_object_unref(&vn->obj);
        o0_object_unref(&file->obj);
        return st;
    }
    *out_handle = handle;
    return O0_OK;
}

o0_status o0_vfs_read(struct o0_process *proc, uint64_t handle, void *buffer,
                      size_t capacity, size_t *out_len) {
    if (proc == NULL || out_len == NULL) {
        return O0_EINVAL;
    }
    struct o0_object *obj = NULL;
    o0_status st = o0_handle_lookup(proc->handles, handle, O0_OBJ_FILE,
                                    O0_RIGHT_READ, &obj);
    if (st != O0_OK) {
        return st;
    }
    struct o0_file *file = (struct o0_file *)obj;
    st = file->vn->ops->read(file->vn, file->offset, buffer, capacity,
                             out_len);
    if (st == O0_OK) {
        file->offset += *out_len;
    }
    o0_object_unref(obj);
    return st;
}

o0_status o0_vfs_write(struct o0_process *proc, uint64_t handle,
                       const void *buffer, size_t len, size_t *out_len) {
    if (proc == NULL || out_len == NULL) {
        return O0_EINVAL;
    }
    struct o0_object *obj = NULL;
    o0_status st = o0_handle_lookup(proc->handles, handle, O0_OBJ_FILE,
                                    O0_RIGHT_WRITE, &obj);
    if (st != O0_OK) {
        return st;
    }
    struct o0_file *file = (struct o0_file *)obj;
    st = file->vn->ops->write(file->vn, file->offset, buffer, len, out_len);
    if (st == O0_OK) {
        file->offset += *out_len;
    }
    o0_object_unref(obj);
    return st;
}

o0_status o0_vfs_stat(const char *path, struct o0fs_stat *out) {
    if (path == NULL || out == NULL) {
        return O0_EINVAL;
    }
    struct o0_vnode *vn = NULL;
    o0_status st = o0_vfs_lookup_path(path, &vn);
    if (st != O0_OK) {
        return st;
    }
    st = vn->ops->stat(vn, out);
    o0_object_unref(&vn->obj);
    return st;
}

o0_status o0_vfs_listdir(const char *path, o0fs_iter_fn fn, void *ctx) {
    if (path == NULL || fn == NULL) {
        return O0_EINVAL;
    }
    struct o0_vnode *vn = NULL;
    o0_status st = o0_vfs_lookup_path(path, &vn);
    if (st != O0_OK) {
        return st;
    }
    st = vn->ops->list(vn, fn, ctx);
    o0_object_unref(&vn->obj);
    return st;
}
