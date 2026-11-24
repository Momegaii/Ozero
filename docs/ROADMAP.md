# Ozero Development Roadmap

These are development stages, not delivery-date promises. The order is intentional; later work depends on earlier foundations.

| Stage | Focus | Exit evidence |
|---:|---|---|
| 0 | Architecture specification and toolchain | Approved architecture decisions, reproducible host toolchain, build/test conventions |
| 1 | Bootable x86-64 kernel | Reproducible boot in emulation, documented boot path, serial/debug output |
| 2 | Physical and virtual memory | Physical page allocator, virtual memory mappings, protection and fault tests |
| 3 | Processes, threads and scheduler | Process/thread lifecycle, context switching, synchronization and scheduler tests |
| 4 | Storage, filesystem and drivers | Block layer, initial storage drivers, filesystem operations and fault tests |
| 5 | GPU memory and Haswell submission | GPU virtual memory/residency and generation-correct Haswell submission/completion |
| 6 | Native application environment | Native executable loading, system services, documented native ABI |
| 7 | PE loader and Windows user-mode ABI | PE loading plus a measured initial Win32 compatibility test suite |
| 8 | Graphics compatibility and gaming | Graphics stack milestones, frame-time and compatibility measurements |
| 9 | Modern hardware expansion | Validated support added across selected modern platforms and devices |
| 10 | Advanced Windows compatibility | Broader API/runtime coverage with explicit compatibility results and known gaps |

## Stage progress (2026-11-23)

- **Stage 0** — Complete: architecture baseline approved, toolchain scaffold in place.
- **Stage 1** — Boot verified in QEMU (BIOS) at the time of the Stage 2 wave; serial/debug workflow documented. Remaining: UEFI path exercise re-run, physical hardware tests. Re-verification is required for the current revision because the Stage 3 change set was produced without a local emulator.
- **Stage 2** — Implemented: physical page allocator, normalised boot memory map, 4-level page-table construction, page-fault error-code decoding, CPU feature discovery (CPUID). The CR3 switch, the HHDM direct map, `virt_to_phys`/`phys_to_virt` and the address-space API landed with Stage 3. Remaining: memory reclaim.
- **Stage 3** — Implemented and boot-verified in QEMU (BIOS): kernel runs on its own page tables; slab kernel heap; processes, kernel threads and per-thread stacks; assembly context switch; PIT-driven preemptive priority round-robin scheduler (demo threads run to completion under the timer); spinlocks and blocking mutexes; host unit tests for the allocator and scheduler. Remaining: UEFI boot verification (OVMF) and physical hardware validation.
- **Stage 4** — Implemented at the substrate level: block layer with validated registration, bio submission and a synchronous single-segment API; RAM disk as the first storage driver with fault injection; the draft OzeroFS filesystem (format, mount, files, directories, read/write/truncate/unlink, free-space accounting, mounted read-write); boot self-test with an injected storage fault; host unit tests for the block layer and the filesystem. Remaining: real hardware protocol drivers (AHCI/NVMe), partition discovery, the asynchronous I/O model, an approved filesystem format, crash-consistency work, UEFI boot and physical validation. See `docs/decisions/0003-stage4-storage.md`.
- **Stages 5–10** — Not started.

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
