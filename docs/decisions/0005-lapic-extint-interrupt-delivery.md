# ADR 0005 — Interrupt delivery: 8259 PIC through LAPIC ExtINT (virtual wire)

- Status: accepted, implemented (2026-11-24)
- Scope: `kernel/arch/x86_64/pic.c`, boot serial contract

## Context

Ozero still uses the legacy 8259 PIC with the 8254 PIT as the scheduler tick. Limine hands the
kernel a CPU with the local APIC reaching it in whatever state the firmware left. Two hardware
arrangements can deliver the PIC's INTR output:

1. **APIC globally disabled.** INTR reaches the CPU directly. This is what the kernel did
   before this ADR: it cleared `IA32_APIC_BASE` bit 11.
2. **APIC enabled with LINT0 programmed for ExtINT.** The 8259 output is delivered through
   the LAPIC's LINT0 pin as an ExtINT interrupt. This is the architectural "virtual wire"
   configuration used by legacy-capable kernels on APIC machines.

Verification on the current branch showed that arrangement 1 is not portable across emulators.
With `-machine q35 -cpu qemu64` on QEMU 8.2.2 (Ubuntu 24.04's packaged version), the guest
reached `OZERO: worker-b iteration=0` and then stopped. The QEMU monitor showed `pic0:
irr=03 imr=f8 isr=00` — IRQ0 (PIT) and IRQ1 (keyboard) pending and unmasked — while the CPU
was halted at `o0_cpu_idle`'s `hlt` with `RFL=0x246` (IF=1) and the local APIC reported
"APIC disabled". No interrupt was ever delivered (`-d int` recorded no `INT=0x20..0x2f`).
The identical image ran the scheduler to completion on QEMU 10.1.0. Real hardware is expected
to follow the architectural rule (INTR to the CPU when the APIC is globally disabled), so the
kernel was correct in principle but unusable on a widely deployed emulator.

## Alternatives considered

| Alternative | Why it was not chosen |
|---|---|
| Keep disabling the APIC | Works on QEMU >= 9 and presumably on hardware, but loses every timer interrupt on QEMU 8.2, which is the packaged version on Ubuntu 24.04 LTS and a likely CI/host baseline. A kernel that silently loses its tick on a common emulator is not verifiable there. |
| Keep the APIC disabled and require QEMU >= 9 | Documents the problem instead of fixing it, and leaves the CI matrix unable to use distro QEMU. It also does not exercise the virtual-wire path that real firmware commonly uses. |
| Migrate to the LAPIC timer now | The correct long-term answer, but it needs ACPI MADT parsing, APIC timer calibration and per-CPU vectors — Wave 2 SMP work. Doing it here would change the scheduling baseline in a verification-debt wave. |
| Move to the I/O APIC now | Same dependency as the LAPIC timer (MADT, GSI routing, MSI groundwork) and larger blast radius than the PIC path needs. |
| Touch the LAPIC through the HHDM direct map | The direct map maps the low 4 GiB with write-back 2 MiB pages. Device MMIO must be uncached; using the WB alias for APIC registers is architecturally wrong even where it happens to work. |

## Decision

`o0_pic_init` now calls `lapic_extint_init` before unmasking any 8259 line:

1. Skip (and fall back to the old disabled-APIC path) if CPUID leaf 1 EDX bit 9 reports no
   APIC.
2. Read `IA32_APIC_BASE` (MSR `0x1B`) for the APIC physical base, defaulting to `0xFEE00000`.
   The LAPIC page is mapped once at `0xffffc00000000000` with
   `O0_MAP_READ | O0_MAP_WRITE | O0_MAP_UC | O0_MAP_NX`, outside the HHDM window, so it can
   never alias RAM or the framebuffer.
3. Force legacy xAPIC MMIO mode (clear bit 10) and enable the unit (set bit 11).
4. Program `TPR = 0`, `SVR = 0x1FF` (software enable, spurious vector 255) and
   `LVT0 = 0x700` (ExtINT delivery, unmasked), then read back `SVR` to flush posted writes.

The boot log records which configuration is active:
`OZERO: interrupt delivery=lapic-extint` or `OZERO: interrupt delivery=pic-legacy`, and the
QEMU smoke test requires the marker (without pinning a specific value, because a CPU without
an APIC is allowed to take the legacy path).

## Consequences

- The timer interrupt now reaches the CPU on QEMU 8.2 and QEMU 10.x, with the 8259 still
  owning vector allocation. The scheduler, sleep/wake and preemption markers are meaningful
  evidence again in every matrix cell.
- The PIC's EOI remains a PIC EOI. ExtINT delivery needs no LAPIC EOI; the LAPIC's ISR does
  not record this vector.
- A kernel virtual address (`0xffffc00000000000`) is now reserved for the LAPIC page. Future
  device mappings must not reuse it.
- This is a bring-up configuration, not the end state. Wave 2 replaces it with MADT-driven
  LAPIC/IOAPIC setup and the APIC timer; that work must remove the 8259 path and the reserved
  window, and record its own ADR.
- Verification is emulator-only: the marker set passes in all four firmware/CPU cells on two
  QEMU versions. No physical hardware has been exercised, and this ADR does not claim any.
