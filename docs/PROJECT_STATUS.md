# Ozero Project Status

Last updated: 2026-11-23  
Current stage: **Stage 3 — processes, threads and scheduler (implemented; boot-verified in QEMU BIOS)**  
Stage 2 — physical and virtual memory: **implemented, verified by build + host tests + QEMU boot**  
Stage 1 — bootable x86-64 kernel: **implemented; see verification state**

## Repository baseline

- Repository: `Momegaii/Ozero`
- Default branch: `main`
- Project Director approved Limine + ELF64 and freestanding C on 2026-11-23.
- Stage 3 work lives on the `kilo/morning-dale-tmb` lineage; the kernel entry, IDT and
  serial path it builds on are the ones committed on `main`.

## Implemented

### Stage 1 — boot path

- Freestanding x86-64 C kernel entry through Limine, base revision 6.
- COM1 serial diagnostics, IDT for CPU exceptions 0–31, exception dispatch with
  vector/error/rip/cr2 and page-fault error-code decoding.
- ELF64 high-half link script at `0xffffffff80000000`.
- BIOS/UEFI hybrid ISO recipe and headless QEMU smoke test.
- GitHub Actions workflow building the kernel/ISO and running the smoke test.

### Stage 2 — memory

- `kernel/src/boot_info.c`: normalises the Limine memory map into `o0_boot_info`, and now
  resolves `kernel_phys` with a page-table walk instead of leaving it at 0.
- `kernel/src/pmm.c`: bitmap page-frame allocator, one bit per usable page, now with
  interrupt-masked allocation/free so it is safe against preemption.
- `kernel/arch/x86_64/paging.c`: 4-level tables with an HHDM direct map and a 2 MiB kernel
  mapping, physical page-table addressing resolved by page walk, real CR3 switch
  (`o0_paging_load`), address-space create/map/unmap/protect, `virt_to_phys`/`phys_to_virt`
  and checked `o0_hhdm_*` conversions.
- `kernel/arch/x86_64/cpu.c`: CPUID feature detection.
- `tests/pmm_test.c`: host unit test for the allocator.

### Stage 3 — execution model

- `kernel/src/slab.c`: page-backed slab allocator behind `o0_kalloc`/`o0_kfree`
  (8 size classes, 16-byte payload alignment, per-class free lists) replacing the
  Stage 2 bump heap.
- `kernel/src/thread.c`: TCBs, thread create/exit/join/sleep, processes, per-thread kernel
  stacks allocated from the PMM.
- `kernel/src/sched.c`: priority-aware preemptive round-robin scheduler with timeslices,
  explicit blocking and a tick-driven sleeper scan.
- `kernel/arch/x86_64/context.S`: callee-saved context switch.
- `kernel/arch/x86_64/pic.c`: 8259 remap to vectors 32–47, mask/EOI control and PIT
  programming; `kernel/arch/x86_64/irq.S` + `kernel/src/irq.c`: IRQ stubs, dispatch,
  registration and `o0_irq_save`/`o0_irq_restore`.
- `kernel/src/sync.c` + `include/ozero/sync.h`: spinlocks and a blocking mutex with
  ownership handoff.
- `kernel/src/serial.c`, `kernel/src/string.c`, `kernel/src/status.c`: shared serial console,
  freestanding memory primitives and status names.
- Boot self-tests in `kmain`: slab (allocation, alignment, reuse), address spaces
  (map/protect/unmap/ENOENT), spinlock and mutex semantics.
- `tests/slab_test.c` and `tests/sched_test.c`: host unit tests for the allocator and the
  scheduler rules; `make test` runs all three suites.

## Verification state

Truthful split by evidence type:

- **Build:** the kernel links cleanly with `-Werror` on the documented flags
  (`make kernel`, ELF64 x86-64). Performed in the current work environment.
