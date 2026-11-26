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
#include "ozero/sync.h"

#include "string.h"

static struct o0_vnode *vfs_root;
static void cache_init(void);
static void cache_invalidate_all(void);

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
    cache_init();
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
    cache_invalidate_all();
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

o0_status o0_vfs_lookup(struct o0_vnode *directory, const char *path,
                         size_t path_length, struct o0_vnode **out) {
    if (out == NULL) return O0_EINVAL;
    *out = NULL;
    return resolve_node(directory, path, path_length, out);
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
    file->reserved = 0;
    *out = file;
    return O0_OK;
}

#define O0_VFS_CACHE_PAGES 8u

struct o0_vfs_cache_page {
    struct o0_vnode *node;
    uint64_t index;
    uint64_t age;
    size_t valid_bytes;
    bool valid;
    uint8_t data[O0_PAGE_SIZE];
};

static struct o0_vfs_cache_page vfs_cache[O0_VFS_CACHE_PAGES];
static o0_spinlock_t vfs_cache_lock;
static uint64_t vfs_cache_age;
static bool vfs_cache_ready;

static void cache_init(void) {
    if (!vfs_cache_ready) {
        o0_spin_init_ordered(&vfs_cache_lock, 5u);
        vfs_cache_ready = true;
    }
}

static void cache_drop_page(struct o0_vfs_cache_page *page) {
    if (page->valid && page->node != NULL) vnode_unref(page->node);
    memset(page, 0, sizeof(*page));
}

static struct o0_vfs_cache_page *cache_find_locked(struct o0_vnode *node,
                                                    uint64_t index) {
    for (size_t i = 0; i < O0_VFS_CACHE_PAGES; ++i) {
        if (vfs_cache[i].valid && vfs_cache[i].node == node &&
            vfs_cache[i].index == index) {
            vfs_cache[i].age = ++vfs_cache_age;
            return &vfs_cache[i];
        }
    }
    return NULL;
}

/* Called with vfs_cache_lock held. The cache is write-through: no dirty page
 * can be lost on eviction or unmount. */
static o0_status cache_get_locked(struct o0_vnode *node, uint64_t index,
                                  bool read_existing,
                                  struct o0_vfs_cache_page **out) {
    struct o0_vfs_cache_page *page = cache_find_locked(node, index);
    if (page != NULL) { *out = page; return O0_OK; }
    size_t victim = 0;
    for (size_t i = 0; i < O0_VFS_CACHE_PAGES; ++i) {
        if (!vfs_cache[i].valid) { victim = i; break; }
        if (vfs_cache[i].age < vfs_cache[victim].age) victim = i;
    }
    page = &vfs_cache[victim];
    cache_drop_page(page);
    page->node = node;
    page->index = index;
    page->age = ++vfs_cache_age;
    page->valid = true;
    vnode_ref(node);
    memset(page->data, 0, sizeof(page->data));
    if (read_existing) {
        if (node->ops == NULL || node->ops->read == NULL) {
            cache_drop_page(page);
            return O0_ENOTSUP;
        }
        size_t got = 0;
        o0_status st = node->ops->read(node, index * O0_PAGE_SIZE,
                                       page->data, O0_PAGE_SIZE, &got);
        if (st != O0_OK) { cache_drop_page(page); return st; }
        page->valid_bytes = got;
        if (got < O0_PAGE_SIZE)
            memset(page->data + got, 0, O0_PAGE_SIZE - got);
    }
    *out = page;
    return O0_OK;
}

static void cache_invalidate_all(void) {
    if (!vfs_cache_ready) return;
    o0_spin_lock(&vfs_cache_lock);
    for (size_t i = 0; i < O0_VFS_CACHE_PAGES; ++i) cache_drop_page(&vfs_cache[i]);
    o0_spin_unlock(&vfs_cache_lock);
}

