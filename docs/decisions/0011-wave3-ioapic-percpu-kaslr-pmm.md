# ADR 0011: Wave 3 completion — I/O APIC routing, per-CPU scheduling, KASLR, buddy PMM and reclaim

- **Status:** Accepted-provisional (implemented and emulator-verified; review requested)
- **Date:** 2026-11-24
- **Wave:** 3
- **Supersedes:** the "not started" clauses of ADR 0009 §3.4 and ADR 0010 §3.5
- **Related:** ADR 0005 (LAPIC ExtINT), ADR 0007 (user ABI and per-CPU reserve),
  ADR 0009 (SMP bring-up), ADR 0010 (SMEP/SMAP), `docs/hardening-roadmap.md`

## 1. Context

ADR 0009 brought the APs online, but deliberately left the scheduler BSP-only and
left I/O APIC routing, per-CPU runqueues, KASLR, buddy allocation and bootloader
reclaim unclaimed. ADR 0010 kept the bitmap PMM and the reclaim stub. This ADR
records the decisions that close those items, and the limits that remain.

The constraints are unchanged: Ozero owns its native execution architecture;
Linux is not the reference; hardware features are used only when CPUID or ACPI
advertises them; and no evidence is inferred across host, QEMU or physical runs.

## 2. Alternatives

### Interrupt routing

| Alternative | Trade-off |
|---|---|
| **I/O APIC when the MADT exposes one, 8259 otherwise (chosen)** | Removes the 8259 from the normal QEMU path, supports ISA overrides and level/edge polarity, and keeps a working fallback for firmware without a usable MADT. |
| Keep the 8259 only | Simpler, but leaves the I/O APIC and GSI overrides unexercised and keeps the legacy PIC in the hot path. |
| MSI/MSI-X first | More modern, but needs PCI capability parsing and per-device vector allocation; deferred to Wave 5. |
| x2APIC | Not assumed; xAPIC MMIO remains the common path. |

### Scheduler topology

| Alternative | Trade-off |
|---|---|
| **Per-CPU runqueues with kernel-thread migration (chosen)** | Exercises AP scheduling without changing the user ABI. Ready threads move between CPUs; the current CPU reschedules through the existing IPI vector. |
| Keep one global queue | Fewer races, but the APs never run work and per-CPU state stays untested. |
| User threads on APs now | Requires per-CPU TSS/IST and a user-thread affinity model; deferred because the shared TSS would corrupt RSP0. |
| Work stealing | More balanced, but needs a second queue policy and more tests; deferred. |

### KASLR

| Alternative | Trade-off |
|---|---|
| **Randomise the kernel-stack window only (chosen)** | The kernel image is linked at a fixed high-half address; moving the dedicated stack region is a real layout change with a small, testable surface. |
| Relocate the kernel image | Needs a relocatable link, relocation processing before paging, and a boot-protocol change; larger than this wave. |
| Randomise the direct map | Would break the HHDM contract used by every physical-to-virtual conversion. |
| No KASLR | Leaves the layout fixed and the ADR open. |

### Physical page allocator

| Alternative | Trade-off |
|---|---|
| **Bitmap PMM with aligned buddy blocks and coalescing checks (chosen)** | Keeps the existing bitmap and its churn test, adds power-of-two alignment for large orders, and records buddy coalescing without introducing a second allocator. |
| Free-list buddy allocator | Classic, but replaces a tested allocator and needs split/merge metadata for every order. |
| Keep first-fit only | Large aligned allocations can fail even when enough free pages exist. |

### Bootloader-memory reclaim

| Alternative | Trade-off |
|---|---|
| **Release Limine `BOOTLOADER_RECLAIMABLE` after the boot contract copies its data (chosen)** | Returns real memory and is observable as a byte count. The kernel's boot info, memory map, RSDP-derived ACPI data and framebuffer description are already kernel-owned copies. |
| Release ACPI reclaimable too | The kernel does not yet copy every ACPI table it may need; unsafe until an ACPI-owned copy exists. |
| Never reclaim | Wastes memory and leaves the contract untested. |

