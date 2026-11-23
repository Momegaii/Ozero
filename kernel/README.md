# Ozero x86-64 kernel

Kernel sources live under `src/` and architecture-specific code under `arch/x86_64/`.

## Current source files

- `src/main.c` — kernel entry (`kmain`): serial init, Limine base-revision validation, boot-info population, PMM init, paging table construction, memory-map report, halt loop.
- `src/boot_info.c` — populates the `o0_boot_info` contract from Limine responses.
- `src/pmm.c` — bitmap page-frame allocator over usable memory-map regions.
- `arch/x86_64/interrupts.S` — IDT exception stubs (vectors 0–31) and common dispatch frame.
- `arch/x86_64/interrupts.c` — IDT gate setup and `lidt`.
- `arch/x86_64/paging.c` — 4-level page-table builder (identity low map + kernel high-half).
- `linker.lds` — ELF64 high-half link script at `0xffffffff80000000`.

## Current bring-up milestone

- Limine base revision 6 request.
- x86-64 ELF64 linked at `0xffffffff80000000`.
- COM1 serial initialisation and diagnostics.
- Limine memory-map request and typed region report.
- Safe halt loop after successful handoff.
- IDT installed for CPU exceptions 0–31 with fault diagnostics.
- Physical page-frame allocator (`o0_pmm_*`) and normalised boot info (`o0_boot_info`).
- Page-table construction (`o0_paging_init`); CR3 switch deferred.

No libc, hosted runtime, scheduler, or device drivers yet.

Build with `make kernel`; create a bootable ISO with `make iso`; run it with `make run`. The first milestone is a boot/hand-off proof, not yet a usable operating system.
