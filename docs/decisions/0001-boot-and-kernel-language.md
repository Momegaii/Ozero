# ADR 0001: x86-64 boot path and kernel implementation language

- Status: **Accepted**
- Date proposed: 2026-11-23
- Decision recorded: 2026-11-23
- Scope: Stage 0 boot and kernel-language choices.

## Decision

The Project Director approved:

- **Boot protocol:** Limine.
- **Kernel executable:** ELF64.
- **Initial kernel language:** freestanding C, with assembly where architecture entry/interrupt work requires it.
- **Initial architecture:** x86-64.
- **First execution environment:** QEMU, followed by physical hardware validation.
- **Early physical target:** Intel Haswell (Gen7.5), without importing Gen8+ GPU assumptions.

## Implementation pins

- Limine binary release: v11.4.1, repository commit `5be26a73d7b7b4d4477d18be94e1d16e615adf56`.
- Limine protocol header: official protocol repository commit `877c2d4c50e170575f8b8e7fbf7b8fa89b1dc157`.
- Kernel compiler interface: host GCC targeting x86-64 ELF with freestanding flags.
- Linker: GNU ld, ELF64 x86-64 output, high-half kernel link address `0xffffffff80000000`.
- Build dependencies: GNU Make, GCC, GNU binutils, curl, tar, xorriso.
- Emulator/test dependency: QEMU system x86-64.

The compiler and linker are currently selected by the host environment; exact distro package versions are not yet locked. CI provides a repeatable Ubuntu runner, but a fully hermetic toolchain container remains future work.

## Rationale

Limine avoids making Ozero responsible for a custom firmware loader during the first kernel milestone. ELF64 is the protocol's recommended executable format. Freestanding C gives direct control over the ABI, linker layout, and low-level hardware interfaces while keeping the first bring-up toolchain straightforward.

## First implementation milestone

- Enter the x86-64 kernel through Limine.
- Validate the requested Limine base revision.
- Initialise COM1 serial output.
- Request and print the Limine memory map.
- Halt safely after reporting successful boot.
- Build a hybrid BIOS/UEFI ISO and smoke-test it in QEMU.

## Follow-up decisions still required

This ADR does not settle the complete kernel architecture. Separate decisions are required for physical memory management, virtual address layout, interrupt controllers, SMP startup, scheduler, syscall ABI, driver model, and userspace ABI.
