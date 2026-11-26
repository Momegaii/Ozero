# ADR 0008: Capability handles, budgets and synchronous IPC

- **Status:** Accepted (implementation record in §14)
- **Date:** 2026-11-25
- **Wave:** 5.0 (originally numbered Wave 3 before the 2026-11-25 reorder)
- **Supersedes:** none
- **Related:** ADR 0007 (user ABI), `docs/hardening-roadmap.md`

## 1. Context

Wave 2 gave a user process a private address space and four syscalls. It has no
way to name a kernel object, no accounting for what it allocates, and no way to
talk to another process. Wave 3 adds exactly those three things for a single
CPU, without importing a POSIX surface, ambient authority or asynchronous
queues.

Scope and fixed decisions:

- Single CPU, emulator-verified only. Physical hardware is not exercised.
- IPC is synchronous rendezvous only.
- Scheduling is cooperative: a thread runs until it yields or blocks. The timer
  wakes sleepers but does not preempt.
- Handles are per-process indices with a generation counter. A handle is never
  a raw kernel pointer.
- Every kernel object and every page charged to a process is charged to a
  `Budget`.
- No ambient authority: a process can only act on an object through a handle it
  holds, with the rights recorded in that handle.
- No POSIX surface: no file descriptors, `errno`, `pipe`, `fork`, `waitpid`,
  signals or `mmap`-style flags.
- Non-goals: SMP, preemption, SMEP/SMAP, asynchronous IPC rings, user-space
  drivers, filesystems. The five Wave 2 entry criteria stay open.

## 2. Object model

Every kernel object starts with one header:

```c
struct o0_object {
    uint32_t type;        /* Process, Memory, Endpoint, Budget */
    uint32_t magic;
    int32_t  refcount;
    uint32_t reserved;
    struct o0_budget *budget;
    size_t   charge_bytes;
    size_t   alloc_size;
    void   (*destroy)(struct o0_object *);
};
```

Types are `O0_OBJ_PROCESS`, `O0_OBJ_MEMORY`, `O0_OBJ_ENDPOINT` and
`O0_OBJ_BUDGET`. A handle may only refer to a live object of the expected type.

Rules:

1. A reference is taken when an object is installed in a handle table, when a
   message carries it, and when a mapping borrows its pages.
2. The last reference runs `destroy`, which releases child references and
   uncharges the object's bytes.
3. Objects are allocated through the kernel heap and charged before they become
   visible to any handle table.
4. A process control block is a static object with an embedded header; it is
   charged like an allocated object but is not freed by the allocator.

The process object exists for type discipline and future `process_wait`; Wave 3
does not expose a process handle to user space.

## 3. Handles

```c
struct o0_handle_entry {
    struct o0_object *object;
    uint32_t rights;
    uint32_t generation;
    bool     active;
};
```

A handle value is `(generation << 32) | index`. Generation starts at 1, is
incremented on close, and is never reused for the same slot while a stale value
could still exist in user space. `index == 0 && generation == 0` is never a
valid handle.

`o0_handle_lookup(proc, h, type, rights)` is the only resolver. It fails closed
with `O0_ENOENT` for a bad index, an inactive entry, a stale generation or a
type mismatch, and with `O0_EPERM` when a requested right is missing. Every
syscall that takes a handle uses it before touching the object.

Rights:

| Right | Meaning |
|---|---|
| `READ` | memory contents may be read |
| `WRITE` | memory contents may be written |
| `EXEC` | memory may be mapped executable |
| `MAP` | memory may be mapped into an address space |
| `TRANSFER` | handle may be placed in a message or a spawn grant |
| `DUP` | handle may be duplicated |
| `SEND` | endpoint accepts `ipc_send` |
| `RECV` | endpoint accepts `ipc_recv` |
| `WAIT` | endpoint may be used as a call reply target |
| `INFO` | metadata may be queried |
| `SPLIT` | budget may be subdivided |

`handle_dup` may only reduce rights: the requested mask must be a subset of the
entry's mask, otherwise `O0_EPERM`. A duplicate gets a new index and generation;
the object reference count is incremented.

`handle_close` clears the entry, increments the generation and releases the
object. If the object is an endpoint, the close is reported to the endpoint so
blocked peers can be failed rather than hung.

The handle table is a fixed 64-entry array allocated per process and charged to
the process budget. Exhaustion returns `O0_ENOMEM`; it never grows silently.

### Bootstrap handle layout

