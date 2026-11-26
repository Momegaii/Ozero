/* Ozero tmpfs: bounded in-memory reference VFS backend. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/vfs.h"
#include "ozero/mm.h"
#include "ozero/sync.h"

#include "string.h"

struct o0_tmpfs_state {
    struct o0_budget *budget;
    size_t capacity;
    size_t used;
    uint32_t references;
    uint32_t next_id;
    o0_spinlock_t lock;
};

struct o0_tmpfs_node {
    struct o0_vnode vnode;
    struct o0_tmpfs_state *fs;
    struct o0_tmpfs_node *children;
    struct o0_tmpfs_node *next;
    char name[O0_VFS_NAME_MAX + 1u];
    uint64_t id;
    uint64_t size;
    size_t data_capacity;
    uint8_t *data;
};

static struct o0_tmpfs_node *tmpfs_node(struct o0_vnode *vnode) {
    return (struct o0_tmpfs_node *)vnode;
}

static void tmpfs_state_put(struct o0_tmpfs_state *fs) {
    if (fs == NULL || --fs->references != 0) return;
    struct o0_budget *budget = fs->budget;
    o0_kfree(fs, sizeof(*fs));
    o0_budget_uncharge(budget, sizeof(*fs));
    o0_object_unref(&budget->obj);
}

static void tmpfs_node_destroy(struct o0_object *object) {
    struct o0_tmpfs_node *node = (struct o0_tmpfs_node *)object;
    struct o0_tmpfs_state *fs = node->fs;
    if (node->vnode.type == O0_VNODE_DIRECTORY) {
        struct o0_tmpfs_node *child = node->children;
        while (child != NULL) {
            struct o0_tmpfs_node *next = child->next;
            child->next = NULL;
            o0_object_unref(&child->vnode.obj);
            child = next;
        }
    }
    if (node->data != NULL) {
        o0_kfree(node->data, node->data_capacity);
        o0_budget_uncharge(fs->budget, node->data_capacity);
        if (fs->used >= node->data_capacity) fs->used -= node->data_capacity;
        node->data = NULL;
    }
    tmpfs_state_put(fs);
}

static o0_status tmpfs_node_create(struct o0_tmpfs_state *fs, uint32_t type,
                                   const char *name, size_t name_len,
                                   struct o0_tmpfs_node **out) {
    if (fs == NULL || out == NULL ||
        (type != O0_VNODE_FILE && type != O0_VNODE_DIRECTORY) ||
        name_len > O0_VFS_NAME_MAX) return O0_EINVAL;
    struct o0_object *object = NULL;
    o0_status st = o0_object_alloc(O0_OBJ_VNODE, sizeof(struct o0_tmpfs_node),
                                   fs->budget, tmpfs_node_destroy, &object);
    if (st != O0_OK) return st;
    struct o0_tmpfs_node *node = (struct o0_tmpfs_node *)object;
    memset(node, 0, sizeof(*node));
    node->vnode.obj = *object;
    node->vnode.ops = NULL;
    node->vnode.private_data = node;
    node->vnode.type = type;
    node->fs = fs;
    ++fs->references;
    node->id = ++fs->next_id;
    if (name != NULL && name_len != 0) {
        memcpy(node->name, name, name_len);
        node->name[name_len] = '\0';
    }
    *out = node;
    return O0_OK;
}

static o0_status tmpfs_lookup(struct o0_vnode *dir, const char *name,
                              size_t name_len, struct o0_vnode **out) {
    if (dir == NULL || name == NULL || out == NULL ||
        dir->type != O0_VNODE_DIRECTORY) return O0_ENOTDIR;
    *out = NULL;
    struct o0_tmpfs_node *parent = tmpfs_node(dir);
    struct o0_tmpfs_state *fs = parent->fs;
    o0_spin_lock(&fs->lock);
    for (struct o0_tmpfs_node *child = parent->children; child != NULL;
         child = child->next) {
        if (strlen(child->name) == name_len &&
            memcmp(child->name, name, name_len) == 0) {
            o0_object_ref(&child->vnode.obj);
            *out = &child->vnode;
            o0_spin_unlock(&fs->lock);
            return O0_OK;
        }
    }
    o0_spin_unlock(&fs->lock);
    return O0_ENOENT;
}

static o0_status tmpfs_create_node(struct o0_vnode *dir, const char *name,
                                   size_t name_len, uint32_t type,
                                   struct o0_vnode **out) {
    if (dir == NULL || name == NULL || out == NULL ||
        dir->type != O0_VNODE_DIRECTORY) return O0_ENOTDIR;
    *out = NULL;
    struct o0_tmpfs_node *parent = tmpfs_node(dir);
    struct o0_tmpfs_state *fs = parent->fs;
    o0_spin_lock(&fs->lock);
    for (struct o0_tmpfs_node *it = parent->children; it != NULL; it = it->next) {
        if (strlen(it->name) == name_len && memcmp(it->name, name, name_len) == 0) {
            o0_spin_unlock(&fs->lock);
            return O0_EEXIST;
        }
    }
    struct o0_tmpfs_node *node = NULL;
    o0_status st = tmpfs_node_create(fs, type, name, name_len, &node);
    if (st == O0_OK) {
        node->vnode.ops = dir->ops;
        node->next = parent->children;
        parent->children = node; /* object reference is owned by the directory */
        o0_object_ref(&node->vnode.obj);
        *out = &node->vnode; /* second reference is returned to caller */
    }
    o0_spin_unlock(&fs->lock);
    return st;
}