o0_status o0_vfs_read(struct o0_vfs_file *file, uint64_t offset, void *buffer,
                      size_t length, size_t *out_length) {
    if (file == NULL || out_length == NULL || (buffer == NULL && length != 0))
        return O0_EINVAL;
    *out_length = 0;
    if ((file->rights & O0_RIGHT_READ) == 0) return O0_EPERM;
    if (length > O0_VFS_IO_MAX || offset > UINT64_MAX - length) return O0_ERANGE;
    if (file->node->ops == NULL || file->node->ops->read == NULL) return O0_ENOTSUP;
    if (!vfs_cache_ready) return O0_EINVAL;
    o0_spin_lock(&vfs_cache_lock);
    size_t done = 0;
    while (done < length) {
        uint64_t position = offset + done;
        uint64_t page_index = position / O0_PAGE_SIZE;
        size_t in_page = (size_t)(position % O0_PAGE_SIZE);
        size_t take = O0_PAGE_SIZE - in_page;
        if (take > length - done) take = length - done;
        struct o0_vfs_cache_page *page = NULL;
        o0_status st = cache_get_locked(file->node, page_index, true, &page);
        if (st != O0_OK) {
            o0_spin_unlock(&vfs_cache_lock);
            *out_length = done;
            return done != 0 ? O0_OK : st;
        }
        if (in_page >= page->valid_bytes) break;
        if (take > page->valid_bytes - in_page)
            take = page->valid_bytes - in_page;
        memcpy((uint8_t *)buffer + done, page->data + in_page, take);
        done += take;
    }
    o0_spin_unlock(&vfs_cache_lock);
    *out_length = done;
    return O0_OK;
}

o0_status o0_vfs_write(struct o0_vfs_file *file, uint64_t offset,
                       const void *buffer, size_t length, size_t *out_length) {
    if (file == NULL || out_length == NULL || (buffer == NULL && length != 0))
        return O0_EINVAL;
    *out_length = 0;
    if ((file->rights & O0_RIGHT_WRITE) == 0) return O0_EPERM;
    if (length > O0_VFS_IO_MAX || offset > UINT64_MAX - length) return O0_ERANGE;
    if (file->node->ops == NULL || file->node->ops->write == NULL) return O0_EPERM;
    if (!vfs_cache_ready) return O0_EINVAL;
    o0_spin_lock(&vfs_cache_lock);
    size_t done = 0;
    while (done < length) {
        uint64_t position = offset + done;
        uint64_t page_index = position / O0_PAGE_SIZE;
        size_t in_page = (size_t)(position % O0_PAGE_SIZE);
        size_t take = O0_PAGE_SIZE - in_page;
        if (take > length - done) take = length - done;
        bool full_page = in_page == 0 && take == O0_PAGE_SIZE;
        struct o0_vfs_cache_page *page = NULL;
        o0_status st = cache_get_locked(file->node, page_index, !full_page, &page);
        if (st != O0_OK) {
            o0_spin_unlock(&vfs_cache_lock);
            return done != 0 ? O0_OK : st;
        }
        st = file->node->ops->write(file->node, position,
                                    (const uint8_t *)buffer + done, take,
                                    out_length);
        if (st != O0_OK || *out_length != take) {
            cache_drop_page(page);
            o0_spin_unlock(&vfs_cache_lock);
            *out_length = done;
            return st != O0_OK ? st : O0_EIO;
        }
        memcpy(page->data + in_page, (const uint8_t *)buffer + done, take);
        if (in_page + take > page->valid_bytes)
            page->valid_bytes = in_page + take;
        done += take;
    }
    o0_spin_unlock(&vfs_cache_lock);
    *out_length = done;
    return O0_OK;
}

o0_status o0_vfs_stat(struct o0_vnode *directory, const char *path,
                      size_t path_length, struct o0_vfs_stat *out) {
    if (out == NULL) return O0_EINVAL;
    if (directory != NULL && path != NULL && path_length == 1u && path[0] == '.' && path[1] == '\\0') {
        if (directory->mounted_root != NULL) directory = directory->mounted_root;
        if (directory->ops == NULL || directory->ops->stat == NULL) return O0_ENOTSUP;
        return directory->ops->stat(directory, out);
    }
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

o0_status o0_vfs_mkdir(struct o0_vnode *directory, const char *path,
                       size_t path_length) {
    struct o0_vnode *parent = NULL;
    const char *leaf = NULL;
    size_t leaf_len = 0;
    o0_status st = resolve_parent(directory, path, path_length, &parent,
                                  &leaf, &leaf_len);
    if (st != O0_OK) return st;
    if (parent->ops == NULL || parent->ops->create == NULL) st = O0_ENOTSUP;
    else {
        struct o0_vnode *created = NULL;
        st = parent->ops->create(parent, leaf, leaf_len,
                                 O0_VNODE_DIRECTORY, &created);
        if (created != NULL) vnode_unref(created);
    }
    vnode_unref(parent);
    return st;
}
