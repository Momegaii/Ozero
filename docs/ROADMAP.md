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
| 6 | Native graphical application environment | QEMU framebuffer display, input, surfaces, window manager, software compositor, desktop shell and documented integration evidence (see `docs/stage6.md`) |
| 7 | PE loader and Windows user-mode ABI | PE loading plus a measured initial Win32 compatibility test suite |
| 8 | Graphics compatibility and gaming | Graphics stack milestones, frame-time and compatibility measurements |
| 9 | Modern hardware expansion | Validated support added across selected modern platforms and devices |
| 10 | Advanced Windows compatibility | Broader API/runtime coverage with explicit compatibility results and known gaps |

## Stage progress (2026-11-24)

- **Stage 0** — Complete: architecture baseline approved, toolchain scaffold in place.
- **Stage 1** — Boot verified in QEMU (BIOS) at the time of the Stage 2 wave; serial/debug workflow documented. The current revision was re-verified in BIOS **and** UEFI/OVMF in hardening Wave 1. Remaining: physical hardware tests.
- **Stage 2** — Implemented: physical page allocator, normalised boot memory map, 4-level page-table construction, page-fault error-code decoding, CPU feature discovery (CPUID). The CR3 switch, the HHDM direct map, `virt_to_phys`/`phys_to_virt` and the address-space API landed with Stage 3. Remaining: memory reclaim.
- **Stage 3** — Implemented and boot-verified in QEMU (BIOS): kernel runs on its own page tables; slab kernel heap; processes, kernel threads and per-thread stacks; assembly context switch; PIT-driven preemptive priority round-robin scheduler (demo threads run to completion under the timer); spinlocks and blocking mutexes; host unit tests for the allocator and scheduler. UEFI boot verification under OVMF was completed in hardening Wave 1. Remaining: physical hardware validation.
- **Stage 4** — Implemented and verified at the QEMU BIOS substrate level: block layer with validated registration, request/transfer limits, atomic completion and safe timed-out request ownership; RAM disk with fault injection; draft OzeroFS with checksummed layout validation and runtime undo-image rollback for failed writes; expanded host tests pass (block 6/6, OzeroFS 10/10); QEMU BIOS **and UEFI/OVMF** smoke passes on q35 with qemu64 and Haswell-v1 CPU models (UEFI added in hardening Wave 1, QEMU 8.2.2 and 10.1.0), and the closing-fix regressions are executed. Remaining: real hardware protocol drivers (AHCI/NVMe), partition discovery, the asynchronous I/O model, an approved filesystem format and crash-consistency/recovery design, and physical validation. See `docs/decisions/0003-stage4-storage.md`.
- **Stage 5** — Software-foundation milestone closed by Project Director direction. Physical GPU installation and hardware execution remain deferred; vendor-specific acceleration backends are future work. See `docs/decisions/0004-stage5-software-graphics-architecture.md` and `docs/stage5-gpu.md`.
- **Stage 6** — Paused by Rule 0 of the hardening directive. Phases 6.3–6.8 are implemented, host-tested and wired into the boot path (window manager, compositor, input router, desktop shell loop); no Stage 6 desktop or QEMU acceptance is claimed. Stage 6/5 GPU work resumes in hardening Wave 7, after user mode, SMP and a real storage driver exist. Handoff: [docs/stage6.md](stage6.md).
- **Stages 7–10** — Not started.

## Hardening waves (2026-11-24)

The Project Director paused new GPU/Stage 6 scope and ordered a sequence of hardening waves. The plan,
per-item status and evidence live in [docs/hardening-roadmap.md](hardening-roadmap.md).

- **Wave 1 — Close open verification debt: complete locally (2026-11-24).** Stage 4 closing-fix
  regressions executed; UEFI/OVMF boot verified with firmware markers; CI matrix
  `{bios, uefi} × {qemu64, Haswell-v1}` plus ASan/UBSan host tests added; one interrupt-delivery
  defect fixed (ADR 0005). GitHub Actions observation and physical hardware remain open.
- **Wave 2 — single-CPU user mode: implemented and verified in emulation (2026-11-24).**
  Sub-wave 2.0 (Wave 1 housekeeping) is done; 2.1 is
  [ADR 0007](decisions/0007-user-abi-syscall-and-executable.md), accepted by the Director's
  execution directive; 2.2–2.6 are implemented (per-process page tables from the PMM, ring 3
  with TSS/IST, syscall ABI, usercopy, static ELF64 loader, negative tests) and 2.7 passes the
  local `{bios, uefi} × {qemu64, Haswell-v1}` matrix plus the sanitizer job, and CI run
  [#136](https://github.com/Momegaii/Ozero/actions/runs/45502990041) passed all five jobs.
  There is no physical-hardware evidence. Detail and evidence: [docs/hardening-roadmap.md](hardening-roadmap.md).
- **Wave 3 — SMP and memory hardening: implemented and locally emulator-verified
  (2026-11-24), not signed off.** AP bring-up through the Limine MP handoff, xAPIC IPIs,
  page/range/full TLB shootdown, I/O APIC routing, per-CPU runqueues with kernel-thread
  migration, LAPIC timers on every CPU, SMEP/SMAP, kernel-stack randomization (KSR), an aligned buddy PMM
  and bootloader-memory reclaim are done. User threads remain BSP-pinned until per-CPU TSS/IST
  exists. ADRs [0009](decisions/0009-smp-bringup-and-local-apic-timer.md),
  [0010](decisions/0010-memory-hardening-smep-smap.md) and
  [0011](decisions/0011-wave3-ioapic-percpu-kaslr-pmm.md) record the choices. There is no
  physical-hardware evidence.
- **Wave 5.0 — capability handles, budgets and synchronous IPC: implemented and verified in
  emulation ahead of the reorder (2026-11-24).** Design and implementation record:
  [ADR 0008](decisions/0008-capability-handles-budgets-and-ipc.md) and
  [wave5-capability-ipc.md](wave5-capability-ipc.md). Host `cap_test`, the full local four-cell
  matrix on QEMU 10.0.13, CI run
  [#138](https://github.com/Momegaii/Ozero/actions/runs/45587151198) with all five jobs on QEMU
  10.2.1, and `make test-sanitize` pass; eight churn pairs return object, budget and PMM
  accounting to baseline. There is no physical-hardware evidence.
- **Waves 4–7 — planned, not started.** Wave 4 (virtio-blk/AHCI/NVMe, partitions, crash
  consistency, VFS), Wave 5 (remaining IPC/timers/signals, PCI/MSI-X/device probe, argument
  audit, mmap/spawn ADRs), Wave 6
  (fuzzing, concurrency testing, logging/backtraces, performance baselines), Wave 7 (resume
  Stage 5/6 graphics).
- These waves change the stage sequence by Project Director direction; this note records that
  direction rather than redefining it. Each wave's architectural choices still need their own
  ADR before implementation.

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
