# Ozero Development Roadmap

These are development stages, not delivery-date promises. The order is intentional; later work depends on earlier foundations.

| Stage | Focus | Exit evidence |
|-------|-------|---------------|
| 0 | Architecture specification and toolchain | Approved architecture decisions, reproducible host toolchain, build/test conventions |
| 1 | Bootable x86-4 kernel | Reproducible boot in emulation, documented boot path, serial/debug output |
| 2 | Physical and virtual memory | Physical page allocator, virtual memory mappings, protection and fault tests |
| 3 | Processes, threads and scheduler | Process/thread lifecycle, context switching, synchronization and scheduler tests |
| 4 | Storage, filesystem and drivers | Block layer, initial storage drivers, filesystem operations and fault tests |
| 5 | GPU memory and Haswell submission | GPU virtual memory/residency and generation-correct Haswell submission/completion |
| 6 | Native graphical application environment | QEMU framebuffer display, input, surfaces, window manager, software compositor, desktop shell and documented integration evidence (see `docs/stage6.md`) |
| 7 | PE loader and Windows user-mode ABI | PE loading plus a measured initial Win32 compatibility test suite |
| 8 | Graphics compatibility and gaming | Graphics stack milestones, frame-time and compatibility measurements |
| 9 | Modern hardware expansion | Validated support added across selected modern platforms and devices |
| 10 | Advanced Windows compatibility | Broader API/runtime coverage with explicit compatibility results and known gaps |

## Stage progress

- **Stage 0** — Complete: architecture baseline approved (`docs/ARCHITECTURE_BASELINE.md`), toolchain scaffold in place.
- **Stage 1** — Boot verified in QEMU (BIOS and UEFI/OVMF): serial/debug workflow documented. Limine + ELF64 kernel boot path, COM1 serial, IDT exceptions, page tables, PMM, heap, timer-driven scheduler. Physical hardware tests remain open.
- **Stage 2** — Implemented: physical page allocator (bitmap with buddy coalescing), normalised boot memory map, 4-level page-table construction, page-fault error-code decoding, CPU feature discovery (CPUID). CR3 switch, HHDM direct map, `virt_to_phys`/`phys_to_virt` and address-space API landed with Stage 3. Remaining: memory reclaim beyond bootloader reclaim stub.
- **Stage 3** — Implemented and boot-verified in QEMU (BIOS and UEFI/OVMF): kernel runs on its own page tables; slab kernel heap; processes, kernel threads and per-thread stacks; assembly context switch; PIT/LAPIC-driven preemptive priority round-robin scheduler; spinlocks and blocking mutexes; host unit tests for allocator and scheduler. Remaining: physical hardware validation.
- **Stage 4** — Implemented at the QEMU substrate level: block layer with validated registration, request/transfer limits, atomic completion and safe timed-out request ownership; RAM disk with fault injection; draft OzeroFS with checksummed layout validation and runtime undo-image rollback for failed writes; expanded host tests pass (block 6/6, OzeroFS 10/10); QEMU BIOS and UEFI/OVMF smoke pass on q35 with qemu64 and Haswell-v1 CPU models. Remaining: real storage protocol drivers (AHCI/NVMe), partition discovery, the asynchronous I/O model, an approved filesystem format and crash-consistency/recovery design, physical validation. See `docs/decisions/0003-stage4-storage.md`.
- **Stage 5** — Software-foundation milestone closed by Project Director direction. Physical GPU execution and vendor-specific acceleration backends remain deferred. GPU-independent PCI discovery, Haswell PPGTT/GGTT encoding, vendor-neutral queue/fence model, software model self-test with zero MMIO writes. See `docs/decisions/0004-stage5-software-graphics-architecture.md` and `docs/stage5-gpu.md`.
- **Stage 6** — Active: native desktop implemented in QEMU. Display, input, surfaces, window manager, software compositor, custom O0 desktop UI (neon-green sphere launchers, purple assistant spheres, particles, node network, taskbar, panel, tray, assistant), cursor variants (arrow/busy/ibeam), focus borders, smooth fixed-point hover easing. Host-tested (15 desktop_ui tests pass), QEMU BIOS + UEFI smoke tests pass. Full Stage 6 acceptance remains open: end-to-end QEMU keyboard/mouse interaction evidence (manual USB keyboard testing required; QEMU `sendkey` does not produce observable PS/2 events), live QEMU drag/resize demonstration, and CI integration. Handoff: [docs/stage6.md](stage6.md).
- **Stages 7–10** — Not started. PE loader stubs present but Stage 7 scope not opened.

