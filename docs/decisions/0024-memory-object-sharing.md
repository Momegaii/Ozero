# ADR 0024: Memory-object sharing — shared mappings across address spaces

- **Status:** Accepted (implementation record)
- **Date:** 2026-11-28
- **Wave:** 5.0
- **Supersedes:** none
- **Related:** ADR 0008 (memory objects), ADR 0023 (demand paging), `docs/allocator.md`

## 1. Context

Memory objects could previously only be mapped into the address space of the
process that created them: `o0_memory_map()` enforced
`o0_as_budget(as) == memory->obj.budget`. This prevented sharing memory between
processes — for example, passing a buffer to a child process or mapping the
same data into a server and a client.

Sharing lets a single memory object's pages be mapped into multiple address
spaces. The creating process's budget pays for the pages; other address spaces
borrow them without being charged.

## 2. Design

### 2.1 Budget check removed

`o0_memory_map()` no longer requires `o0_as_budget(as) == memory->obj.budget`.
Any address space may map any memory object (subject to rights, alignment,
range, W^X, and destination-unmapped checks). The creating process's budget
remains charged for all pages; borrowing address spaces are not charged.

### 2.2 Borrowed tracking

Each address space that maps the object records its own borrowed range via
`o0_as_add_borrowed(as, vaddr, len, owner, obj_page_offset)`. The address space
holds a reference on the memory object until teardown, at which point the
reference is released. A maximum of 8 borrowed ranges per address space is
supported at this stage.

### 2.3 Demand paging interaction

Lazily allocated (unbacked) pages can be mapped into multiple address spaces.
Each address space's fault handler independently backs the page on first access.
The physical frame is allocated by the first faulting address space and
recorded in `memory->pages[idx]`; subsequent faults in other address spaces see
the page as already backed and map it directly.

Because there is no copy-on-write, all address spaces share the same physical
frame. Writes by one address space are visible to all others.

### 2.4 Teardown

When an address space is torn down (`o0_as_destroy`), each borrowed range's
owner reference is released. The physical pages are freed only when the last
reference on the memory object is dropped (i.e., when the creating process's
handle is closed and all borrowers have torn down).

## 3. Alternatives

| Alternative | Trade-off |
|---|---|
| **Budget-equality restriction (previous)** | Simple accounting, but no sharing. Replaced because sharing is a core IPC building block. |
| **Remove budget check, creator pays (chosen)** | Simple, enables sharing. The creator's budget is the source of truth; borrowers are not charged. Risk: a process can map another's object and consume its memory indirectly, but this is intentional sharing. |
| **Per-borrower budget charge** | Each borrower pays for pages it maps. More complex accounting and requires tracking which borrower backed which lazy page. Deferred. |
| **Copy-on-write** | Writes trigger private copies. More complex, requires tracking COW state per page. Deferred — writes are visible to all sharers for now. |

## 4. Security invariants

1. A memory object can only be mapped via `o0_memory_map()`, which requires the
   `MAP` right on the handle and enforces W^X.
2. A transferred memory handle (via IPC) carries the object reference; the
   recipient can map it into their address space. The creator's budget still
   pays for the pages.
3. Address-space teardown releases borrowed references; the memory object's
   pages are freed only when the object itself is destroyed.
4. Borrowed ranges are bounded at 8 per address space; exhaustion returns
   `O0_ENOMEM`.

## 5. Open questions

- Copy-on-write for shared writable pages is not implemented. A process that
  wants a private copy must allocate its own memory object and copy data.
- Budget recovery when a borrower outlives the creator: the creator's budget is
  charged for all pages regardless of which address spaces map them. If the
  creator exits, the pages are freed and all borrowers' mappings become
  dangling (their next fault allocates a fresh zeroed page). This is acceptable
  for this stage; stronger lifetime binding is deferred.
