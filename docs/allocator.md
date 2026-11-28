# Ozero Allocator Architecture

Status: **Implementation record**  
Scope: physical page allocator, kernel slab, memory objects, budgets, objects,
address spaces and DMA — how they fit together and who owns what.  
Related: `docs/boot-contract.md`, `docs/decisions/0008-*.md`,
`docs/decisions/0010-memory-hardening-smep-smap.md`,
`docs/decisions/0011-wave3-ioapic-percpu-kaslr-pmm.md`,
`docs/hardening-roadmap.md`.

This document is an implementation record: it describes what exists in the
source tree. It is not a design proposal for features that do not yet exist.
Demand paging, memory-object sharing across address spaces beyond borrowed
mappings, and a free-list buddy allocator are not part of this record.

---

## 1. Allocation stack

Ozero has one physical source of pages and several consumers layered on top:

```
Consumer              Backed by             Charged to
--------              -------               -----------
DMA buffers           PMM (contiguous run)  caller budget
Kernel stacks         PMM                   caller budget
Page tables           PMM                   address-space budget
Slab chunks           PMM (one page/class)  slab (internal)
Memory object pages   PMM                   object budget
Kernel heap objects   Slab                  caller budget (via slab)
```

Budgets are accounting, not a second physical allocator. The PMM remains the
source of pages; a budget can only make an allocation fail earlier.

---

## 2. PMM — physical page-frame allocator

File: `kernel/src/pmm.c`  
Header: `include/ozero/mm.h` (`o0_pmm_*`)

### 2.1 Design

A bitmap page-frame allocator built from the normalised boot memory map. One
bit per usable page: 0 = free, 1 = allocated. The bitmap lives in kernel-owned
BSS at the kernel's high-half virtual address, so the allocator never
dereferences a raw physical address.

Page 0 is never returned as a valid allocation; a null `o0_paddr_t` (0) always
means "no allocation".

### 2.2 Initialisation

`o0_pmm_init(bi)` scans the boot memory map (`o0_boot_info.memmap`) and records
regions whose normalised type is `O0_MEM_FREE` or `O0_MEM_RECLAIMABLE` as
usable. Regions must be page-aligned and non-zero length; a maximum of 64
regions is supported.

Reclaimable regions are tracked but **reserved** at initialisation: their bits
are set and they are excluded from the free count. They become allocatable only
after `o0_pmm_release_bootloader_memory()` runs and the boot-contract reclaim
checklist passes.

The kernel image is reserved: the physical load address range from
`bi.kernel_phys`/`bi.kernel_phys_size` is marked allocated, as is physical page
0. If the load address is not yet resolved, pages from the first usable region
are reserved defensively.

### 2.3 Allocation

`o0_pmm_alloc_pages(order)` allocates `2^order` contiguous pages, naturally
aligned to `2^order * O0_PAGE_SIZE`. The scan is interrupt-masked so allocation
from interrupt context (for example slab growth on a woken thread) is atomic.
If fewer than `2^order` pages are free, or no aligned run of that size exists,
the function returns 0.

Allocation is first-fit over regions, not a free-list buddy allocator. The
alignment requirement for large orders can cause failure even when enough total
free pages exist; this is a known limitation of this stage.

### 2.4 Free

`o0_pmm_free_pages(base, order)` clears the bits for the block and verifies
that every page in the block was allocated (aborting without corruption if any
bit is already clear). It then performs buddy-coalescing validation: walking
upward while the paired buddy block is entirely free, checking that no half of
a buddy pair is stranded allocated. The bitmap itself already represents the
merged run; this loop validates the invariant rather than maintaining split
metadata.

### 2.5 Queries

- `o0_pmm_free_bytes()` — free tracked pages × page size
- `o0_pmm_total_bytes()` — total tracked usable pages × page size

### 2.6 Reclaim

`o0_pmm_release_bootloader_memory()` clears every reserved reclaimable region
once, adds its pages to the free count and returns the released byte count. It
runs after the boot information, ACPI data and SMP response have been copied
into kernel-owned memory. ACPI-reclaimable memory is not released. Calling it
twice is a no-op for already-released regions.

### 2.7 Limits

- Maximum 64 memory regions
- Bitmap covers up to 8 GiB of RAM (256 KiB bitmap, one bit per 4 KiB page)
- Orders above 63 are rejected
- Physical addresses at or above 4 GiB have no direct-map virtual address
  (`o0_phys_to_virt()` returns NULL); the allocator can still hand out such
  frames, so every HHDM conversion must be checked

---

## 3. Slab — kernel heap allocator

File: `kernel/src/slab.c`  
Header: `include/ozero/mm.h` (`o0_kalloc`/`o0_kfree`)

### 3.1 Design

A fixed size-class slab allocator backing `o0_kalloc`/`o0_kfree`. Each class
has one 4 KiB page per slab and a per-class free list. Each allocation carries
a 16-byte header holding its size class, so `o0_kfree` does not have to trust
the caller's size argument.

Size classes:

| Class | Chunk size | Payload |
|-------|-----------|---------|
| 0 | 32 | 16 |
| 1 | 64 | 48 |
| 2 | 128 | 112 |
| 3 | 256 | 240 |
| 4 | 512 | 496 |
| 5 | 1024 | 1008 |
| 6 | 2048 | 2032 |
| 7 | 4096 | 4080 |

The smallest class whose payload fits the requested size and whose alignment
satisfies the alignment limit (≤ 16 bytes) is selected. Alignments above 16
bytes are rejected. Size 0 is normalised to 1.

### 3.2 Growth

When a class's free list is empty, `slab_grow()` allocates one page from the
PMM and subdivides it into chunks of the class's object size, linking them onto
the free list. Freed pages are never returned to the PMM at this stage.

### 3.3 Limits

- Alignments above 16 bytes are rejected
- There is no double-free detection beyond the magic check; freeing an already-
  freed pointer clears its magic and links it back onto the free list (a second
  free of the same pointer will find magic == 0 and return silently)
- `o0_kalloc` returns NULL when the slab is not initialised or no class fits

---

## 4. Memory objects

File: `kernel/src/memory.c`  
Header: `include/ozero/memory.h` (`o0_memory_*`)  
Object type: `O0_OBJ_MEMORY`

### 4.1 Design

A memory object owns a set of zeroed 4 KiB pages and a page array. It is the
only user-visible way to obtain memory in the current stage. Pages are allocated
eagerly at creation time: `o0_memory_create()` allocates every page upfront,
charges each page to the caller's budget, and zeros each page.

`o0_memory_create_from()` additionally copies caller data into the pages.

### 4.2 Rights and mapping

Memory objects carry rights (`READ`, `WRITE`, `EXEC`, `MAP`, ...). `memory_map`
requires `MAP` on the handle, a page-aligned range inside the object, an unmapped
destination in the user window, and `prot` as a subset of the handle's rights.

W^X is enforced at map time: `WRITE` and `EXEC` together are refused. A memory
object created with `WRITE` cannot be mapped `EXEC`; an executable image must be
created or loaded without `WRITE`.

### 4.3 Borrowed pages

Mapped pages are marked with `O0_MAP_BORROWED` (software PTE bit 9) so
address-space teardown frees page tables but not pages owned by the memory
object. The address space holds an object reference (`o0_as_add_borrowed`) for
each borrowed mapping; releasing it at teardown lets the memory object free its
pages.

A process may only map a memory object charged to its own budget
(`o0_as_budget(as) == memory->obj.budget` is enforced). Mapping a foreign
budget's pages would let the receiver consume pages it never paid for.

### 4.4 Teardown

`memory_destroy` frees every allocated page back to the PMM, uncharges the
object's budget for each page and for the page array, then frees the page array
itself.

---

## 5. Budgets — hierarchical memory accounting

File: `kernel/src/budget.c`  
Header: `include/ozero/budget.h`  
Object type: `O0_OBJ_BUDGET`

### 5.1 Design

One root budget exists at boot (`O0_BUDGET_ROOT_BYTES` = 64 MiB). Child budgets
are created with `o0_budget_split(parent, limit)`, which charges `limit` to the
parent immediately and returns a child handle. A split fails with `O0_ENOMEM`
when the parent's remaining room is smaller than the request.

`o0_budget_charge(budget, bytes)` is all-or-nothing: on failure the caller
unwinds every allocation it made for that operation; no partial object is
published. `o0_budget_uncharge` releases bytes back.

A child budget holds a reference on its parent. When the child is destroyed it
returns its unused limit to the parent and releases that reference.

`o0_budget_snapshot` reports `limit`, `used` and `remaining`; it requires the
`INFO` right on the budget handle.

Budgets are accounting, not a second physical allocator. The PMM remains the
source of pages; a budget can only make an allocation fail earlier.

---

## 6. Objects — refcounted kernel object header

File: `kernel/src/object.c`  
Header: `include/ozero/object.h`

### 6.1 Design

Every kernel object starts with `struct o0_object`: type, magic, atomic
refcount, budget, charge bytes, allocation size and a destroy callback.
Types include `O0_OBJ_PROCESS`, `O0_OBJ_MEMORY`, `O0_OBJ_ENDPOINT`,
`O0_OBJ_BUDGET`, `O0_OBJ_VNODE` and `O0_OBJ_FILE`.

Objects are allocated through the kernel heap and charged before they become
visible to any handle table. The last reference runs `destroy`, which releases
child references and uncharges the object's bytes.

Static objects (e.g. process control blocks) use `o0_object_init_static`:
charged like an allocated object but not freed by the allocator.

---

## 7. Address spaces

File: `kernel/arch/x86_64/paging.c`  
Header: `include/ozero/mm.h` (`o0_as_*`)

### 7.1 Design

4-level x86-64 page tables (PML4 → PDPT → PD → PT) with 4 KiB leaf pages. The
kernel runs on Limine's page tables until `o0_paging_load()`, then switches to
its own PML4.