A new process always has its own budget handle at index 0 with `INFO | SPLIT |
DUP | TRANSFER`. Explicit spawn grants are installed at indices 1..N in the
order supplied. Because handle values include a generation, the kernel computes
the installed value and passes it in `RDI` (first grant), `RSI` (second) and
`RDX` (third) at ring-3 entry. A program with no grants receives its budget
handle in `RDI`.

## 4. Budgets

```c
struct o0_budget {
    struct o0_object obj;
    uint64_t limit;
    uint64_t used;
    struct o0_budget *parent;
};
```

- The kernel creates one root budget at boot. Its limit is a policy constant
  (`O0_BUDGET_ROOT_BYTES`, 64 MiB).
- `budget_split(limit)` charges `limit` to the parent immediately and returns a
  handle to the child. A split fails with `O0_ENOMEM` when the parent's
  remaining room is smaller than the request. Reserving the whole child limit in
  the parent is what stops a process from escaping its own ceiling by spawning.
- Charges are page-granular for pages and exact for heap objects: object
  headers, handle tables, page tables, kernel stacks, user image pages, user
  stack pages and memory-object pages.
- `o0_budget_charge` is all-or-nothing. On failure the caller unwinds every
  allocation it made for that operation; no partial object is published.
- A child budget holds a reference on its parent. When the child is destroyed it
  returns its unused limit to the parent and releases that reference.
- `budget_info` reports `limit`, `used` and `remaining`. It requires `INFO`.
- A process's budget handle cannot be closed in a way that leaves the process
  without accounting: the process holds its own reference independently of the
  handle table.

Budgets are accounting, not a second physical allocator. The PMM remains the
source of pages; a budget can only make an allocation fail earlier.

## 5. Memory objects

A memory object owns a set of zeroed 4 KiB pages and a page array. It is the
only user-visible way to obtain memory in Wave 3.

- `memory_create(size)` allocates and charges the pages to the caller budget and
  returns a handle with `READ | WRITE | MAP | TRANSFER | DUP | INFO`.
- `memory_map(mem, vaddr, len, prot, offset)` requires `MAP`, a page-aligned
  range inside the object, an unmapped destination in the user window, and
  `prot` as a subset of the handle's `READ`/`WRITE`/`EXEC` rights.
- W^X is enforced at map time: `WRITE` and `EXEC` together are refused. A
  memory object created with `WRITE` cannot be mapped `EXEC`; an executable
  image must be created or loaded without `WRITE`.
- Mapped pages are marked with a software PTE bit (`O0_MAP_BORROWED`, PTE bit
  9) so address-space teardown frees page tables but not pages owned by the
  memory object. The address space holds an object reference for each borrowed
  mapping; releasing it at teardown lets the memory object free its pages.
- A process may only map a memory object charged to its own budget. Mapping a
  foreign budget's pages would let the receiver consume pages it never paid for.
  A transferred memory handle is still useful as an image for `process_spawn`.

The Wave 2 ELF rejection rules are unchanged. Spawn reads the image through the
memory object into a charged kernel buffer and runs the same parser.

## 6. Endpoints and synchronous IPC

An endpoint has a bounded message queue and a bounded receiver wait list.

- Maximum message bytes: 64 (`O0_IPC_MAX_MESSAGE`).
- Maximum transferred handles per message: 4 (`O0_IPC_MAX_HANDLES`).
- Queue depth: 8 messages (`O0_IPC_QUEUE_DEPTH`).
- Receiver wait list: 8 threads.

`ipc_send(ep, buf, len, xfers, n)`:

1. Requires `SEND`. Validates the buffer with `o0_syscall_arg_check` and copies
   it with `o0_copy_from_user`; `len > 64` is `O0_ERANGE`.
2. Validates every transfer entry before changing either handle table: the
   sender must hold the handle, hold `TRANSFER`, and request only a subset of
   the entry's rights. Duplicate entries are rejected.
3. Reserves a queue slot, then atomically removes the transfer handles from the
   sender table and stores their object references in the message.
4. Blocks the sender until a receiver consumes the message, or fails with
   `O0_EPEER` if the endpoint is closed or a peer died.

`ipc_recv(ep, buf, maxlen, out_handles, max_handles, out_count)`:

1. Requires `RECV`. Validates the output buffer and count pointer for write.
2. If a message is queued, checks that the receiver table has room for all its
   handles before consuming it. On `O0_ENOMEM` the message stays queued.
