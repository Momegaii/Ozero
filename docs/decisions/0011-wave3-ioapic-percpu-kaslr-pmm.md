# ADR 0011: Wave 3 completion — I/O APIC routing, per-CPU scheduling, kernel-stack randomization, buddy PMM and reclaim

- **Status:** Accepted-provisional (implemented and emulator-verified; review requested)
- **Date:** 2026-11-24
- **Scope:** I/O APIC discovery and ISA interrupt routing, per-CPU runqueues with kernel-thread migration, TLB shootdown (single-page, range, full), kernel-stack randomization (KSR), buddy-aligned PMM, bootloader-memory reclaim, and lock-order classes.

## 1. Context

ADR 0009 brought the APs online, but deliberately left the scheduler BSP-only and
left I/O APIC routing, per-CPU runqueues, kernel-stack randomization, buddy allocation and bootloader
reclaim unclaimed. ADR 0010 kept the bitmap PMM and the reclaim stub. This ADR
records the decisions that close those items, and the limits that remain.

## 2. Alternatives considered

| Item | Alternative | Why it was not chosen |
|---|---|---|
| I/O APIC | Keep 8259 only | ISA IRQs cannot reach APs without the I/O APIC; the 8259 has no affinity. |
| Per-CPU runqueues | Single global runqueue | Contention scales poorly; the Director directed per-CPU queues with explicit migration. |
| TLB shootdown | Broadcast IPI with a global flush | Range and single-page flushes are cheaper when the mapping change is narrow. The protocol supports all three. |
| User threads on APs now | Requires per-CPU TSS/IST and a user-thread affinity model; deferred because the shared TSS would corrupt RSP0. |
| Work stealing | More balanced, but needs a second queue policy and more tests; deferred. |

### Kernel-stack randomization (KSR)

| Alternative | Trade-off |
|---|---|
| **Randomise the kernel-stack window only (chosen)** | The kernel image is linked at a fixed high-half address; moving the dedicated stack region is a real layout change with a small, testable surface. |
| Relocate the kernel image (KASLR) | Needs a relocatable link, relocation processing before paging, and a boot-protocol change; larger than this wave and not delivered. |
| Randomise the direct map | Would break the HHDM contract used by every physical-to-virtual conversion. |
| No KSR | Leaves the layout fixed and the ADR open. |

### Physical page allocator

| Alternative | Trade-off |
|---|---|
| **Bitmap with buddy alignment (chosen)** | The existing bitmap is the source of truth; adding buddy alignment and coalescing preserves it while satisfying order-`n` requests. |
| Free-list buddy allocator | Replaces the bitmap entirely; larger refactor, not needed for the current scale. |
| Slab-only | Cannot satisfy large physically contiguous requests (e.g., GPU rings). |

### Lock-order checker

| Alternative | Trade-off |
|---|---|
| **Default class 1 with explicit inner classes (chosen)** | A forgotten `o0_spin_init()` cannot silently disable the checker; the default matrix stays checker-off so a checker panic is attributable. |
| Opt-in classes only (previous) | A lock without `o0_spin_init_ordered()` gets class 0 and exits the checker; inversions go undetected. |
| Full lockdep with call-site tracking | Heavy; requires a lock name string and a per-CPU stack of held locks with call sites. The current design is a subset that fits the freestanding kernel. |

## 3. Decisions

1. **I/O APIC.** The MADT parser (`o0_madt_parse()`) finds the I/O APIC base, GSI base and redirection entries. `o0_ioapic_init()` redirects each ISA GSI to the BSP's LAPIC vector (0x20 + GSI), masks all others, and records the GSI→vector map. The 8259 is masked after the I/O APIC takes over.

2. **Per-CPU runqueues.** Each CPU has a `o0_runqueue_t` protected by an IRQ-safe spinlock (class 0). `o0_sched_enqueue()` and `o0_sched_dequeue()` operate on the current CPU's queue. `o0_sched_migrate()` removes the thread from its current queue, updates `thread->cpu`, and enqueues on the target CPU's queue. The scheduler tick runs on every CPU and only scans the local queue.

3. **TLB shootdown.** `o0_tlb_shootdown()` takes a mode (single-page, range, full), a target CPU mask, and the address/size. It writes the flush request (vector, count, all flag) in the target's per-CPU block. The sender waits for every online target with a bounded spin; timeout is a panic, never a silent stale TLB. Single-page, range and full-flush entry points share the protocol.

4. **Kernel-stack randomization (KSR).** `o0_kaslr_init()` derives one
   2 MiB-aligned offset and applies it to the dedicated kernel-stack window
   (`O0_KASLR_KSTACK_BASE`, 1 GiB window). The kernel image and HHDM stay at
   their link-time addresses, so this is **not KASLR** and must not be described
   as image randomisation.
   - **Entropy source:** if CPUID.01H:ECX bit 30 advertises `RDRAND`, up to 16
     `RDRAND` attempts are made and the first successful value is XORed with
     `RDTSC`. Without a successful `RDRAND`, the seed is `RDTSC` XOR
     `(address of layout_offset << 17)`. The seed is finalised with the
     MurmurHash3 64-bit finalizer and reduced modulo 512 slots of 2 MiB.
   - **Strength:** `RDRAND` is a hardware DRBG; the `RDTSC` fallback is
     low-entropy and must not be treated as a cryptographic ASLR boundary. KSR
     raises the cost of opportunistic stack-address guessing, nothing more.
   - The same function feeds every stack allocation, including kernel stacks in
     the randomized window.

5. **Buddy PMM.** The bitmap remains the source of truth. Allocation of order
   `n` requires a naturally aligned `2^n`-page block; free walks the buddy tree
   upward while the paired block is wholly free. Bootloader-reclaimable regions
   are released by `o0_pmm_release_bootloader_memory()` after boot info, ACPI
   data and the SMP response are copied. ACPI reclaimable is not released.

6. **Bootloader-memory reclaim.** `o0_pmm_release_bootloader_memory()` iterates
   the Limine memmap response and returns every `LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE`
   region to the PMM. The kernel image, modules and the framebuffer are excluded
   by type.

7. **Lock checker.** Ordered locks carry a class; acquisition must strictly
   increase the class and release must be in reverse order. Unordered locks
   (class 0) are IRQ-safe spinlocks and are outside the checker. Runqueue locks
   are class 0 and disable interrupts around every queue operation. Host tests
   compile with `O0_LOCK_ORDER_CHECK`, which gives every `o0_spin_init()` lock a
   default class of 1; one CI boot cell builds the kernel the same way. The
   default matrix stays checker-off so a checker panic is attributable.

8. **ABI reserve.** The `swapgs`/per-CPU block and the syscall STAR layout from
   ADR 0007 are unchanged, so adding per-CPU TSS descriptors later does not
   change the user ABI.

## 4. Consequences

- QEMU BIOS/UEFI, `qemu64`/`Haswell-v1`/`max`, `-smp 1/2/4` exercise the I/O
  APIC, AP scheduling, range shootdown, KSR layout, aligned PMM allocation and
  reclaim. The evidence is emulator-only.
- `-cpu max` is the only local model with SMAP; `Haswell-v1` reports SMEP on and
  SMAP off.
- The buddy PMM is a bitmap with buddy alignment/coalescing, not a free-list
  allocator. The ADR names it that way to avoid overclaiming.
- KSR randomises the stack window, not the kernel text or image. The ADR names
  it that way to avoid overclaiming; kernel image relocation (KASLR) remains
  deferred.