The kernel address space owns the shared kernel half: the HHDM direct map
(covering physical `[0, 4 GiB)` at `hhdm_offset`, using 2 MiB pages except for
framebuffer chunks which use 4 KiB UC pages) and the kernel image at its linked
high-half virtual address (text RX, data RW+NX, W^X). User address spaces copy
the kernel half of the kernel PML4 at creation and refresh it on every
activation, so mappings created after the address space (for example a new
kernel stack) are visible.

Each user address space owns its user half (PML4 entries below the user/kernel
split) and its page-table pages, allocated from the PMM and charged to the
address space's budget.

### 7.2 Mapping

`o0_as_map(as, vaddr, paddr, len, flags)` maps one or more 4 KiB pages. Flags
include `O0_MAP_READ`, `O0_MAP_WRITE`, `O0_MAP_EXEC`, `O0_MAP_USER`,
`O0_MAP_GLOBAL`, `O0_MAP_UC`, `O0_MAP_WC`, `O0_MAP_NX` and the internal
`O0_MAP_BORROWED`. W+X is refused at every public mapping entry point. NX is
set on every non-executable mapping when `EFER.NXE` is enabled.

`o0_as_unmap` removes a mapping and frees the leaf page (unless borrowed).
`o0_as_protect` changes flags on an existing mapping. `o0_as_query` resolves
one 4 KiB mapping, returning effective flags and physical address.

### 7.3 Borrowed tracking

`o0_as_add_borrowed(as, vaddr, len, owner)` records that `[vaddr, vaddr+len)`
is backed by `owner`'s pages. The address space holds a reference on `owner`
until teardown. A maximum of 8 borrowed ranges per address space is supported
at this stage.

### 7.4 Kernel stacks

`o0_kstack_alloc(pages, budget, &base)` allocates physically contiguous pages
from the PMM and maps them into the kernel address space in the randomized
kernel-stack window (see ADR 0011 §4.4). One guard page below each stack is
left unmapped. `o0_kstack_free` unmaps and frees the pages.

---

## 8. DMA

File: `kernel/src/dma.c`  
Header: `include/ozero/mm.h` (`o0_dma_*`)

### 8.1 Design

The DMA allocator returns physically contiguous segments from the PMM. The
caller supplies constraints: `addr_mask` (a contiguous low-bit mask),
`alignment` (power of two or 0 for page alignment), `max_segment` (bytes per
contiguous segment, 0 for no limit) and `coherent`. There is no IOMMU at this
stage: returned addresses are untranslated physical addresses and are only safe
for trusted in-kernel drivers.

`o0_dma_pin`/`o0_dma_unpin` nest: each pin must be matched by exactly one unpin,
normally in the completion callback. `o0_dma_free` returns `O0_EBUSY` while a
pin is held, so a buffer can never be returned to the page allocator underneath
a request that is still in flight.

---

## 9. Allocation flows

### 9.1 User memory object creation

```
1. o0_budget_charge(budget, page_array_bytes)
2. o0_kalloc(page_array_bytes, 8)        // page array from slab
3. For each page:
   a. o0_budget_charge(budget, O0_PAGE_SIZE)
   b. o0_pmm_alloc_pages(0)               // physical page from PMM
   c. o0_phys_to_virt(phys)               // HHDM conversion
   d. memset(page, 0, O0_PAGE_SIZE)
4. o0_object_alloc(O0_OBJ_MEMORY, ...)   // object header charged to budget
5. Publish handle
```

Failure at any step unwinds all previous charges and allocations.

### 9.2 Memory object mapping

```
1. Validate rights, alignment, range, W^X
2. Check o0_as_budget(as) == memory->obj.budget
3. Check destination is unmapped
4. For each page:
   a. o0_as_map(as, vaddr, phys, O0_PAGE_SIZE, O0_MAP_USER|O0_MAP_BORROWED|prot)
5. o0_as_add_borrowed(as, vaddr, len, &memory->obj)
```

### 9.3 Address-space teardown

```
1. For each user PML4 entry:
   a. Walk PDPT → PD → PT
   b. For each leaf page:
      - If O0_ENTRY_SW_BORROWED: do not free the physical page
      - Else: o0_pmm_free_pages(phys, 0), uncharge budget
   c. Free PT, PD, PDPT pages back to PMM
2. Free PML4 page back to PMM
3. For each borrowed range: o0_object_unref(owner)
```

---

## 10. What is not part of this record

The following are explicitly **not** implemented and are not described here as
if they were:

- **Demand paging** — all memory-object pages are allocated eagerly at
  `o0_memory_create()` time; there is no page-fault-driven lazy allocation.
- **Memory-object sharing across address spaces** beyond borrowed mappings — a
  memory object's pages may be mapped into one address space at a time through
  `o0_memory_map`; there is no copy-on-write or shared-page protocol.
- **Free-list buddy allocator** — the PMM is a bitmap with buddy-coalescing
  validation, not a free-list buddy allocator with split/merge metadata.
- **Physical page reclaim beyond bootloader memory** — only
  `O0_MEM_RECLAIMABLE` regions are releasable; ACPI-reclaimable and other
  reserved types are not.

See `docs/decisions/0010-memory-hardening-smep-smap.md` §5 for the recorded
deferral of KASLR, buddy and reclaim as separate items.