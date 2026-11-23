# ADR 0001: x86-64 boot path and kernel implementation language

- Status: **Proposed — Project Director approval required**
- Date: 2026-11-23
- Scope: Stage 0 decisions required before the first bootable kernel implementation.

## Context

Ozero needs a small, repeatable x86-64 bring-up path that works in QEMU and can later be validated on physical systems. The project also needs a kernel language and toolchain that support low-level control, debugging, maintainability, and a growing driver ecosystem.

These choices affect the boot contract, linker layout, runtime assumptions, debugging, CI, and contributor requirements. They must not be hidden inside an early prototype.

## Boot-path options

1. **Limine boot protocol with an ELF64 kernel**
   - Advantages: existing bootloader, documented protocol, useful x86-64 support, less early firmware/loader code for Ozero to own.
   - Costs: external bootloader dependency and a protocol contract Ozero must track.
2. **UEFI application / custom UEFI loader**
   - Advantages: direct control of the loader and a smaller external dependency surface.
   - Costs: Ozero must own more early boot, memory-map, image-loading, and firmware-transition logic sooner.
3. **GRUB Multiboot2**
   - Advantages: mature ecosystem and broad emulator support.
   - Costs: another protocol contract and less alignment with a deliberately minimal modern UEFI-first bring-up unless the project chooses that trade-off.

## Kernel-language options

1. **C (freestanding) plus a small amount of assembly**
   - Advantages: mature x86 systems ecosystem, straightforward ABI/toolchain integration, broad debugger and compiler support.
   - Costs: memory-safety burden and need for strict coding rules, review, sanitizers where possible, and defensive APIs.
2. **Rust (no_std) plus a small amount of assembly**
   - Advantages: stronger memory-safety guarantees for much kernel code and expressive type systems.
   - Costs: more complex target/toolchain setup, unsafe boundaries remain, and low-level driver work still requires hardware expertise.
3. **C/Rust hybrid**
   - Advantages: permits gradual adoption and lets subsystems use the strongest fit.
   - Costs: increases ABI, build, debugging, and maintenance complexity before the project has a stable kernel boundary.

## Engineering recommendation (not an approval)

For the fastest transparent x86-64 bring-up, evaluate **Limine + ELF64** as the leading boot candidate. Evaluate **freestanding C plus assembly** against **Rust no_std** using a small identical proof-of-toolchain experiment before choosing the kernel language. Do not begin broad kernel implementation until both decisions are recorded as approved.

This recommendation is deliberately provisional. It does not authorize implementing either choice as the final Ozero architecture.

## Required decision evidence

- Minimal boot image can be built reproducibly from a clean checkout.
- Kernel receives and validates the bootloader-provided memory map and framebuffer/firmware data selected for the milestone.
- QEMU serial output and debugger attachment work.
- Compiler, linker, assembler, target triple, sysroot strategy, and versions are pinned.
- The same toolchain can build a minimal freestanding image without host libc leakage.
- The choice has a documented path to physical UEFI hardware validation.

## Approval

Project Director decision: **Pending**.

Approved boot path: _pending_  
Approved kernel language: _pending_  
Toolchain versions: _pending_