3. Copies bytes with `o0_copy_to_user`, installs the transferred handles, wakes
   the blocked sender and returns the byte count.
4. If no message is queued, registers as a waiting receiver and blocks.

`ipc_call(ep, sbuf, slen, rbuf, rlen)` creates a private reply endpoint, sends
the request with a `SEND`-only handle to that endpoint as the single transfer,
then receives the reply. The reply endpoint rejects further handle transfers,
so a call cannot smuggle capabilities back without an explicit `ipc_recv`. If
the callee dies before replying, the caller's receive fails with `O0_EPEER`.

Transfer is atomic because the sender removes handles only after every check
and queue reservation succeeds, under one interrupt-disabled section on a single
CPU. A failure before that point leaves both tables unchanged.

Peer death is explicit: closing the last handle, closing a handle while another
holder exists, or tearing down a process reports to each endpoint it held.
Blocked senders and receivers are woken with `O0_EPEER`; queued messages are
released with their object references. A blocked peer never waits forever for a
dead process.

## 7. Cooperative scheduling

- The ready queue, block and wake are the only scheduling operations Wave 3
  adds to. `o0_sched_start` no longer enables timer preemption.
- A thread blocks with a reason (`IPC`, `JOIN`, `SLEEP`, `MUTEX`) and is made
  ready only by an explicit wake. The PIT still advances time and wakes sleepers;
  it does not switch a running thread.
- The idle thread halts until an interrupt. If every thread is blocked, that is
  a kernel defect for IPC: peer death and close paths must wake waiters first.
- `o0_thread_wake_result(thread, status)` records the reason a waiter woke, so
  a sender can distinguish success from peer death without inspecting endpoint
  state that may have been reused.

## 8. Syscall surface v2

Numbers 1-4 keep their Wave 2 meaning. New numbers:

| # | Call | Arguments (RDI, RSI, RDX, R10, R8, R9) | Returns |
|---|---|---|---|
| 5 | `handle_close` | handle | 0 |
| 6 | `handle_dup` | handle, rights | new handle |
| 7 | `handle_info` | handle, out_info* | 0 |
| 8 | `budget_split` | limit | new handle |
| 9 | `budget_info` | handle, out_info* | 0 |
| 10 | `endpoint_create` | - | handle |
| 11 | `ipc_send` | ep, buf, len, xfers*, n | 0 |
| 12 | `ipc_recv` | ep, buf, maxlen, out_handles*, max, out_count* | bytes |
| 13 | `ipc_call` | ep, sbuf, slen, rbuf, rlen | bytes |
| 14 | `memory_create` | size | handle |
| 15 | `memory_map` | mem, vaddr, len, prot, offset | 0 |
| 16 | `process_spawn` | mem, budget, xfers*, n | pid |

`O0_SYS_MAX` becomes 16. `handle_info` returns
`{handle, type, rights, charge_bytes}`. `budget_info` returns
`{limit, used, remaining}`. Transfer entries are
`{handle, rights, reserved}` (16 bytes).

Every pointer argument goes through `o0_syscall_arg_check`; every byte moves
through `o0_copy_from_user` / `o0_copy_to_user`. Handle arguments are resolved
through `o0_handle_lookup` with an explicit type and rights mask. No syscall
dereferences a user pointer directly.

The Wave 2 return-slot fix is covered by a dedicated user regression program
that checks `getpid() > 0` and the `debug_write` byte count in `RAX`.

## 9. Errors

One native code is added: `O0_EPEER` (19) / `O0_UE_PEER` (-19), meaning the
peer endpoint or process is gone. Everything else keeps its Wave 2 value.
`-O0_UE_NOMEM` covers both budget exhaustion and physical exhaustion; the two
are distinguished by `budget_info`, not by a second error code.

## 10. Hardware-dependent choices and Intel SDM references

These are the choices that depend on architectural behaviour rather than on
Ozero policy. They are unchanged from Wave 2 unless noted; Wave 3 only relies on
them more heavily. References are to the *Intel 64 and IA-32 Architectures
Software Developer's Manual*; section numbers are given so real-silicon tests
can check the exact clause.

1. **`SYSCALL` entry.** `SYSCALL` saves the return RIP in `RCX` and RFLAGS in
   `R11`, loads RIP from `IA32_LSTAR`, CS/SS from `IA32_STAR`, and masks RFLAGS
   with `IA32_FMASK`; it does not change RSP. Vol. 2A, *SYSCALL - Fast System
   Call*. Ozero therefore sets `FMASK = TF|IF|DF` and switches to the kernel
   stack in the stub. On real silicon the kernel must confirm that the
   non-canonical `IA32_LSTAR` case faults rather than wrapping, and that `FMASK`
   is applied before the first kernel instruction.
