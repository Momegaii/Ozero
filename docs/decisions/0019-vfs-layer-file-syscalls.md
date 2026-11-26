# ADR 0019: VFS layer and file syscalls (Wave 4.6)

- **Status:** Accepted
- **Date:** 2026-11-26
- **Wave:** 4.6 (VFS + file syscalls)
- **Related:** ADR 0008 (capability handles, budgets, IPC), ADR 0018 (O0FS redo journal),
  `include/ozero/vfs.h`, `kernel/src/vfs.c`, `kernel/src/syscall.c`,
  `include/ozero/uapi.h`, `include/ozero/fs.h`, `tests/vfs_test.c`

## 1. Context

Stage 4 storage needs a file API: user programs must be able to open, read,
write, close, stat and list files on a mounted filesystem. Wave 4.5 delivered
a crash-consistent O0FS with inode-level operations
(`o0fs_inode_read/write/stat`). Wave 4.6 adds the VFS layer that sits between
the file syscalls and concrete filesystems, plus the file syscalls themselves.

Ozero defines its own native execution architecture and does not default to
Linux conventions. No ELF, ELF64, Linux syscall, glibc or Linux userspace
assumption appears in this code. The VFS design — mount table, vnodes as
kernel objects, open files as handle-backed objects, path resolution — is
Ozero's own. File descriptors are handles to `o0_file` objects; vnodes are
`o0_object`s of type `O0_OBJ_VNODE`.

## 2. Alternatives

| Alternative | How it works | Trade-off |
|---|---|---|
| **VFS layer with vnode + open file objects (implemented)** | Mount table tracks mounted filesystems. Vnodes reference inodes in a mounted fs and are refcounted kernel objects. Open files pair a vnode with a cursor offset and access flags. File descriptors are handles to open-file objects. Path resolution walks from the root mount. | Clean separation of concerns; handles give built-in rights and lifecycle management; vnode ops vector lets different filesystems plug in. More layers than a direct fs syscall. |
| **Direct filesystem syscalls** | Syscalls call o0fs_* functions directly with a path; no mount table, no vnode abstraction. | Simpler but no multi-filesystem support, no per-open cursor, no handle-based rights. Rejected: doesn't scale beyond one filesystem. |
| **Unix-style file descriptor table** | Per-process fd array separate from the handle table. | Duplicates the handle mechanism; Ozero already has a per-process capability handle table (ADR 0008). Rejected: redundancy. |

## 3. Decision

### 3.1 Mount table

A fixed kernel array (`O0_MAX_MOUNTS = 8`) of `o0_mount` entries. Each entry
holds a block device, an `o0fs` handle, and a root vnode. `o0_vfs_init()`
zeroes the table; `o0_mount_install()` claims a free slot; `o0_mount_get()`
looks up by index; `o0_mount_release()` clears a slot. Index 0 is the root
mount by convention.

### 3.2 Vnodes

A vnode is a `struct o0_vnode` embedded in an `o0_object` of type
`O0_OBJ_VNODE`. It holds a back-pointer to its filesystem, an inode number,
and a pointer to a `o0_vnode_ops` vector (read, write, stat, lookup, list).
`o0_vnode_create()` allocates and initialises one; `o0_object_unref()` frees
it. The vnode does not own its filesystem.

### 3.3 Open files

An open file is a `struct o0_file` embedded in an `o0_object` of type
`O0_OBJ_FILE`. It holds a vnode reference, a 64-bit cursor offset, and access
flags (`O0_O_RDONLY`, `O0_O_WRONLY`, `O0_O_RDWR`, `O0_O_CREAT`,
`O0_O_DIRECTORY`). `o0_file_create()` allocates and initialises one.

### 3.4 File descriptors

File descriptors are handles to `o0_file` objects in the caller's handle
table. `o0_vfs_open()` resolves a path, creates a vnode and an open file, then
installs a handle with `O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_DUP`.
`o0_vfs_read()`/`o0_vfs_write()` look up the handle, check the right, call
the vnode op, advance the cursor, and release the object reference.
`o0_vfs_close()` closes the handle.

### 3.5 Syscall numbers

New syscall numbers (17–22), appended after the Wave 3 set:

| Number | Name |
|---|---|
| 17 | `O0_SYS_OPEN` |
| 18 | `O0_SYS_READ` |
| 19 | `O0_SYS_WRITE` |
| 20 | `O0_SYS_CLOSE` |
| 21 | `O0_SYS_STAT` |
| 22 | `O0_SYS_READDIR` |

### 3.6 User ABI structures

`o0_uapi_stat` (st_size, st_blocks, st_mode, st_links, reserved) and
`o0_uapi_dirent` (d_ino, d_reclen, d_namlen, d_type, d_name[24]) are added to
`include/ozero/uapi.h`. These are Ozero-native, not `stat`/`dirent` from any
other system.

### 3.7 Path resolution

`o0_vfs_lookup_path()` resolves an absolute path against mount 0 using the
filesystem's path resolver (`o0fs_lookup`), then wraps the resulting inode in
a vnode. Subdirectory traversal within a directory's data blocks is
`O0_ENOSYS` for V1 — only single-component lookups from the root are
supported; full recursive resolution is deferred.

### 3.8 Ozero-native usercopy

All user pointers go through `o0_copy_from_user` / `o0_copy_to_user` after an
`o0_user_range_check`. Read and write use a 4 KiB bounce buffer. No syscall
dereferences user memory directly.

## 4. Consequences

- File syscalls return `-O0_ENOSYS` for operations the VFS doesn't yet
  implement (subdirectory lookup, directory listing, etc.), not silent
  success.
- Handle-based file descriptors inherit the handle table's rights checking
  and generation validation.
- The mount table is fixed-size; `O0_ENOMEM` when full.
- Write path uses a bounce buffer, limiting single syscall writes to 4 KiB.
  Larger writes require multiple syscalls.
- Every metadata-modifying operation on the underlying filesystem is a
  journaled transaction (Wave 4.5), so file syscalls inherit crash
  consistency.

## 5. Test plan

Host (`make test`, `make test-sanitize`):

- `tests/vfs_test.c`: mount table install/get/release; lookup/stat/listdir
  with no mount return `O0_ENOENT`; vnode/file creation rejects invalid args;
  full format-mount-vnode-create-stat-unmount cycle; open/read/write with
  invalid handle returns errors.
- Existing `tests/fs_test.c` and `tests/o0fs_journal_test.c` continue to
  pass, confirming the VFS layer doesn't break O0FS.

## 6. Open questions

- Recursive path resolution (walking `a/b/c` component by component).
- Directory listing for subdirectories (not just root).
- `O0_O_CREAT` semantics: create-if-missing vs always-create.
- Whether `O0_O_DIRECTORY` should be enforced at the VFS or filesystem layer.
- Per-process open-file limits beyond `O0_HANDLE_MAX`.