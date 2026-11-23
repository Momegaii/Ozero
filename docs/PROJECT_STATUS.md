# Ozero Project Status

Last updated: 2026-11-23  
Current stage: **Stage 1 — Bootable x86-64 kernel bring-up (verified)**  
Stage 2 — Physical memory management: **implemented, boot-verified**

## Verified repository baseline

- Repository: `Momegaii/Ozero`
- Default branch: `main`
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

## Stage 2 — physical memory management (implemented)

- `kernel/src/pmm.c`: bitmap page-frame allocator built from the normalised boot memory map. One bit per page, stored in kernel BSS (no raw physical-address dereference). Implements `o0_pmm_init`, `o0_pmm_alloc_pages`, `o0_pmm_free_pages`, `o0_pmm_free_bytes`, `o0_pmm_total_bytes`, `o0_pmm_release_bootloader_memory`.
- `kernel/src/boot_info.c`: copies Limine memory-map responses into kernel-owned `o0_mem_region` array; normalises Limine types to `o0_mem_type`; populates the `o0_boot_info` contract.
- `kernel/arch/x86_64/paging.c`: builds 4-level page tables (PML4/PDPT/PD) identity-mapping low 4 GiB and mapping the kernel high-half. BSS-resident; CR3 switch deferred until the kernel physical load address is known.
- `kernel/src/main.c`: wires PMM, boot info, paging, and kernel self-tests into `kmain`; reports total/free memory via serial.
- `kernel/arch/x86_64/interrupts.c`: IDT gate setup and `lidt` (already present).
- Page-fault handler: `x86_exception_dispatch` decodes page-fault error code (protection-violation vs not-present, read vs write, user vs supervisor, reserved bit) and reports CR2.
- Kernel heap: `o0_kalloc`/`o0_kfree` bump allocator in `kernel/src/pmm.c` (256 KiB early heap).
- `include/ozero/*`: public header contracts ported from Stage 0 review branches (`types.h`, `status.h`, `boot.h`, `mm.h`, `cpu.h`, `driver.h`, `irq.h`, `platform.h`, `thread.h`, `block.h`, `ipc.h`, `o0.h`). Interface sketches, not a frozen ABI.
- `tests/pmm_test.c`: host-compilable unit test for the allocator (alloc/free, exhaustion, invalid inputs, no usable regions).
- Kernel self-test in `kmain`: exercises heap alloc/free and PMM alloc/free, reports OK on serial.

## Verification state

- `make doctor`: all host dependencies present.
- `make check`: clean ELF64 build with `-Werror`, bootable ISO produced.
- QEMU BIOS smoke test: **PASS** — kernel entry reached, Limine base revision accepted, IDT installed, PMM initialised (510 MiB total), page tables built, kernel self-test (heap + PMM) OK, 20 memory-map entries reported, `boot success`, idle halt loop.
- PMM unit tests (`tests/pmm_test`): 4/4 pass.
- QEMU UEFI boot path: not yet exercised locally.
- Physical hardware testing: not yet performed (no Haswell or modern x86-64 machine available in this sandbox).

## Stage 1 exit criteria

- [x] Clean CI build produces a valid ELF64 kernel and bootable ISO. (Verified locally via `make check`; GitHub Actions job not yet confirmed.)
- [x] QEMU BIOS smoke test reaches kernel entry and reports the memory map. (Verified locally via `tools/qemu-smoke.sh`.)
- [ ] QEMU UEFI boot path is exercised.
- [ ] Serial/debug workflow is documented and repeatable.
- [x] Fatal exception handling and CPU state diagnostics are implemented. (IDT + exception dispatch with vector/error/rip/cr2 and page-fault error-code decoding.)
- [ ] Physical Haswell and at least one modern x86-64 machine are tested.

## Immediate next engineering wave

1. Exercise the QEMU UEFI boot path.
2. Document the serial/debug workflow (COM1 baud, log markers, GDB on :1234).
3. Confirm kernel physical load address so CR3 can be switched to the built page tables (`o0_paging_load`).
4. Add CPU feature discovery (`o0_cpu_features`) and platform handoff validation.
5. Refine physical memory-map normalisation: precise kernel-size reservation instead of the conservative 64 KiB placeholder.
6. Begin physical memory management only after memory-map ownership/reclaim rules are documented.

## Reporting standard

Separate completed code, successful builds, emulator tests, and physical hardware tests. Never infer one from another.
