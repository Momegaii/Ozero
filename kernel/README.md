# Ozero x86-64 kernel

The first freestanding C kernel entry point lives in `src/main.c`.

## Current bring-up milestone

- Limine base revision 6 request.
- x86-64 ELF64 linked at `0xffffffff80000000`.
- COM1 serial initialisation and diagnostics.
- Limine memory-map request and typed region report.
- Safe halt loop after successful handoff.
- No libc, hosted runtime, allocator, interrupt handlers, scheduler, or device drivers yet.

Build with `make kernel`; create a bootable ISO with `make iso`; run it with `make run`. The first milestone is a boot/hand-off proof, not yet a usable operating system.
