# ADR 0017: VFS, directory authority and file capabilities

- **Status:** Accepted — Director-approved implementation boundary, 2026-11-26.
- **Date:** 2026-11-26
- **Wave:** 4.6
- **Related:** ADR 0007 (native ABI), ADR 0008 (capabilities), ADR 0012 (O0FS crash
  consistency), ADR 0013 (O0FS format versioning), `docs/wave4.md`.

## 1. Context

Wave 4.6 needs filesystem-independent file access. ADR 0008 explicitly rejects a
POSIX file-descriptor surface and ambient authority. The existing OzeroFS is a
path-based Stage 4 proposal; it has no journal and no crash-recovery guarantee.
The VFS must not make those limitations invisible or turn O0FS into the VFS
contract.

Ozero's native ABI remains independent. A future Windows execution layer must
translate its own file/object contracts to Ozero capabilities; it does not
change native handle semantics.

## 2. Decisions

1. **VFS boundary.** Filesystems provide vnode operations. VFS owns mount
   selection, path walking, vnode references and the common metadata/dirent
   representation. Filesystem-specific inode numbers and disk layouts do not
   escape the backend.
2. **Directory authority.** Path operations are relative to an explicit
   directory capability supplied by the caller. The kernel does not resolve
   paths from a process-global or implicit root. The boot supervisor grants the root directory only to an explicitly selected
   bootstrap process; the root is never installed as an implicit process default. Child processes receive it only when the parent explicitly grants
   it through the existing spawn-transfer mechanism. A grant may reduce rights,
   never increase them.
3. **File capabilities.** An opened file is represented by an opaque,
   generation-protected per-process handle to a refcounted file object. Opening
   a directory returns a vnode capability carrying only its explicitly granted
   directory rights. The
   handle table records READ, WRITE, INFO, DUP and TRANSFER rights. The kernel
   never exposes a pointer, inode number or reusable slot index as authority.
4. **No POSIX descriptor model.** There is no process-wide descriptor namespace,
   implicit current offset, `errno`, `dup2`, `fork`, or POSIX lifecycle.
   Read/write calls carry an explicit 64-bit offset. Closing a file capability
   uses the existing generic `handle_close` operation; there is no separate
   file-close syscall.
5. **Path rules.** Paths are bounded and relative to the supplied directory
   capability. Repeated separators are collapsed. Explicit `.` components are
   rejected during traversal (standalone `.` is accepted only by `file_stat` to
   query the granted directory itself); `..` is always rejected. Symlinks are not part
   of this wave. Component and traversal limits are enforced before backend
   calls. Path components are byte strings (not Unicode); invalid UTF-8 is retained
   as opaque bytes and no Unicode normalization is performed.
6. **Backends.** A writable, bounded tmpfs is the reference VFS backend. O0FS
   is exposed read-only in this wave. The O0FS format and journal are not
   promoted to approved or durable by mounting it. Writable O0FS access stays
   disabled until ADR 0012's complete crash-injection matrix passes and the
   format decision is approved.
7. **Cache.** The VFS uses an eight-page bounded write-through cache keyed by
   vnode identity and page index. Writes reach the backend synchronously; there
   are no dirty cache pages. LRU eviction drops the vnode reference, and unmount
   invalidates the cache. No cache entry may outlive its vnode identity. This
   cache does not imply durable O0FS writes.
8. **User memory.** Every user pointer is checked and copied only through
   `o0_syscall_arg_check`, `o0_copy_from_user` and `o0_copy_to_user`.
   Backend code receives kernel buffers only.
9. **Windows boundary.** Win32 file handles, sharing modes, access masks,
   disposition, namespace rules and I/O completion semantics belong to a
   separate compatibility layer. They are not aliases for native Ozero file
   capabilities.

## 3. Native syscall contract

The existing syscall numbers 1–16 are unchanged. New numbers are append-only:

| Number | Call | Contract |
|---:|---|---|
| 17 | `file_open` | directory capability, bounded relative path, requested rights and open policy; returns a file capability for files or a vnode capability for directories |
| 18 | `file_read` | file capability, kernel-validated output buffer, length, explicit offset; returns bytes read |
| 19 | `file_write` | file capability, kernel-copied input buffer, length, explicit offset; returns bytes written; backend must be writable |
| 20 | `file_stat` | directory capability + relative path + output metadata |
| 21 | `file_readdir` | directory capability + bounded output buffer + explicit opaque cursor |

The exact C layouts, maximum transfer sizes and error mapping are frozen in
`include/ozero/uapi.h` and covered by host ABI tests. `file_open` returns an
`O0_OBJ_FILE` for a regular file and an `O0_OBJ_VNODE` for a directory; callers
must use the returned object type and matching rights. `handle_close` (syscall 5) closes a file capability.

## 4. Object and lifetime rules

- A directory capability references a vnode and carries directory-operation
  authority. It is not a pathname string and cannot be forged.
- An opened file object holds a vnode reference. Unlinking a name does not
  invalidate already-open file objects; the backend must defer reclamation
  until the last open/reference is released. If a backend cannot provide that
  guarantee, unlink of an open vnode returns a defined busy status.
- Mount entries retain their mountpoint and root vnode. Unmount refuses while
  external references to the mounted root remain. Open file vnodes retain their
  backend's lifetime independently and may continue after namespace detachment;
  each backend must keep its state alive until its last vnode is released. The
  boot O0FS instance is pinned for the lifetime of the current kernel image.
- Readdir cursors are opaque, backend-independent tokens. A stale or malformed
  cursor fails closed.
- Handle duplication and transfer can only preserve or reduce rights.

## 5. Required tests

- VFS path resolution across mount points and mount boundaries.
- Relative lookup, root confinement, rejected `..`, empty/oversize path,
  opaque invalid-UTF-8 names, excessive depth and component length.
- tmpfs create/read/write/readdir/stat, zero-fill gaps, capacity
  exhaustion and cleanup after allocation failure.
- O0FS read-only stat/read/readdir; every write-like operation is refused.
- Open-file lifetime across unlink, mount reference lifetime and stale handles.
- Capability rights reduction, transfer, stale-generation rejection and
  process isolation.
- Every syscall: bad handle/type/rights, bad pointers, zero/oversize lengths,
  integer overflow and backend errors.
- Ring-3 process acceptance in QEMU: open a file relative to its granted
  directory capability, read it, write a tmpfs file, read back and close it.
- Host tests under ASan/UBSan and the existing QEMU/CI matrix.

## 6. Explicit non-goals

SMP-safe VFS mutation, symlinks, hard links, permissions/ACLs, file locking,
asynchronous I/O, memory-mapped files, durable O0FS writes, POSIX emulation
and Win32 API implementation are outside Wave 4.6. They must not be implied by
the initial interface.