### Lock ordering

| Alternative | Trade-off |
|---|---|
| **Opt-in lock classes plus an IRQ-safe runqueue lock (chosen)** | The checker validates ordered blocking locks and panics on inversion; runqueue locks disable interrupts and are not class-checked because they are never held across a blocking operation. |
| Check every lock | The scheduler's interrupt-safe critical sections would trip the checker during legitimate timer re-entry. |
| No checker | Cheaper, but lock inversions become timing-dependent bugs. |

## 3. Decision

1. **I/O APIC.** When the MADT provides an I/O APIC, `o0_ioapic_init()` maps its
   MMIO page, masks every redirection entry, routes ISA IRQs through the MADT
   override table (GSI, level/edge, polarity) and masks the 8259. Without a usable
   I/O APIC the kernel keeps the PIC path. The LAPIC timer is calibrated before
   the I/O APIC is enabled because calibration uses PIT IRQ0.
2. **Per-CPU scheduler.** Each CPU owns a runqueue and an idle thread. The BSP
   advances global time and scans sleepers; every CPU's LAPIC timer preempts its
   own runqueue. Kernel threads may be created on or migrated to an AP. User
   threads remain pinned to the BSP until per-CPU TSS/IST descriptors exist,
   because `TSS.RSP0` is still shared.
3. **TLB shootdown.** Vector `0x40` carries a range descriptor (`addr`, page
   count, all flag) in the target's per-CPU block. The sender waits for every
   online target with a bounded spin; timeout is a panic, never a silent stale
   TLB. Single-page, range and full-flush entry points share the protocol.
4. **KASLR.** `o0_kaslr_init()` derives one 2 MiB-aligned offset from `RDRAND`
   when available and `RDTSC` otherwise. The offset randomises the kernel-stack
   window (`O0_KASLR_KSTACK_BASE`, 1 GiB window); the kernel image and HHDM stay
   at their link-time addresses. The same function feeds every stack allocation.
5. **Buddy PMM.** The bitmap remains the source of truth. Allocation of order
   `n` requires a naturally aligned `2^n`-page block; free walks the buddy tree
   upward while the paired block is wholly free. Bootloader-reclaimable regions
   are tracked but reserved at initialisation.
6. **Reclaim.** `o0_pmm_release_bootloader_memory()` clears every reserved
   reclaimable region once, adds its pages to the free count and returns the
   released byte count. It runs after the boot information, ACPI data and SMP
   response have been consumed. ACPI reclaimable memory is not released.
7. **Lock checker.** Ordered locks carry a class; acquisition must strictly
   increase the class and release must be in reverse order. Unordered locks
   (class 0) are IRQ-safe spinlocks and are outside the checker. Runqueue locks
   are class 0 and disable interrupts around every queue operation.
8. **ABI reserve.** The `swapgs`/per-CPU block and the syscall STAR layout from
   ADR 0007 are unchanged, so adding per-CPU TSS descriptors later does not
   change the user ABI.

## 4. Consequences

- QEMU BIOS/UEFI, `qemu64`/`Haswell-v1`/`max`, `-smp 1/2/4` exercise the I/O
  APIC, AP scheduling, range shootdown, KASLR layout, aligned PMM allocation and
  reclaim. The evidence is emulator-only.
- `-cpu max` is the only local model with SMAP; `Haswell-v1` reports SMEP on and
  SMAP off.
- User threads still do not run on APs. Per-CPU TSS/IST, user-thread affinity,
  work stealing, ACPI reclaim and kernel-image relocation remain open and are
  not claimed.
- The buddy PMM is a bitmap with buddy alignment/coalescing, not a free-list
  allocator. The ADR names it that way to avoid overclaiming.
- KASLR randomises the stack window, not the kernel text. The ADR names it that
  way to avoid overclaiming.
