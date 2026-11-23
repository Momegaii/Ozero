# Ozero Project Status

Last updated: 2026-11-23  
Current stage: **Stage 3 — processes, threads and scheduler (implemented, awaiting boot verification)**  
Stage 2 — physical and virtual memory: **implemented, verified by build + host tests only**  
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
- **Host unit tests:** `make test` — PMM 4/4, slab 5/5, scheduler 5/5 pass.
- **QEMU / ISO boot:** **not performed for this revision.** The development environment used
  for this change has neither `xorriso` nor `qemu-system-x86`, so `make iso`, the BIOS smoke
  test and the UEFI path were not run. Earlier Stage 2 notes that claimed BIOS and UEFI
  boots are historical claims from the authoring environment and are not evidence for this
  revision; they must be re-established by CI or a machine with QEMU installed.
- **GitHub Actions:** not confirmed for this revision.
- **Physical hardware:** never performed.

The `tests/pmm_test.c` failure recorded in review (free-page accounting) was a defect in the
test — `struct o0_boot_info` was used uninitialised — and is fixed; the test now zeroes the
structure. The documented build command for that test was wrong (it linked `pmm.c` twice)
and is fixed as well.

## Stage 3 exit criteria

- [x] Kernel runs on its own page tables (CR3 switched to kernel-built tables).
- [x] Virtual memory: address-space create/map/unmap/protect with page-level protection.
- [x] General-purpose kernel allocator that can free (slab).
- [x] Thread control blocks, process structures and per-thread kernel stacks.
- [x] Context switch implemented in assembly.
- [x] Timer-driven preemptive scheduler with priorities and timeslices.
- [x] Synchronization primitives: spinlock and blocking mutex.
- [x] Host-side tests for the allocator and scheduler rules.
- [ ] Boot verification in QEMU (BIOS and UEFI) on a machine with the emulator installed.
- [ ] Physical hardware validation.

## Immediate next engineering wave

1. Run `make iso`, `tools/qemu-smoke.sh` and a UEFI boot on a host with QEMU/xorriso; add
   Stage 3 serial markers to the smoke assertions (page tables active, scheduler started,
   worker iterations).
2. User-mode entry: GDT ring-3 segments, TSS with a kernel stack, `iretq` path and a
   syscall ABI decision (ADR 0002 follow-ups).
3. Replace the static page-table pool with PMM-backed per-address-space tables.
4. AP bring-up (ACPI MADT, local APIC, SMP trampoline) and the PIT → APIC timer migration.
5. Memory-map reclaim (`o0_pmm_release_bootloader_memory`) per `docs/boot-contract.md`.

## Reporting standard

Separate completed code, successful builds, host tests, emulator tests, and physical
hardware tests. Never infer one from another.