2. **`SYSRET` return.** `SYSRET` loads RIP from `RCX` and RFLAGS from `R11`, and
   a non-canonical return RIP raises `#GP(0)`. Vol. 2A, *SYSRET - Return From
   Fast System Call*. Ozero uses `IRETQ` for the actual return path, so this
   clause is a check on the selector values (`STAR`) rather than on a `SYSRET`
   instruction; the kernel never executes `SYSRET`.
3. **`STAR` layout.** `SYSCALL` takes CS = `STAR[47:32]` and SS = CS+8;
   `SYSRET` takes CS = `STAR[63:48]+16` and SS = `STAR[63:48]+8`, all with RPL 3
   for the user half. Vol. 2A, *SYSCALL* and *SYSRET* operation sections; Vol.
   3A, §5.8 describes the descriptor-table privilege rules those loads must
   satisfy. Ozero freezes `STAR = (0x10 << 48) | (0x08 << 32)` and asserts the
   resulting selectors in `abi_test`.
4. **TSS `RSP0` and `IST1`.** On a ring-3 to ring-0 interrupt or exception the
   CPU loads `RSP` from `TSS.RSP0`; when an IDT gate selects an IST index it
   loads `RSP` from `TSS.ISTn` instead. Vol. 3A, §7.2 and §7.2.1 (TSS structure),
   §6.14.5 (Interrupt Stack Table). Ozero keeps `RSP0` equal to the current
   thread's kernel stack top and puts the double-fault handler on `IST1` so a
   stack overflow still has a stack. Real silicon must confirm the exact
   `RSP0` alignment expectations and that a fault during the `RSP0` load raises
   a double fault rather than corrupting state.
5. **Page-fault error code.** The `#PF` error code reports P (0), W/R (1),
   U/S (2), RSVD (3) and I/D (4); the kernel uses bit 2 to separate user from
   kernel faults. Vol. 3A, §4.7 (*Page-Fault Exception*). A real CPU may set
   RSVD or I/D in cases QEMU does not; the handler logs the raw error code, so
   the distinction is visible rather than assumed.
6. **User/supervisor and W^X bits.** `PTE.U/S` controls ring-3 access and
   `PTE.R/W` plus `EFER.NXE`/`PTE.NX` control writability and executability.
   Vol. 3A, Chapter 4 (IA-32e paging and the execute-disable bit), §4.6
   (*Access Rights*), §2.5 (*Control Registers*, CR0.WP and CR4). Ozero enables
   `EFER.NXE` and `CR0.WP`, sets `U/S` only on user mappings and refuses
   `W+X` at every public mapping entry point. SMEP/SMAP are not enabled; the
   `STAC`/`CLAC` hooks remain unexercised.
7. **Handle transfer is not a hardware behaviour.** It is Ozero policy and is
   listed here only to keep the hardware claims separate from the object model.

Physical hardware is unverified for all of the above. The QEMU matrix,
including a TCG run, is emulator evidence only.

## 11. Alternatives considered

- **Raw kernel pointers as handles.** Rejected: forgeable, leaks kernel
  addresses, and makes stale-use a memory-safety bug.
- **Global object namespace / ambient authority.** Rejected: a process could
  name objects it was never granted.
- **Handle value = index only, generation stored in the table.** Rejected: a
  stale handle would silently resolve to a new object in the same slot.
- **POSIX file descriptors and pipes.** Rejected: imports an ABI and semantics
  Ozero has not chosen, and encourages ambient authority.
- **Asynchronous IPC rings.** Deferred: requires shared-memory protocols,
  notification and backpressure design. Synchronous rendezvous is the smallest
  complete primitive and is enough for spawn and request/reply.
- **Preemptive scheduling for IPC.** Rejected for Wave 3: block/wake is
  sufficient, removes timer dependence from correctness, and keeps the
  scheduling surface small.
- **Charge only physical pages, not objects.** Rejected: a process could create
  unbounded empty objects and handle tables without touching a page.
- **Transfer handles at receive time instead of send time.** Rejected: the
  sender could close or duplicate the handle while blocked, so the receiver's
  view would not be atomic. Taking at send time makes the message own the
  references.

## 12. Security invariants

