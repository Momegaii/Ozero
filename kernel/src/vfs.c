/* Ozero VFS core. See ADR 0017.
 *
 * Path traversal is relative to an explicit directory vnode capability.
 * Backends never receive user pointers and cannot grant authority.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/vfs.h"
#include "ozero/mm.h"

#include "string.h"

static struct o0_vnode *vfs_root;

static void vnode_ref(struct o0_vnode *node) {
    if (node != NULL) o0_object_ref(&node->obj);
}

static void vnode_unref(struct o0_vnode *node) {
    if (node != NULL) o0_object_unref(&node->obj);
}

static void file_destroy(struct o0_object *object) {
    struct o0_vfs_file *file = (struct o0_vfs_file *)object;
    vnode_unref(file->node);
    file->node = NULL;
}

static o0_status path_component(const char **cursor, const char **name,
                                size_t *name_len) {
    const char *p = *cursor;
    while (*p == '/') ++p;
    if (*p == '\0') {
        *cursor = p;
        *name = NULL;
        *name_len = 0;
        return O0_OK;
    }
    const char *start = p;
    while (*p != '\0' && *p != '/') ++p;
    size_t n = (size_t)(p - start);
    if (n == 0 || n > O0_VFS_NAME_MAX) return O0_ERANGE;
    *cursor = p;
    *name = start;
    *name_len = n;
    return O0_OK;
}

static bool component_is(const char *name, size_t length, const char *literal) {
    size_t n = strlen(literal);
    return n == length && memcmp(name, literal, n) == 0;
}

/* Resolve all components except the final one. Returns a referenced parent. */
static o0_status resolve_parent(struct o0_vnode *directory, const char *path,
                                size_t path_length, struct o0_vnode **parent_out,
                                const char **leaf_out, size_t *leaf_len_out) {
    if (directory == NULL || path == NULL || parent_out == NULL ||
        leaf_out == NULL || leaf_len_out == NULL) return O0_EINVAL;
    *parent_out = NULL;
    *leaf_out = NULL;
    *leaf_len_out = 0;
    if (path_length == 0 || path_length > O0_VFS_PATH_MAX ||
        path[0] == '/' || path[path_length] != '\0' ||
        memchr(path, '\0', path_length) != NULL) return O0_EINVAL;

    struct o0_vnode *current = directory;
    vnode_ref(current);
    const char *cursor = path;
    uint32_t depth = 0;
    for (;;) {
        const char *name = NULL;
        size_t name_len = 0;
        o0_status st = path_component(&cursor, &name, &name_len);
        if (st != O0_OK) { vnode_unref(current); return st; }
        if (name == NULL) { vnode_unref(current); return O0_EINVAL; }
        if (component_is(name, name_len, "..")) {
            vnode_unref(current);
            return O0_EPERM;
        }
        if (component_is(name, name_len, ".")) {
            while (*cursor == '/') ++cursor;
            if (*cursor == '\0') { vnode_unref(current); return O0_EINVAL; }
            continue;
        }
        if (++depth > O0_VFS_MAX_DEPTH) {
            vnode_unref(current);
            return O0_ERANGE;
        }
        const char *lookahead = cursor;
        while (*lookahead == '/') ++lookahead;
        if (*lookahead == '\0') {
            *parent_out = current;
            *leaf_out = name;
            *leaf_len_out = name_len;
            return O0_OK;
        }
        if (current->type != O0_VNODE_DIRECTORY || current->ops == NULL ||
            current->ops->lookup == NULL) {
            vnode_unref(current);
            return O0_ENOTDIR;
        }
        struct o0_vnode *next = NULL;
        st = current->ops->lookup(current, name, name_len, &next);
        if (st != O0_OK) { vnode_unref(current); return st; }
        vnode_unref(current);
        if (next == NULL) return O0_EIO;
        current = next;
        if (current->mounted_root != NULL) {
            struct o0_vnode *mounted = current->mounted_root;
            vnode_ref(mounted);
            vnode_unref(current);
            current = mounted;
        }
    }
}