- **Host unit tests:** `make test` — PMM 6/6, slab 5/5, scheduler 5/5, CPU pass.
- **QEMU / ISO boot (BIOS):** **performed and passing for this revision** with
  `make iso` + `tools/qemu-smoke.sh` on QEMU 10 (both `q35` and `pc` machines).
  The smoke test asserts the Stage 1/2 boot milestones plus the Stage 3 execution
  model: page tables active, kernel heap ready, scheduler initialised, Stage 3
  self-test complete, two kernel threads created, and both demo workers running
  all three iterations and exiting under the preemptive PIT scheduler.
- **UEFI boot:** **not performed for this revision.** The ISO carries a UEFI
  boot path (`limine-uefi-cd.bin`), but it has not been exercised under OVMF here.
- **GitHub Actions:** not confirmed for this revision.
- **Physical hardware:** never performed.

Boot verification of this revision surfaced and fixed three defects that build +
host tests alone could not catch:

1. **Kernel image mapping (`paging.c`).** The kernel is linked at
   `0xffffffff80000000` but Limine loads it at a physical address that is not
   congruent to the link address modulo 2 MiB. The old 2 MiB-page mapping aligned
   virtual and physical down independently, dropping the sub-2 MiB offset, so the
   first instruction after the CR3 switch read the wrong bytes and triple-faulted.
   The kernel image is now mapped with 4 KiB pages so the offset is preserved.
2. **`O0_ALIGN_UP`/`O0_ALIGN_DOWN` (`types.h`).** The `~((a) - 1u)` mask was
   computed in 32-bit unsigned, so aligning a 64-bit pointer with an `int`
   literal (e.g. `O0_ALIGN_DOWN(stack_top, 16)` in `thread.c`) zeroed bits 32..63
   and produced a near-NULL stack pointer. The mask is now computed in
   `o0_paddr_t`.
3. **Local APIC left enabled (`pic.c`).** Limine enables the LAPIC
   (`IA32_APIC_BASE` bit 11). With the APIC active, the 8259's INTR is routed
   through LINT0 and only reaches the CPU when LVT0 is programmed for ExtINT
   (virtual-wire) delivery; this PIC-only stage never does that, so every
   maskable interrupt was silently dropped and the PIT tick never fired. PIC init
   now clears the APIC global-enable bit so the 8259 delivers INTR directly;
   Stage 4 AP bring-up re-enables and configures it.

The `tests/pmm_test.c` failure recorded in review (free-page accounting) was a
defect in the test — `struct o0_boot_info` was used uninitialised — and is fixed;
the test now zeroes the structure. The documented build command for that test was
wrong (it linked `pmm.c` twice) and is fixed as well.

## Stage 3 exit criteria

- [x] Kernel runs on its own page tables (CR3 switched to kernel-built tables).
- [x] Virtual memory: address-space create/map/unmap/protect with page-level protection.
- [x] General-purpose kernel allocator that can free (slab).
- [x] Thread control blocks, process structures and per-thread kernel stacks.
- [x] Context switch implemented in assembly.
- [x] Timer-driven preemptive scheduler with priorities and timeslices.
- [x] Synchronization primitives: spinlock and blocking mutex.
- [x] Host-side tests for the allocator and scheduler rules.
- [x] Boot verification in QEMU (BIOS): boot milestones and the preemptive
      scheduler running the demo threads to completion (smoke test PASS).
- [ ] Boot verification in QEMU (UEFI) on a machine with OVMF installed.
- [ ] Physical hardware validation.

## Immediate next engineering wave

1. Run a UEFI boot under OVMF (the BIOS smoke test now passes); add the same
   Stage 3 serial markers to any UEFI smoke path.
2. User-mode entry: GDT ring-3 segments, TSS with a kernel stack, `iretq` path and a
   syscall ABI decision (ADR 0002 follow-ups).
3. Replace the static page-table pool with PMM-backed per-address-space tables.
4. AP bring-up (ACPI MADT, local APIC, SMP trampoline) and the PIT → APIC timer
   migration; the LAPIC disable added for the PIC path is removed here.
5. Memory-map reclaim (`o0_pmm_release_bootloader_memory`) per `docs/boot-contract.md`.

## Reporting standard

Separate completed code, successful builds, host tests, emulator tests, and physical
hardware tests. Never infer one from another.
