/* Read-only VFS adapter for the Stage 4 OzeroFS proposal.
 * O0FS stays read-only here until ADR 0012's recovery gate is closed.
 */
#include <stddef.h>
#include <stdint.h>

#include "ozero/fs.h"
#include "ozero/vfs.h"
#include "ozero/mm.h"

#include "string.h"

struct o0_o0fs_vnode {
    struct o0_vnode vnode;
    struct o0fs *fs;
    char path[O0FS_PATH_MAX + 1u];
};

static void o0fs_vnode_destroy(struct o0_object *object) {
    (void)object;
}

static o0_status o0fs_vnode_new(struct o0fs *fs, struct o0_budget *budget,
                                const char *path, struct o0_o0fs_vnode **out) {
    struct o0fs_stat info;
    o0_status st = o0fs_stat(fs, path, &info);
    if (st != O0_OK) return st;
    if (info.type != O0FS_TYPE_FILE && info.type != O0FS_TYPE_DIR) return O0_EIO;
    struct o0_object *object = NULL;
    st = o0_object_alloc(O0_OBJ_VNODE, sizeof(struct o0_o0fs_vnode), budget,
                         o0fs_vnode_destroy, &object);
    if (st != O0_OK) return st;
    struct o0_o0fs_vnode *node = (struct o0_o0fs_vnode *)object;
    memset((uint8_t *)node + sizeof(node->vnode.obj), 0,
           sizeof(*node) - sizeof(node->vnode.obj));
    node->vnode.type = info.type == O0FS_TYPE_DIR
                           ? O0_VNODE_DIRECTORY : O0_VNODE_FILE;
    node->vnode.private_data = node;
    node->fs = fs;
    node->vnode.fs_identity = fs;
    size_t n = strlen(path);
    if (n > O0FS_PATH_MAX) {
        o0_object_unref(object);
        return O0_ERANGE;
    }
    memcpy(node->path, path, n + 1u);
    *out = node;
    return O0_OK;
}

static o0_status o0fs_vnode_lookup(struct o0_vnode *vnode, const char *name,
                                  size_t name_len, struct o0_vnode **out) {
    if (vnode == NULL || name == NULL || out == NULL ||
        vnode->type != O0_VNODE_DIRECTORY) return O0_ENOTDIR;
    *out = NULL;
    if (name_len == 0 || name_len > O0FS_NAME_MAX) return O0_ERANGE;
    struct o0_o0fs_vnode *parent = (struct o0_o0fs_vnode *)vnode;
    char path[O0FS_PATH_MAX + 1u];
    size_t prefix = strlen(parent->path);
    size_t separator = prefix == 1u ? 0u : 1u;
    if (prefix + separator + name_len > O0FS_PATH_MAX) return O0_ERANGE;
    memcpy(path, parent->path, prefix);
    size_t pos = prefix;
    if (separator != 0) path[pos++] = '/';
    memcpy(path + pos, name, name_len);
    path[pos + name_len] = '\0';
    struct o0_o0fs_vnode *child = NULL;
    o0_status st = o0fs_vnode_new(parent->fs, vnode->obj.budget, path, &child);
    if (st == O0_OK) {
        child->vnode.ops = vnode->ops;
        *out = &child->vnode;
    }
    return st;
}

static o0_status o0fs_vnode_read(struct o0_vnode *vnode, uint64_t offset,
                                void *buffer, size_t length, size_t *out_length) {
    if (vnode == NULL || vnode->type != O0_VNODE_FILE) return O0_EISDIR;
    struct o0_o0fs_vnode *node = (struct o0_o0fs_vnode *)vnode;
    return o0fs_read(node->fs, node->path, offset, buffer, length, out_length);
}

static o0_status o0fs_vnode_stat(struct o0_vnode *vnode,
                                 struct o0_vfs_stat *out) {
    if (vnode == NULL || out == NULL) return O0_EINVAL;
    struct o0_o0fs_vnode *node = (struct o0_o0fs_vnode *)vnode;
    struct o0fs_stat info;
    o0_status st = o0fs_stat(node->fs, node->path, &info);
    if (st != O0_OK) return st;
    out->size = info.size;
    out->object_id = info.inode;
    out->type = vnode->type;
    out->flags = 1u; /* backend is read-only */
    return O0_OK;
}

struct o0fs_readdir_context {
    uint64_t target;
    uint64_t index;
    struct o0_vfs_dirent *out;
    bool found;
};

static void o0fs_vnode_readdir_entry(void *opaque, const char *name,
                                    const struct o0fs_stat *info) {
    struct o0fs_readdir_context *ctx = opaque;
    if (ctx->index++ != ctx->target) return;
    size_t n = strlen(name);
    memset(ctx->out, 0, sizeof(*ctx->out));
    if (n > O0_VFS_NAME_MAX) return;
    ctx->out->object_id = info->inode;
    ctx->out->size = info->size;
    ctx->out->type = info->type == O0FS_TYPE_DIR
                         ? O0_VNODE_DIRECTORY : O0_VNODE_FILE;
    ctx->out->name_len = (uint32_t)n;
    memcpy(ctx->out->name, name, n + 1u);
    ctx->found = true;
}

static o0_status o0fs_vnode_readdir(struct o0_vnode *vnode, uint64_t *cursor,
                                    struct o0_vfs_dirent *out) {
    if (vnode == NULL || cursor == NULL || out == NULL) return O0_EINVAL;
    if (vnode->type != O0_VNODE_DIRECTORY) return O0_ENOTDIR;
    struct o0_o0fs_vnode *node = (struct o0_o0fs_vnode *)vnode;
    struct o0fs_readdir_context ctx = {
        .target = *cursor, .index = 0, .out = out, .found = false
    };
    o0_status st = o0fs_list(node->fs, node->path, o0fs_vnode_readdir_entry, &ctx);
    if (st != O0_OK) return st;
    if (!ctx.found) return O0_ENOENT;
    ++*cursor;
    return O0_OK;
}

static const struct o0_vnode_ops o0fs_vnode_ops = {
    .lookup = o0fs_vnode_lookup,
    .create = NULL,
    .read = o0fs_vnode_read,
    .write = NULL,
    .stat = o0fs_vnode_stat,
    .readdir = o0fs_vnode_readdir,
    .unlink = NULL
};

o0_status o0_vfs_o0fs_create(struct o0fs *fs, struct o0_budget *budget,
                              struct o0_vnode **root_out) {
    if (fs == NULL || budget == NULL || root_out == NULL) return O0_EINVAL;
    *root_out = NULL;
    struct o0_o0fs_vnode *root = NULL;
    o0_status st = o0fs_vnode_new(fs, budget, "/", &root);
    if (st != O0_OK) return st;
    root->vnode.ops = &o0fs_vnode_ops;
    *root_out = &root->vnode;
    return O0_OK;
}
