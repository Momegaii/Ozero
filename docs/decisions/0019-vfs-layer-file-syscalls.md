# ADR 0019: VFS layer and file syscalls — superseded

- **Status:** Superseded by ADR 0022.
- **Date:** 2026-11-27
- **Wave:** 4.6

This decision described path-global lookup, cursor-bearing open-file descriptions,
POSIX-like file-descriptor semantics and syscall numbers 17–22. Those choices
conflict with Ozero's approved capability model and are not the Wave 4.6 contract.

ADR 0022 replaces this design. Its binding requirements are explicit directory
capabilities, generation-protected vnode/file handles, explicit 64-bit I/O offsets,
append-only syscall numbers 17–21, bounded writable tmpfs, and read-only O0FS
pending the separate ADR 0012 recovery gate.

No code or ABI should be implemented from this superseded record.
