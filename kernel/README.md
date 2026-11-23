# Ozero x86-64 kernel

Kernel sources live under `src/` and architecture-specific code under `arch/x86_64/`.
Internal headers shared between kernel modules live in `src/` (they are not part of the
public `include/ozero` contract).

## Current source files

- `src/main.c` — kernel entry (`kmain`): serial, Limine validation, boot info, PMM, CPU
  detection, page tables + CR3 switch, slab, IRQ/PIC/PIT, scheduler, self-tests, demo
  threads, scheduler start.
- `src/serial.c` — COM1 console (`o0_serial_*`).
- `src/string.c` — freestanding `memset`/`memcpy`/`memmove`/`memcmp`/`strlen`.
- `src/status.c` — `o0_status_name`.
- `src/boot_info.c` — populates the `o0_boot_info` contract from Limine responses and
  resolves the kernel physical load address.
- `src/pmm.c` — bitmap page-frame allocator over usable memory-map regions.
- `src/slab.c` — slab kernel heap behind `o0_kalloc`/`o0_kfree`.
- `src/irq.c` — interrupt dispatch, line registration, vector allocation,
  `o0_irq_save`/`o0_irq_restore`.
- `src/thread.c` — processes, TCBs, thread lifecycle, sleep and join.
- `src/sched.c` — preemptive priority round-robin scheduler.
- `src/sync.c` — spinlocks and blocking mutex.
- `src/thread_internal.h` — TCB/process definitions and internal scheduler interfaces.
- `src/serial.h` — serial console interface.
- `arch/x86_64/interrupts.S` — CPU exception stubs (vectors 0–31) and common frame.
- `arch/x86_64/interrupts.c` — IDT gates, including IRQ gates for vectors 32–47.
- `arch/x86_64/irq.S` — 16 IRQ stubs and dispatch trampoline.
- `arch/x86_64/context.S` — callee-saved context switch.
- `arch/x86_64/paging.c` — page tables, CR3 switch, address spaces, physical mapping.
- `arch/x86_64/cpu.c` — CPUID feature detection.
- `arch/x86_64/pic.c` — 8259 remap/mask/EOI and 8254 PIT programming.
- `arch/x86_64/io.h` — port I/O helpers.
- `linker.lds` — ELF64 high-half link script at `0xffffffff80000000`; defines
  `_kernel_start`, `_kernel_data_end`, `_kernel_end`.

## Current bring-up milestone (Stage 3)

- Limine base revision 6; COM1 diagnostics; exception IDT with fault decoding.
- Boot info with a resolved `kernel_phys` and the bootloader's HHDM offset.
- Physical page allocator, safe against preemption.
- Kernel page tables: HHDM direct map (first 4 GiB) + 2 MiB kernel image mapping; CR3
  switched to them at boot.
- Address spaces: create/map/unmap/protect, 4 KiB pages, kernel half shared.
- Slab kernel heap.
- PIC remapped to vectors 32–47; PIT tick at 100 Hz.
- Kernel threads (16 KiB stacks), processes, assembly context switch, preemptive priority
  round-robin scheduler, sleep and join.
- Spinlocks and blocking mutexes.
- Boot self-tests for heap, address spaces and synchronization; host unit tests via
  `make test`.

## Known limits

- No user mode, syscalls, or address-space switching on behalf of a thread.
- Direct map limited to the first 4 GiB of RAM; allocations above it cannot be mapped.
- NX is not enabled; `O0_MAP_EXEC`/`O0_MAP_NX` describe intent only.
- One static page-table pool (128 tables, 8 address spaces) that is never reclaimed.
- Freed thread stacks are not returned to the PMM.
- BSP only; no SMP, no APIC, no ACPI.
- No boot verification was performed for this revision in the authoring environment (no
  QEMU/xorriso); see `docs/PROJECT_STATUS.md`.

Build with `make kernel`; create a bootable ISO with `make iso`; run host tests with
`make test`; boot it with `make run`.