static o0_status tmpfs_read(struct o0_vnode *vnode, uint64_t offset,
                            void *buffer, size_t length, size_t *out_length) {
    if (vnode == NULL || out_length == NULL || (buffer == NULL && length != 0))
        return O0_EINVAL;
    *out_length = 0;
    if (vnode->type != O0_VNODE_FILE) return O0_EISDIR;
    struct o0_tmpfs_node *node = tmpfs_node(vnode);
    struct o0_tmpfs_state *fs = node->fs;
    o0_spin_lock(&fs->lock);
    if (offset >= node->size || length == 0) {
        o0_spin_unlock(&fs->lock);
        return O0_OK;
    }
    uint64_t available = node->size - offset;
    size_t count = available < length ? (size_t)available : length;
    memcpy(buffer, node->data + (size_t)offset, count);
    *out_length = count;
    o0_spin_unlock(&fs->lock);
    return O0_OK;
}

static o0_status tmpfs_write(struct o0_vnode *vnode, uint64_t offset,
                             const void *buffer, size_t length,
                             size_t *out_length) {
    if (vnode == NULL || out_length == NULL || (buffer == NULL && length != 0))
        return O0_EINVAL;
    *out_length = 0;
    if (vnode->type != O0_VNODE_FILE) return O0_EISDIR;
    if (length == 0) return O0_OK;
    if (offset > SIZE_MAX || length > SIZE_MAX - (size_t)offset) return O0_ERANGE;
    size_t end = (size_t)offset + length;
    struct o0_tmpfs_node *node = tmpfs_node(vnode);
    struct o0_tmpfs_state *fs = node->fs;
    o0_spin_lock(&fs->lock);
    if (end > node->data_capacity) {
        size_t capacity = node->data_capacity == 0 ? 64u : node->data_capacity;
        while (capacity < end) {
            if (capacity > SIZE_MAX / 2u) { capacity = end; break; }
            capacity *= 2u;
        }
        if (capacity > fs->capacity - fs->used) {
            o0_spin_unlock(&fs->lock);
            return O0_ENOSPC;
        }
        o0_status st = o0_budget_charge(fs->budget, capacity);
        if (st != O0_OK) {
            o0_spin_unlock(&fs->lock);
            return st;
        }
        uint8_t *data = o0_kalloc(capacity, 16);
        if (data == NULL) {
            o0_budget_uncharge(fs->budget, capacity);
            o0_spin_unlock(&fs->lock);
            return O0_ENOMEM;
        }
        memset(data, 0, capacity);
        if (node->data != NULL) {
            memcpy(data, node->data, (size_t)node->size);
            o0_kfree(node->data, node->data_capacity);
            o0_budget_uncharge(fs->budget, node->data_capacity);
            fs->used -= node->data_capacity;
        }
        node->data = data;
        fs->used += capacity;
        node->data_capacity = capacity;
    }
    if ((uint64_t)end > node->size) {
        memset(node->data + (size_t)node->size, 0, end - (size_t)node->size);
        node->size = end;
    }
    memcpy(node->data + (size_t)offset, buffer, length);
    *out_length = length;
    o0_spin_unlock(&fs->lock);
    return O0_OK;
}

