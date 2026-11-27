# ADR 0002: Stage 3 kernel execution model

- Status: **Proposed** — implementation landed for review; Project Director approval pending
- Date proposed: 2026-11-23
- Scope: kernel execution model, address spaces, scheduling and synchronization for Stage 3.

## Context

Stage 2 delivered a physical page allocator, a normalised boot memory map, page-table
construction and CPU feature detection, but the kernel still ran on the bootloader's page
tables and had no execution model. The Stage 3 exit criteria in `docs/ROADMAP.md` are
process/thread lifecycle, context switching, synchronization and scheduler tests.

Two problems had to be solved before any of that could be scheduled:

1. **The CR3 switch was blocked** because the kernel's physical load address was unknown.
   Page tables in BSS cannot be referenced by physical address without it.
2. **The kernel heap could not free**, so thread control blocks and per-thread stacks would
   have leaked from the first creation.

## Decision

### Physical load address and CR3

The kernel resolves its physical load address by walking the **bootloader's active page
tables** for `_kernel_start` (`o0_virt_to_phys`). The boot info records the result in
`kernel_phys`, which unblocks `o0_paging_load()`.

The kernel then builds its own tables and switches CR3 to them:

- a direct map (HHDM) window `[hhdm_offset, hhdm_offset + 4 GiB)` → physical `[0, 4 GiB)`,
  using 2 MiB pages, so every physical page the allocator hands out is reachable;
- the kernel image at its linked high-half address, 2 MiB pages;
- per-address-space 4 KiB mappings for everything else.

Page-table pages come from one static BSS pool (128 tables). Their physical addresses are
resolved with the same page walk, so no assumption is made about the kernel's load offset.

### Execution model

- Threads only: a thread is ring-0 kernel code with its own 16 KiB kernel stack.
- A process is a name, an address space and a thread group. No user-mode entry, no syscall
  ABI, no TLS programming yet.
- Preemptive, priority-aware round robin: highest priority runs first, FIFO within one
  priority, and the current thread keeps the CPU until its 5-tick timeslice expires.
- The preemption source is the 8254 PIT at 100 Hz through the remapped 8259 PIC
  (vectors 32–47). The local APIC is the intended replacement once ACPI parsing lands.
- A context switch saves the callee-saved register set (`rbx`, `rbp`, `r12`–`r15`) and the
  stack pointer. Because every thread owns a kernel stack, switching from the timer
  handler is safe: the interrupted thread resumes inside the handler and `iret`s when
  scheduled again.
- Blocking is explicit (`o0_sched_block`) with reasons: sleep, join, mutex. Sleeping
  threads are woken by the tick.
- Kernel heap: page-backed slab allocator with eight size classes and per-class free lists.

## Alternatives considered

| Alternative | Why not now |
|---|---|
| Keep a bump heap and defer free | Thread creation/exit leaks; Stage 3 needs create/destroy cycles in tests |
| Cooperative scheduling only | Cannot demonstrate preemption or timeslice fairness, both Stage 3 evidence |
| Local APIC timer first | Requires MADT parsing and a working interrupt-controller abstraction; PIT proves the model sooner |
| Per-address-space page-table pool allocated from the PMM | Needs a mapping for freshly allocated tables before the direct map is trusted; static pool is simpler and auditable at this stage |
| Identity-map low memory instead of HHDM | The bootloader already provides an HHDM; using it keeps one translation rule for physical access |

## Consequences

- The direct map covers only the first 4 GiB of physical memory. Allocations above that
  limit cannot be converted to a virtual address, and callers fail instead of guessing.
- `EFER.NXE` stays off, so no-execute is not enforced; `O0_MAP_EXEC`/`O0_MAP_NX` only
  describe intent.
- One page-table pool is shared by all address spaces and is never reclaimed. That bounds
  the number of simultaneously mapped address spaces (8) and the total mappings.
- Freed thread stacks are not returned to the PMM, and `o0_thread_join` releases the TCB
  slot but not the stack.
- Only the BSP runs; `o0_cpu_count()` still returns 1.
- No user/kernel isolation yet: threads share the kernel address space, and
  `o0_as_create` produces an address space that nothing enters.

## Follow-up decisions required

1. User-mode entry mechanism (`iretq` to ring 3) and the syscall ABI (`syscall`/`sysret`
   versus `int 0x80`), which decides GS/FS and kernel-stack switching.
2. AP bring-up order (APIC, SMP trampoline) and per-CPU data layout.
3. Page-table pool replacement: per-address-space tables from the PMM plus a table cache.
4. NX/W^X policy once `EFER.NXE` is enabled, including how the direct map is split.
5. Timer choice: PIT → APIC timer → TSC-deadline, and the tickless/idle policy.