1. No handle value is ever dereferenced as an address.
2. A handle resolves only in the process that holds it, only for its recorded
   type, and only with the requested rights.
3. Rights never increase through `handle_dup`, transfer or spawn.
4. A failed transfer leaves both handle tables unchanged.
5. No allocation is visible before its budget charge succeeds.
6. A process cannot map pages charged to another budget.
7. A blocked sender or receiver is woken with an error when its peer dies.
8. Teardown releases every handle, page, table, stack and budget charge.

## 13. Open questions and deferred work

- SMP: `swapgs`, the per-CPU block and handle tables are single-CPU only. The
  per-CPU reservation from ADR 0007 is not exercised.
- SMEP/SMAP: `STAC`/`CLAC` are structured but not emitted.
- Physical silicon: `SYSCALL`/`SYSRET`, `RSP0`/IST and `#PF` behaviour must be
  checked against the SDM clauses above on real hardware.
- Async IPC, `mmap`-style sharing, `fork`, signals, `process_wait` and
  user-space drivers remain out of scope.

## 14. Implementation record

Wave 3 closed on 2026-11-24 for a single CPU and emulator scope.

Implemented as specified:

- `struct o0_object` with type, magic, atomic refcount, budget, charge and
  destroy hook; static process objects use the same header.
- Per-process handle tables with generation-tagged values, per-handle rights,
  lookup/install/install-owned/take/dup/close and teardown.
- Hierarchical budgets with split, charge, uncharge and snapshots; objects and
  every user page are charged before they become visible.
- Memory objects with budget-charged pages, read access, mapping policy and
  W^X validation; `memory_create` and `memory_map` syscalls.
- Endpoints with a bounded message queue, synchronous send/receive/call,
  handle transfer at send time and peer-death wakeups.
- `process_spawn` with grant installation and detached child reaping.
- Syscalls: `handle_close`, `handle_dup`, `handle_info`, `budget_split`,
  `budget_info`, `endpoint_create`, `ipc_send`, `ipc_recv`, `ipc_call`,
  `memory_create`, `memory_map` and `process_spawn`.

Verification and evidence:

- Host: `tests/cap_test.c` (object, budget, handle, memory and endpoint
  cores) passes under `make test` and `make test-sanitize`.
- Emulator: `{bios, uefi} × {qemu64, Haswell-v1}` on QEMU 10.0.13; every cell
  prints the capability/IPC markers and `QEMU smoke test: PASS`.
- CI: run [#138](https://github.com/Momegaii/Ozero/actions/runs/45587151198) on
  commit `521bc04` passed all five jobs on QEMU 10.2.1; the downloaded job logs
  contain the same markers.
- Negative coverage: forged and stale handles, rights escalation, foreign
  budget mapping, bad IPC pointers, dead peers, budget exhaustion and eight
  process-churn pairs with object, budget and PMM accounting returned to
  baseline.
- Physical hardware: not exercised. This record is not physical evidence.

Defects fixed during the implementation record:

1. A memory object that failed partway through page allocation uncharged the
   pages it had allocated but only `allocated_pages * sizeof(paddr)` of the
   page array. The array is charged for the full requested capacity; the
   object now records that capacity before allocation, so a failed create
   unwinds exactly. Covered by the tight-budget case in `cap_test`.
2. `o0_ipc_call` took an extra reference on its reply endpoint before
   installing it, and released only one of the two extra references. One
   endpoint leaked per call. The redundant reference was removed; the churn
   check now requires the endpoint live count to return to baseline.

Open items are unchanged from §13: SMP, SMEP/SMAP, preemption, asynchronous
IPC, physical silicon and the remaining deferred surfaces.


## 15. Wave 4.6 file capabilities

ADR 0017 extends this object model with `O0_OBJ_VNODE` and `O0_OBJ_FILE`.
These remain ordinary opaque objects in the same generation-protected per-process
handle table. Directory handles carry explicit enumeration/create authority; file
handles carry READ/WRITE/INFO and may be duplicated or transferred only when those
rights are present. Rights are reduced, never escalated.

This does **not** reverse §1's rejection of POSIX file descriptors. A file capability
is an object handle, not a descriptor number: there is no implicit current offset,
process-wide descriptor namespace, or POSIX close operation. Existing
`handle_close` closes it. Path authority is rooted in an explicitly granted directory
capability, never an implicit process-global root. See ADR 0017 for the VFS, backend,
path, cache, syscall and verification contracts.
