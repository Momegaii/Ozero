# ADR 0023: Demand paging — lazy physical frame allocation on first access

- **Status:** Accepted (implementation record)
- **Date:** 2026-11-28
- **Wave:** 5.0
- **Supersedes:** none
- **Related:** ADR 0008 (memory objects), `docs/allocator.md`, `docs/boot-contract.md`

## 1. Context

Memory objects (`O0_OBJ_MEMORY`) allocate every physical frame eagerly at
`o0_memory_create()` time. A process that creates a 1 MiB object pays for all
256 frames immediately, even if it only ever touches the first page. This wastes
physical memory and delays process startup under memory pressure.

Demand paging defers physical frame allocation until a page is first accessed.
A page fault on an unbacked, borrowed mapping triggers allocation of a zeroed
frame, which is then mapped into the faulting address space and recorded in the
memory object.

## 2. Design

### 2.1 Memory object layout

`struct o0_memory.pages[i]` is 0 when page `i` is unbacked. Physical page 0 is
never a valid PMM allocation (it is the null sentinel), so 0 unambiguously
means "not yet backed". `memory_destroy` frees only backed pages and uncharges
the budget for every page (backed or not), since all pages were charged at
create time.

### 2.2 Creation

`o0_memory_create()` charges the budget for every page but allocates no
physical frames. `o0_memory_create_from()` backs every page eagerly because
caller data must be copied into the frames immediately.

### 2.3 Mapping

`o0_memory_map()` maps backed pages as present with `O0_MAP_BORROWED`.
Unbacked pages are mapped as not-present with `O0_MAP_BORROWED | O0_MAP_LAZY`.
The `O0_MAP_LAZY` software flag (PTE bit 9 is `O0_ENTRY_SW_BORROWED`, bit 10
is the lazy marker) causes `flag_bits()` to omit `O0_ENTRY_PRESENT`, so any
access faults.

### 2.4 Page fault handler

`x86_exception_dispatch` (kernel/src/main.c) routes ring-3 `#PF` to
`o0_thread_user_fault()` (kernel/src/thread.c). The handler:

1. Rejects protection violations (error code P=1) and non-#PF vectors: the
   thread is killed as before.
2. Calls `o0_as_fault_in()` (kernel/arch/x86_64/paging.c), which walks the
   address space's borrowed ranges to find the memory object and page index
   owning `CR2`.
3. If the page is unbacked, allocates a zeroed frame from the PMM, maps it
   present with the object's rights-derived protection, records the backing
   page in `memory->pages[idx]`, and returns `O0_OK`. The exception dispatch
   returns and `iretq` re-executes the faulting instruction.
4. If `CR2` is not in a lazy borrowed mapping, the thread is killed.

The budget was already charged at create time, so no additional charge occurs
on fault.

### 2.5 Kernel reads

`o0_memory_read()` demand-allocates an unbacked page on first read, so kernel
callers never encounter a hole.

## 3. Alternatives

| Alternative | Trade-off |
|---|---|
| **Eager allocation (previous)** | Simple, but wastes memory for sparsely used objects. Chosen as baseline before this ADR. |
| **Demand paging, not-present PTE (chosen)** | Allocates on first read or write. Simple fault handler. No zero-page needed. |
| **Global zero page + copy-on-write** | Reads never fault (zero page is present); writes trigger COW. Saves memory for read-only shared pages but is more complex and does not allocate on read. Deferred. |
| **Page daemon / prepaging** | Requires a background thread and access-prediction heuristics. Out of scope for this stage. |

## 4. Security invariants

1. An unbacked page is never exposed as present: any access faults and is
   backed by a freshly zeroed frame, so no stale data leaks.
2. The faulting thread's address space must own a borrowed reference to the
   memory object covering `CR2`.
3. W^X is preserved: the fault-in mapping derives protection from the memory
   object's rights, which already forbid WRITE+EXEC.
4. A process cannot fault in pages for a memory object it did not create —
   borrowing is established at `o0_memory_map()` time, not on fault.

## 5. Open questions

- Copy-on-write for shared pages is not implemented. Writes to a shared page
  are visible to all address spaces.
- Physical memory above 4 GiB has no HHDM virtual address; `o0_phys_to_virt()`
  returns NULL and fault-in fails with `O0_ERANGE`. Such frames cannot back
  user memory objects until the direct map is extended.
- The fault handler runs with interrupts enabled (exception context). On a
  single CPU this is safe; SMP concurrency on the same page is not yet a
  concern.
