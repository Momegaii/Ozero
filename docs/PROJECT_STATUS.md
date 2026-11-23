# Ozero Project Status

Last updated: 2026-11-23  
Current stage: **Stage 1 — Bootable x86-64 kernel bring-up**

## Verified repository baseline

- Repository: `Momegaii/Ozero`
- Default branch: `main`
- Previous development-tooling commit: `60d52cbddf39ff616f845ac59d974cadf7a25904`
- Project Director approved Limine + ELF64 and freestanding C on 2026-11-23.
- This wave adds the first kernel implementation and reproducible image/CI workflow.

## Implemented in this wave

- Freestanding x86-64 C kernel entry.
- Limine base revision 6 and memory-map requests using the official protocol header.
- COM1 serial diagnostics and memory-region reporting.
- ELF64 high-half linker script at `0xffffffff80000000`.
- Pinned Limine v11.4.1 binary source commit for image creation.
- BIOS/UEFI hybrid ISO recipe.
- QEMU interactive and GDB launch targets.
- Headless QEMU smoke test checking kernel entry, protocol acceptance, memory map, and boot-success log markers.
- GitHub Actions workflow to build the kernel/ISO and run the QEMU smoke test.

## Verification state

The GitHub Actions workflow is the authoritative execution environment for this commit. Do not mark the boot milestone verified until its build and QEMU job completes successfully. Physical hardware testing has not been performed.

## Stage 1 exit criteria

- [ ] Clean CI build produces a valid ELF64 kernel and bootable ISO.
- [ ] QEMU BIOS smoke test reaches kernel entry and reports the memory map.
- [ ] QEMU UEFI boot path is exercised.
- [ ] Serial/debug workflow is documented and repeatable.
- [ ] Fatal exception handling and CPU state diagnostics are implemented.
- [ ] Physical Haswell and at least one modern x86-64 machine are tested.

## Immediate next engineering wave

1. Confirm CI ELF build and QEMU BIOS boot.
2. Add IDT, exception stubs, and fatal register/stack diagnostics.
3. Add CPU feature discovery and platform handoff validation.
4. Implement physical memory-map normalisation and page-frame allocator design.
5. Begin physical memory management only after memory-map ownership/reclaim rules are documented.

## Reporting standard

Separate completed code, successful builds, emulator tests, and physical hardware tests. Never infer one from another.