static o0_status tmpfs_stat(struct o0_vnode *vnode, struct o0_vfs_stat *out) {
    if (vnode == NULL || out == NULL) return O0_EINVAL;
    struct o0_tmpfs_node *node = tmpfs_node(vnode);
    out->size = node->size;
    out->object_id = node->id;
    out->type = vnode->type;
    out->flags = 0;
    return O0_OK;
}

static o0_status tmpfs_readdir(struct o0_vnode *vnode, uint64_t *cursor,
                               struct o0_vfs_dirent *out) {
    if (vnode == NULL || cursor == NULL || out == NULL) return O0_EINVAL;
    if (vnode->type != O0_VNODE_DIRECTORY) return O0_ENOTDIR;
    struct o0_tmpfs_node *dir = tmpfs_node(vnode);
    struct o0_tmpfs_state *fs = dir->fs;
    o0_spin_lock(&fs->lock);
    struct o0_tmpfs_node *selected = NULL;
    for (struct o0_tmpfs_node *child = dir->children; child != NULL;
         child = child->next) {
        if (child->id >= *cursor && (selected == NULL || child->id < selected->id))
            selected = child;
    }
    if (selected == NULL) {
        o0_spin_unlock(&fs->lock);
        return O0_ENOENT;
    }
    memset(out, 0, sizeof(*out));
    out->object_id = selected->id;
    out->size = selected->size;
    out->type = selected->vnode.type;
    out->name_len = (uint32_t)strlen(selected->name);
    memcpy(out->name, selected->name, out->name_len + 1u);
    *cursor = selected->id + 1u;
    o0_spin_unlock(&fs->lock);
    return O0_OK;
}

static o0_status tmpfs_unlink(struct o0_vnode *vnode, const char *name,
                              size_t name_len) {
    if (vnode == NULL || name == NULL || vnode->type != O0_VNODE_DIRECTORY)
        return O0_ENOTDIR;
    struct o0_tmpfs_node *dir = tmpfs_node(vnode);
    struct o0_tmpfs_state *fs = dir->fs;
    o0_spin_lock(&fs->lock);
    struct o0_tmpfs_node **link = &dir->children;
    while (*link != NULL) {
        struct o0_tmpfs_node *child = *link;
        if (strlen(child->name) == name_len &&
            memcmp(child->name, name, name_len) == 0) {
            if (child->vnode.type == O0_VNODE_DIRECTORY && child->children != NULL) {
                o0_spin_unlock(&fs->lock);
                return O0_ENOTEMPTY;
            }
            *link = child->next;
            child->next = NULL;
            o0_spin_unlock(&fs->lock);
            o0_object_unref(&child->vnode.obj); /* drop directory's ownership */
            return O0_OK;
        }
        link = &(*link)->next;
    }
    o0_spin_unlock(&fs->lock);
    return O0_ENOENT;
}

static const struct o0_vnode_ops tmpfs_ops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create_node,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .stat = tmpfs_stat,
    .readdir = tmpfs_readdir,
    .unlink = tmpfs_unlink
};

o0_status o0_tmpfs_create(struct o0_budget *budget, size_t capacity,
                          struct o0_vnode **root_out) {
    if (budget == NULL || root_out == NULL || capacity == 0) return O0_EINVAL;
    *root_out = NULL;
    o0_status st = o0_budget_charge(budget, sizeof(struct o0_tmpfs_state));
    if (st != O0_OK) return st;
    struct o0_tmpfs_state *fs = o0_kalloc(sizeof(*fs), 16);
    if (fs == NULL) {
        o0_budget_uncharge(budget, sizeof(*fs));
        return O0_ENOMEM;
    }
    memset(fs, 0, sizeof(*fs));
    fs->budget = budget;
    fs->capacity = capacity;
    fs->references = 0;
    fs->next_id = 0;
    o0_spin_init(&fs->lock);
    o0_object_ref(&budget->obj);

    struct o0_tmpfs_node *root = NULL;
    st = tmpfs_node_create(fs, O0_VNODE_DIRECTORY, NULL, 0, &root);
    if (st != O0_OK) {
        o0_kfree(fs, sizeof(*fs));
        o0_budget_uncharge(budget, sizeof(*fs));
        o0_object_unref(&budget->obj);
        return st;
    }
    root->vnode.ops = &tmpfs_ops;
    *root_out = &root->vnode;
    return O0_OK;
}
