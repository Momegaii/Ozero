# Ozero x86-64 kernel

Kernel sources live under `src/` and architecture-specific code under `arch/x86_64/`.

## Current source files

- `src/main.c` — kernel entry (`kmain`): serial init, Limine base-revision validation, boot-info population, PMM init, CPU detection, paging table construction, kernel self-tests, memory-map report, halt loop.
- `src/boot_info.c` — populates the `o0_boot_info` contract from Limine responses.
- `src/pmm.c` — bitmap page-frame allocator over usable memory-map regions; early kernel heap (`o0_kalloc`/`o0_kfree`).
- `arch/x86_64/interrupts.S` — IDT exception stubs (vectors 0–31) and common dispatch frame.
- `arch/x86_64/interrupts.c` — IDT gate setup and `lidt`.
- `arch/x86_64/paging.c` — 4-level page-table builder (identity low map + kernel high-half).
- `arch/x86_64/cpu.c` — CPUID feature detection (`o0_cpu_detect`, `o0_cpu_features`, `o0_rdtsc`, `o0_cpu_idle`, etc.).
- `linker.lds` — ELF64 high-half link script at `0xffffffff80000000`. Defines `_kernel_start`, `_kernel_data_end`, `_kernel_end`.

## Current bring-up milestone

- Limine base revision 6 request.
- x86-64 ELF64 linked at `0xffffffff80000000`.
- COM1 serial initialisation and diagnostics.
- Limine memory-map request and typed region report.
- Safe halt loop after successful handoff.
- IDT installed for CPU exceptions 0–31 with fault diagnostics.
- Physical page-frame allocator (`o0_pmm_*`) and normalised boot info (`o0_boot_info`).
- Page-table construction (`o0_paging_init`); CR3 switch deferred.
- CPU feature detection (`o0_cpu_detect`): long_mode, pae, nx, sse2, apic, page_1gb, etc.
- Early kernel heap (`o0_kalloc`/`o0_kfree`).
- Kernel self-tests (heap + PMM) run at boot.

No libc, hosted runtime, scheduler, or device drivers yet.

Build with `make kernel`; create a bootable ISO with `make iso`; run it with `make run`. The first milestone is a boot/hand-off proof, not yet a usable operating system.
