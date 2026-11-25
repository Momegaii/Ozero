# ADR 0009: SMP bring-up, cross-CPU invalidation and the local APIC timer

- **Status:** Accepted-provisional; scheduler/topology clauses 3.4 and 4 are superseded by ADR 0011
- **Date:** 2026-11-25
- **Wave:** 3
- **Supersedes:** none
- **Related:** ADR 0005 (LAPIC ExtINT), ADR 0007 (user ABI), `docs/hardening-roadmap.md`

## 1. Context

Wave 2 verified one CPU in ring 3. The revised Wave 3 adds SMP and memory
hardening before storage work. The kernel already runs on its own page tables,
has a single per-CPU block reserved by ADR 0007, and receives the legacy 8259
through the LAPIC ExtINT virtual wire (ADR 0005).

The design has to answer four questions without assuming optional hardware:
how APs are released, which APIC mode is used, how a page is invalidated on
another CPU, and which timer drives the scheduler.

## 2. Alternatives

### AP release

| Alternative | Trade-off |
|---|---|
| **Limine MP protocol (chosen)** | The bootloader parks APs and releases one when `goto_address` is published. Ozero still owns the AP entry, stack, page tables, GDT/IDT and APIC setup. No real-mode trampoline or SIPI sequence is written. |
| Ozero real-mode AP trampoline | More self-contained, but requires a low-memory trampoline, INIT-SIPI sequencing, A20 and mode transitions. It duplicates a bootloader responsibility and adds a large failure surface before SMP can be tested. |
| No AP bring-up | Cheapest, but leaves `swapgs`, per-CPU data and TLB shootdown unexercised. |

### APIC mode

| Alternative | Trade-off |
|---|---|
| **xAPIC MMIO only (chosen)** | Works on the QEMU models in the matrix and on the Director's Haswell host. The BSP already maps the LAPIC page for ExtINT. |
| x2APIC | Not assumed. It needs MSR access, a different ICR and no MMIO window; QEMU TCG coverage and firmware hand-off vary. |
| Mixed mode | More branches, no benefit for this wave. |

### Cross-CPU invalidation

| Alternative | Trade-off |
|---|---|
| **Single-page IPI shootdown (chosen)** | Vector `0x40`, one address in the target's per-CPU block, acknowledgement counter. Simple enough to test with two and four CPUs. |
| Broadcast INVLPG | The architecture has no broadcast `INVLPG`. |
| PCID/INVPCID | Not assumed; `INVPCID` is absent on some TCG models and PCID is not enabled yet. |
| Deferred/range shootdown | More efficient, but needs a queue and a generation protocol. Deferred. |

### Scheduler timer

| Alternative | Trade-off |
|---|---|
| **LAPIC timer calibrated against the PIT (chosen)** | Ten PIT ticks give a known interval. The PIT is then stopped and IRQ0 masked. No invariant-TSC or TSC-deadline assumption. |
| Keep the PIT | Works, but does not exercise per-CPU timers and keeps the 8259 in the hot path. |
| TSC-deadline | Not assumed; not present on every TCG model. |
| HPET | Needs another ACPI table and comparator programming. Deferred. |

## 3. Decision

1. The kernel requests `LIMINE_MP_REQUEST_ID` with `flags = 0`. Limine starts
   APs only when the request is present.
2. `o0_smp_start()` assigns each AP a slot index, stores it in
   `extra_argument`, publishes `o0_ap_entry` with release semantics, and waits
   for the online counter. The assembly entry switches to a kernel-owned
   per-CPU stack before entering C.
3. Each AP loads the kernel CR3, enables NX/WP and SMEP/SMAP when advertised,
   reloads the GDT and SYSCALL MSRs, installs the IDT, enables its LAPIC in
   xAPIC MMIO mode, masks LINT0 (the 8259 belongs to the BSP), marks itself
   online and idles with interrupts enabled.
4. The scheduler remains BSP-driven in this wave. APs service IPIs only; they
   do not run user threads. Per-CPU runqueues are explicitly not claimed.
5. TLB shootdown uses vector `0x40`. The sender stores the address, resets the
   acknowledgement, publishes `tlb_pending`, sends a fixed-delivery IPI, waits
   for the acknowledgement and then invalidates locally.
6. The BSP scheduler timer is the LAPIC timer on vector `0x41`, calibrated
   against ten PIT ticks, divisor 16, periodic mode. The PIT is stopped and
   IRQ0 masked only after successful calibration. If calibration fails, the
   PIT remains the timer.
7. Vectors `0x40` and `0x41` are reserved in the generic vector allocator.

## 4. Consequences

- `-smp 1`, `-smp 2` and `-smp 4` are exercised on `qemu64`, `Haswell-v1` and
  `max` under BIOS and UEFI. The evidence is QEMU only.
- `SMEP` is available on `Haswell-v1`; `SMAP` is not. The SMAP path is verified
  with `-cpu max` locally and is not claimed for `Haswell-v1`.
- APs are online and acknowledge TLB shootdowns. Per-CPU runqueues, kernel-thread
  migration and AP preemption are now recorded in ADR 0011; user threads remain
  BSP-pinned until per-CPU TSS/IST descriptors exist.
- The APs use the BSP's TSS. That is safe while APs never enter ring 3; a
  per-CPU TSS/IST layout is required before APs run user threads.
- KASLR, a buddy allocator and bootloader-memory reclaim are not part of this
  ADR. They remain open Wave 3 items.