static o0_status resolve_node(struct o0_vnode *directory, const char *path,
                              size_t path_length, struct o0_vnode **out) {
    struct o0_vnode *parent = NULL;
    const char *leaf = NULL;
    size_t leaf_len = 0;
    o0_status st = resolve_parent(directory, path, path_length, &parent,
                                  &leaf, &leaf_len);
    if (st != O0_OK) return st;
    if (parent->type != O0_VNODE_DIRECTORY || parent->ops == NULL ||
        parent->ops->lookup == NULL) {
        vnode_unref(parent);
        return O0_ENOTDIR;
    }
    st = parent->ops->lookup(parent, leaf, leaf_len, out);
    vnode_unref(parent);
    if (st == O0_OK && *out != NULL && (*out)->mounted_root != NULL) {
        struct o0_vnode *mounted = (*out)->mounted_root;
        vnode_ref(mounted);
        vnode_unref(*out);
        *out = mounted;
    }
    return st;
}

o0_status o0_vfs_mount_root(struct o0_vnode *root) {
    if (root == NULL || root->type != O0_VNODE_DIRECTORY) return O0_EINVAL;
    if (vfs_root != NULL) return O0_EBUSY;
    vnode_ref(root);
    vfs_root = root;
    return O0_OK;
}

o0_status o0_vfs_mount(struct o0_vnode *directory, struct o0_vnode *root) {
    if (directory == NULL || root == NULL ||
        directory->type != O0_VNODE_DIRECTORY ||
        root->type != O0_VNODE_DIRECTORY) return O0_EINVAL;
    if (directory->mounted_root != NULL) return O0_EBUSY;
    vnode_ref(root);
    directory->mounted_root = root;
    return O0_OK;
}

o0_status o0_vfs_unmount(struct o0_vnode *directory) {
    if (directory == NULL || directory->mounted_root == NULL) return O0_EINVAL;
    struct o0_vnode *root = directory->mounted_root;
    /* The mount's reference must be the only remaining reference. */
    if (root->obj.refcount != 1) return O0_EBUSY;
    directory->mounted_root = NULL;
    vnode_unref(root);
    return O0_OK;
}

o0_status o0_vfs_root(struct o0_vnode **out) {
    if (out == NULL) return O0_EINVAL;
    *out = NULL;
    if (vfs_root == NULL) return O0_ENOENT;
    vnode_ref(vfs_root);
    *out = vfs_root;
    return O0_OK;
}

o0_status o0_vfs_open(struct o0_vnode *directory, const char *path,
                      size_t path_length, uint32_t flags, uint32_t rights,
                      struct o0_budget *budget, struct o0_vfs_file **out) {
    if (out == NULL || directory == NULL || budget == NULL ||
        (rights & ~(O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_INFO |
                    O0_RIGHT_DUP | O0_RIGHT_TRANSFER)) != 0 ||
        (rights & (O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_INFO)) == 0 ||
        (flags & ~(O0_VFS_OPEN_CREATE | O0_VFS_OPEN_EXCLUSIVE)) != 0) {
        return O0_EINVAL;
    }
    *out = NULL;
    struct o0_vnode *node = NULL;
    o0_status st = resolve_node(directory, path, path_length, &node);
    if (st == O0_ENOENT && (flags & O0_VFS_OPEN_CREATE) != 0) {
        struct o0_vnode *parent = NULL;
        const char *leaf = NULL;
        size_t leaf_len = 0;
        st = resolve_parent(directory, path, path_length, &parent, &leaf, &leaf_len);
        if (st == O0_OK) {
            if (parent->ops == NULL || parent->ops->create == NULL) st = O0_ENOTSUP;
            else st = parent->ops->create(parent, leaf, leaf_len, O0_VNODE_FILE, &node);
            vnode_unref(parent);
        }
    } else if (st == O0_OK && (flags & O0_VFS_OPEN_EXCLUSIVE) != 0 &&
               (flags & O0_VFS_OPEN_CREATE) != 0) {
        vnode_unref(node);
        return O0_EEXIST;
    }
    if (st != O0_OK) return st;
    if (node == NULL) return O0_EIO;
    if (node->type != O0_VNODE_FILE) {
        vnode_unref(node);
        return O0_EISDIR;
    }
    if ((rights & O0_RIGHT_WRITE) != 0 &&
        (node->ops == NULL || node->ops->write == NULL)) {
        vnode_unref(node);
        return O0_EPERM;
    }
    struct o0_object *object = NULL;
    st = o0_object_alloc(O0_OBJ_FILE, sizeof(struct o0_vfs_file), budget,
                         file_destroy, &object);
    if (st != O0_OK) {
        vnode_unref(node);
        return st;
    }
    struct o0_vfs_file *file = (struct o0_vfs_file *)object;
    file->node = node; /* take the resolver's reference */
    file->rights = rights;
    *out = file;
    return O0_OK;
}

