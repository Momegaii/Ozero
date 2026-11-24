# Ozero hardening roadmap (2026-11-24)

This document records the hardening directive accepted by the Project Director and tracks it
against the tree. It is a working plan, not a delivery promise: items are marked done only
with the evidence type named in the reporting standard (host test, emulator boot, physical
hardware — never inferred from one another).

**Rule 0 — new GPU/Stage 6 scope is paused.** Open Stage 6 phases are finished only where
they do not block the waves below. Stage 5/6 work resumes in Wave 6, after user mode, SMP
and a real storage driver exist. No Stage 6 phase is claimed complete by this document.

## Wave 1 — Close open verification debt

| # | Item | Status | Evidence |
|---:|---|---|---|
| 1 | Execute the authored-but-unrun Stage 4 regressions and tick the checklist | **Done** | Host tests: block layer 6/6, OzeroFS 10/10 (0 failures) in `make test` and again in `make test-sanitize`. The seven closing-fix regressions (late completion after timeout, driver-confirmed cancellation, transfer limits, checksummed malformed superblocks, lookup I/O propagation, partial-overwrite rollback, final inode persistence failure) all execute and pass. Checklist ticked in `docs/PROJECT_STATUS.md`. |
| 2 | Boot under OVMF (UEFI) and add smoke markers for it | **Done** | Kernel requests the Limine EFI system table and prints `OZERO: firmware=UEFI` / `OZERO: firmware=BIOS`. `tools/qemu-smoke.sh` gained `QEMU_FIRMWARE=uefi` (OVMF 4 MiB code + per-run variable store) and asserts the matching firmware marker. UEFI boots reach the full marker set, including the scheduler/storage/GPU self-tests. |
| 3 | Confirm GitHub Actions on the current branch, not just local runs | **Blocked in this environment** | The sandbox has no GitHub API access (unauthenticated `api.github.com` returns 403) and no token, so workflow runs cannot be observed here. The workflow file is updated for the matrix; observing the run requires the repository UI/API after the branch is pushed. Not claimed as passing. |
| 4 | Add a CI matrix: `qemu64`, `Haswell-v1`, BIOS and UEFI, with ASan/UBSan host tests | **Done (local), pending CI observation** | `.github/workflows/quality.yml` now has a 4-cell `kernel-and-boot` matrix (CPU × firmware) plus a `host-sanitizers` job running `make test-sanitize`. The same four cells were executed locally on QEMU 8.2.2 and QEMU 10.1.0 and all passed. See `docs/decisions/0006-verification-matrix.md`. |

Wave 1 also produced one kernel fix. On QEMU 8.2 (Ubuntu 24.04's default), a kernel that
globally disables the local APIC never receives the 8259 output: `info pic` showed IRQ0/IRQ1
pending (`irr=03`) while the CPU halted with `IF=1`, so the scheduler tick never fired and the
demo threads stopped at `iteration=0`. QEMU 10.1 delivered the same image correctly. The
kernel now configures the architectural virtual-wire path instead (APIC enabled, `LVT0` =
ExtINT, `TPR` = 0) through a dedicated uncached mapping, which works on both emulator versions.
Rationale and alternatives: `docs/decisions/0005-lapic-extint-interrupt-delivery.md`.

Local boot matrix result (`tools/qemu-smoke.sh`, full marker set, `PASS` in every cell):

| Firmware | CPU | QEMU 8.2.2 | QEMU 10.1.0 |
|---|---|---|---|
| BIOS | qemu64 | PASS | PASS |
| BIOS | Haswell-v1 | PASS | PASS |
| UEFI/OVMF | qemu64 | PASS | PASS |
| UEFI/OVMF | Haswell-v1 | PASS | PASS |

This is emulator evidence for the serial marker set. It is not physical-hardware evidence and
not a claim about UEFI on real firmware beyond Limine's own boot path.

## Wave 2 — Core kernel fundamentals (planned, not started)

1. **User mode:** GDT ring-3 segments, TSS with a kernel stack, `iretq` entry,
   syscall/sysret, an ABI ADR (Ozero-native, not a Linux syscall table), loader for the
   native executable format, first init process.
2. **Per-process page tables from the PMM** (replace the static table pool), demand paging,
   page-fault handler with VMA tracking.
3. **SMP:** ACPI MADT parsing, LAPIC/IOAPIC, AP trampoline, per-CPU data, per-CPU runqueues,
   TLB shootdown, then PIT → APIC timer. The Wave 1 LAPIC ExtINT configuration is the
   starting point and is expected to be replaced here.
4. **Memory hardening:** SMEP/SMAP/NX/WP enforcement, KASLR, guard pages on kernel stacks,
   buddy allocator for the PMM, reclaiming bootloader memory
   (`o0_pmm_release_bootloader_memory`).

Dependencies: (2) needs the PMM interface stabilised; (3) needs MADT parsing and removes the
8259 path; everything in this wave needs the native ABI decision recorded as an ADR first.

## Wave 3 — Storage and filesystem (planned, not started)

1. virtio-blk first, then AHCI/NVMe with DMA and IRQ completion.
2. MBR/GPT partition discovery.
3. Replace rollback with real crash consistency (journaling or copy-on-write), as an ADR,
   tested with injected power loss at every write point.
4. VFS with file descriptors, a page cache and a second filesystem.

## Wave 4 — Platform services (planned, not started)

1. IPC model (choice recorded in an ADR), signals, futex-style primitives, timers.
2. PCI/MSI-X, ACPI power basics, device model with probe/remove.
3. Security model: capabilities or equivalent, syscall argument validation, copy-from/to-user
   helpers.

## Wave 5 — Quality engineering (planned, not started)

1. Fuzz syscalls, the filesystem image parser and the block layer (libFuzzer/AFL in host
   builds).
2. Concurrency testing: lockdep-lite, scheduler/mutex race stress, deterministic replay in
   QEMU.
3. Kernel logging levels, panic backtraces, symbolised oops, GDB-over-serial stub.
4. Performance baselines (context switch, allocator throughput, boot time) tracked in CI.

## Wave 6 — Return to graphics (deferred)

Resume Stage 5/6 only once user mode, SMP and a real storage driver exist. Then do the Haswell
engine MMIO, reset and interrupt work against real hardware, keeping the software-rendered
fallback as the default path. Until a verified reset sequence exists, engine reset stays
refused and the engine stays quarantined.

## Standing rules for every agent

- Every change needs a test and a documentation update. Every architectural choice needs an
  ADR with the alternatives considered.
- Evidence is reported by type in `docs/PROJECT_STATUS.md`; a boot or hardware box is never
  ticked on build or host-test evidence alone.
- Small, reviewable commits. `make check`, `make test-sanitize` and the analyzer pass before
  handoff.
- Anything that cannot be verified (for example unreset GPU engines) stays refused or
  quarantined, never assumed.

## Repository checks and what they cover

| Command | Covers | Does not cover |
|---|---|---|
| `make test` | Host unit suites: PMM, slab, scheduler, CPU, block, OzeroFS, PCI, GPU, display, input, PS/2, surface | Kernel execution, interrupts, firmware |
| `make test-sanitize` | The same suites under ASan/UBSan with leak detection | Kernel execution |
| `make check` | Doctor + `-Werror` freestanding kernel/ISO + host suites | Booting |
| `tools/qemu-smoke.sh` | BIOS or UEFI boot, full serial marker set, timer-driven scheduler, storage and GPU software self-tests | Physical hardware, GPU engines, user mode |
| `make test-sanitize` + `-fanalyzer` | Memory/UB defects and static analysis in host-buildable code | Hardware behaviour |