## Hardening waves

- **Wave 1** — Close open verification debt: **complete locally.** Stage 4 closing-fix regressions executed; UEFI/OVMF boot verified; CI matrix `{bios, uefi} × {qemu64, Haswell-v1}` plus ASan/UBSan host tests added; one interrupt-delivery defect fixed (ADR 0005: LAPIC ExtINT virtual-wire). GitHub Actions observation and physical hardware remain open.
- **Wave 2** — single-CPU user mode: **implemented and emulator-verified.** ADR 0007 accepted; per-process page tables from the PMM, ring 3 with TSS/IST, syscall ABI, usercopy, static ELF64 loader, negative tests, process churn. All four `{bios, uefi} × {qemu64, Haswell-v1}` cells pass locally on QEMU 10.0.13. No physical-hardware evidence.
- **Wave 3** — SMP and memory hardening: **implemented in source.** AP bring-up through Limine MP handoff, xAPIC IPIs, page/range/full TLB shootdown, I/O APIC routing, per-CPU runqueues with kernel-thread migration, LAPIC timers on every CPU, SMEP/SMAP, kernel-stack randomization (KSR), aligned buddy PMM with coalescing checks, bootloader-memory reclaim. User threads remain BSP-pinned until per-CPU TSS/IST exists. ADRs 0009, 0010, 0011 record the choices. No physical-hardware evidence.
- **Wave 4** — storage and filesystem: **Wave 4.1 implemented** (PCI capability walk, BAR sizing, MSI/MSI-X, DMA allocator with pinning). **Waves 4.2/4.3 implemented and build-verified** (virtio-blk, AHCI, NVMe drivers behind `o0_block_register`; `make kernel` passes with `-Werror`, `tests/storage_encode_test.c` passes). No QEMU storage cell observed green. Waves 4.4–4.7 partial: crash consistency per ADR 0012/0018 implemented and host-verified (redo journal v2; `o0fs_journal_test` 5/5, `fs_test` 10/10); partition discovery and VFS/page cache remain open.
- **Wave 5** — remaining platform services: **Wave 5.0 implemented** (capability handles, budgets, synchronous IPC). IPC extensions, futex-style primitives, timers, signals, PCI/MSI-X device probe/remove, syscall-argument audit, `mmap` and `spawn` ADRs not started.
- **Waves 6–7** — not started (fuzzing, concurrency testing, logging/backtraces, performance baselines; resume Stage 5/6 graphics).
- These waves change the stage sequence by Project Director direction; this note records that direction rather than redefining it. Each wave's architectural choices still need their own ADR before implementation.

## Dependency rules

- Stage 0 decisions constrain all implementation stages.
- Stage 1 must establish a repeatable boot/debug loop before substantial kernel work.
- Stage 2 is a prerequisite for robust process isolation and later GPU virtual-memory work.
- Stage 3 provides the execution model needed by user-mode services and compatibility layers.
- Stage 4 establishes the I/O foundation used by native services and Windows-compatible I/O.
- Stage 5 must implement Haswell Gen7.5 behavior without importing Gen8+ assumptions.
- Stages 7–10 are not reducible to PE loading; they require broad user-mode semantics, graphics, runtimes, and compatibility testing.

## Change control

Changing stage order, scope, or accepted architecture constraints requires a written proposal and Project Director approval. Agents may recommend changes but may not silently redefine the roadmap.