o0_status o0_vfs_read(struct o0_vfs_file *file, uint64_t offset, void *buffer,
                      size_t length, size_t *out_length) {
    if (file == NULL || out_length == NULL || (buffer == NULL && length != 0))
        return O0_EINVAL;
    *out_length = 0;
    if ((file->rights & O0_RIGHT_READ) == 0) return O0_EPERM;
    if (length > O0_VFS_IO_MAX || offset > UINT64_MAX - length) return O0_ERANGE;
    if (file->node->ops == NULL || file->node->ops->read == NULL) return O0_ENOTSUP;
    return file->node->ops->read(file->node, offset, buffer, length, out_length);
}

o0_status o0_vfs_write(struct o0_vfs_file *file, uint64_t offset,
                       const void *buffer, size_t length, size_t *out_length) {
    if (file == NULL || out_length == NULL || (buffer == NULL && length != 0))
        return O0_EINVAL;
    *out_length = 0;
    if ((file->rights & O0_RIGHT_WRITE) == 0) return O0_EPERM;
    if (length > O0_VFS_IO_MAX || offset > UINT64_MAX - length) return O0_ERANGE;
    if (file->node->ops == NULL || file->node->ops->write == NULL) return O0_EPERM;
    return file->node->ops->write(file->node, offset, buffer, length, out_length);
}

o0_status o0_vfs_stat(struct o0_vnode *directory, const char *path,
                      size_t path_length, struct o0_vfs_stat *out) {
    if (out == NULL) return O0_EINVAL;
    struct o0_vnode *node = NULL;
    o0_status st = resolve_node(directory, path, path_length, &node);
    if (st != O0_OK) return st;
    if (node->ops == NULL || node->ops->stat == NULL) st = O0_ENOTSUP;
    else st = node->ops->stat(node, out);
    vnode_unref(node);
    return st;
}

o0_status o0_vfs_readdir(struct o0_vnode *directory, uint64_t *cursor,
                         struct o0_vfs_dirent *out) {
    if (directory == NULL || cursor == NULL || out == NULL) return O0_EINVAL;
    if (directory->type != O0_VNODE_DIRECTORY) return O0_ENOTDIR;
    if (directory->mounted_root != NULL) directory = directory->mounted_root;
    if (directory->ops == NULL || directory->ops->readdir == NULL) return O0_ENOTSUP;
    return directory->ops->readdir(directory, cursor, out);
}

o0_status o0_vfs_unlink(struct o0_vnode *directory, const char *path,
                        size_t path_length) {
    struct o0_vnode *parent = NULL;
    const char *leaf = NULL;
    size_t leaf_len = 0;
    o0_status st = resolve_parent(directory, path, path_length, &parent,
                                  &leaf, &leaf_len);
    if (st != O0_OK) return st;
    if (parent->ops == NULL || parent->ops->unlink == NULL) st = O0_ENOTSUP;
    else st = parent->ops->unlink(parent, leaf, leaf_len);
    vnode_unref(parent);
    return st;
}
