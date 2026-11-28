# Ozero hardening roadmap (2026-11-27)

This document records the hardening directive accepted by the Project Director and tracks it
against the tree. It is a working plan, not a delivery promise: items are marked done only
with the evidence type named in the reporting standard (host test, emulator boot, physical
hardware — never inferred from one another).

## Repository baseline

- Branch: `main`, synced with `origin/main` at `a478b385` ("Merge origin/main: P1 compositor focus-border regression fix").
- Commit line of record: `e904fe7` (P0 baseline) → `4f641e8` (P1 input acceptance) → `49585b2` (P2 Stage 6 acceptance) → `173e0f3` (P1 focus-border fix) → `a478b385` (merge).
- Wave 1 (verification debt) and Wave 2 (single-CPU user mode) are complete and merged into `main`.
- Wave 3 (SMP and memory hardening) is implemented in source and emulator-verified locally.
- Wave 5.0 (capability handles, budgets, synchronous IPC) is implemented and emulator-verified.
- Stage 6 (native graphical desktop) is implemented, host-tested, and QEMU BIOS + UEFI verified.

**Rule 0 — evidence stays split by type.** Host test results, emulator boot results, and
physical-hardware results are never inferred from one another. A build or host-test pass is
not boot evidence; a QEMU pass is not physical evidence. No physical hardware has been
exercised for any stage or wave.

## Wave 1 — Close open verification debt (complete locally)

| # | Item | Status | Evidence |
|---|------|--------|----------|
| 1 | Execute the authored-but-unrun Stage 4 regressions and tick the checklist | **Done** | Host tests: block layer 6/6, OzeroFS 10/10 (0 failures) in `make test` and `make test-sanitize`. The closing-fix regressions (late completion after timeout, driver-confirmed cancellation, transfer limits, checksummed malformed superblocks, lookup I/O propagation, partial-overwrite rollback, final inode persistence failure) all execute and pass. |
| 2 | Boot under OVMF (UEFI) and add smoke markers for it | **Done** | Kernel requests the Limine EFI system table and prints `OZERO: firmware=UEFI` / `OZERO: firmware=BIOS`. `tools/qemu-smoke.sh` supports `QEMU_FIRMWARE=uefi` (OVMF 4 MiB code + per-run variable store) and asserts the matching firmware marker. UEFI boots reach the full marker set including scheduler, storage, GPU self-tests. |
| 3 | Add a CI matrix: `qemu64`, `Haswell-v1`, BIOS and UEFI, with ASan/UBSan host tests | **Done (documented)** | CI configuration references a 4-cell `kernel-and-boot` matrix (CPU × firmware) plus a `host-sanitizers` job. This workspace's local QEMU matrix passes on BIOS and UEFI with `qemu64` and `Haswell-v1` at `-smp 1`; QEMU 8.2.2, 10.0.13 and 10.1.0 were exercised locally. CI run numbers and job logs from parallel branches are not recorded here. |
| 4 | Record local boot matrix results | **Done** | `tools/qemu-smoke.sh` full marker set, `PASS` in every cell: BIOS/UEFI × qemu64/Haswell-v1 on QEMU 8.2.2, 10.0.13, 10.1.0. Emulator evidence only, not physical. |

Wave 1 also produced one kernel fix. On QEMU 8.2, a kernel that globally disables the local
APIC never receives the 8259 output: `info pic` showed IRQ0/IRQ1 pending while the CPU halted
with `IF=1`, so the scheduler tick never fired. The kernel now configures the architectural
virtual-wire path instead (APIC enabled, `LVT0` = ExtINT, `TPR` = 0) through a dedicated
uncached mapping, which works on both emulator versions. Rationale and alternatives:
`docs/decisions/0005-lapic-extint-interrupt-delivery.md`.

## Wave 2 — single-CPU user mode (implemented and emulator-verified)

ADR 0007 accepted; per-process page tables from the PMM, ring 3 with TSS/IST, syscall ABI,
usercopy, static ELF64 loader, negative tests, process churn. All four `{bios, uefi} ×
{qemu64, Haswell-v1}` cells pass locally on QEMU 10.0.13. No physical-hardware evidence.

Sub-waves 2.0–2.7 are implemented:
- Syscall and executable ABI (`tests/abi_test.c`, `tests/elf_test.c`)
- Per-process page tables from the PMM, null guard, stack guard page, W^X
- Ring 3: GDT ring-3 segments, TSS with `RSP0`, `#DF` IST, `iretq` entry, `syscall` stub
- `o0_copy_from_user`/`o0_copy_to_user` as the only user-memory path, SMAP-ready
- Static ELF64 `PT_LOAD` loader, init process, syscalls `exit`/`yield`/`debug_write`/`getpid`
- Negative tests: NULL dereference dies alone, privileged instruction/kernel jump/bad syscall pointer contained, ELF parser fuzz, process churn without PMM leaks
- Boot markers `user entry OK`, `syscall exit status=0`, `user fault contained` on the full BIOS/UEFI × `qemu64`/`Haswell-v1` matrix plus the sanitizer job

Entry criteria carried forward from Wave 2 (gaps, not failures):
1. SMP must exercise `swapgs` and the per-CPU block before claiming ABI SMP-safety.
2. SMEP/SMAP must exercise `STAC`/`CLAC`; the usercopy cores are structured for it but no `STAC`/`CLAC` instruction is emitted yet.
3. Physical silicon is still an open box.
4. Loader fuzzing should become a time-boxed CI job.

Deferred until Wave 2 was verified: SMP and SMEP/SMAP (now Wave 3), IPC (Wave 5.0). Still
deferred: KASLR (kernel-stack randomization instead), signals, `mmap`, dynamic linking, `fork`.

## Wave Q — evidence capture and framebuffer verification

| Item | Scope | Status | Evidence |
|------|-------|--------|----------|
| Q.1 | Director-observed evidence rows | **Reserved, not fabricated** | Rows exist for Director-observed cells; none reported yet. |
| Q.2 | Environment findings | **Done** | QEMU version and HAXM findings recorded without turning them into Ozero defects. |
| Q.3 | Self-describing boot header | **Done** | First kernel lines: `OZERO: build commit=`, `OZERO: cpu vendor=…`, `OZERO: platform=…`, `firmware=…`. Commit compiled in from `git rev-parse --short=12 HEAD`. |
| Q.4 | Pending Director cells | **Open** | UEFI/`Haswell-v1` and HAXM/`Haswell-v1` not run by the Director. |
| Q.8.1 | Sampled-pixel/checksum marker for the eight bars | **Done** | Kernel prints `framebuffer bars=8`, per-bar sample lines and `framebuffer checksum=…`. Host `display_test` asserts reference pixels and checksum. |
| Q.8.2 | CI screendump comparison against the eight-bar layout | **Done (locally)** | `tools/framebuffer-check.py` boots QEMU, takes a QMP screendump, validates all eight bar centres at three heights, checks margins, and compares the FNV checksum with the kernel marker. `make check` runs it when QEMU is installed. CI run numbers from parallel branches are not recorded here. |

**P.0 physical boot gate:** recorded in `docs/PROJECT_STATUS.md`. Not scheduled here; no
agent may boot physical hardware on the strength of emulator evidence.

## Wave 3 — SMP and memory hardening (implemented in source, emulator-verified)

| Sub-wave | Scope | Status | Evidence |
|--------|-------|--------|----------|
| 3.1 | ACPI RSDP/RSDT/XSDT/MADT discovery, checksums, CPU/IOAPIC/ISO records | **Done** | Host: `tests/acpi_test.c`. QEMU: `OZERO: acpi cpus=… ioapics=… overrides=… lapic=… bsp_apic=…` on every cell. |
| 3.2 | LAPIC xAPIC MMIO setup, EOI, fixed-delivery IPI | **Done** | APs online with xAPIC; TLB shootdown uses the IPI path. x2APIC deliberately not assumed. |
| 3.3 | IOAPIC redirection programming and GSI routing | **Done** | `kernel/src/ioapic.c` maps each MADT I/O APIC, masks every RTE, resolves ISA overrides, routes to BSP; 8259 masked. Host: `tests/ioapic_test.c`. QEMU: `ioapic=active gsis=24`. |
| 3.4 | AP release and entry | **Done** | Limine MP releases each AP; `kernel/arch/x86_64/ap_entry.S` switches to a kernel-owned stack; `kernel/src/smp.c` owns setup. QEMU: `smp cpus=4 online=4`. |
| 3.5 | Per-CPU data, GS base, online accounting | **Done** | `O0_MAX_CPUS` is 4; each AP installs its own `o0_cpu_local`, APIC ID and stack top. QEMU: online count equals `-smp`. |
| 3.6 | Per-CPU runqueues and AP scheduling | **Done for kernel threads** | Each CPU owns a runqueue and idle thread; LAPIC timer preempts its own queue; `o0_thread_migrate` moves ready/running kernel threads. User threads stay BSP-pinned until per-CPU TSS/IST exists. |
| 3.7 | TLB shootdown | **Done (page, range, full)** | Vector `0x40` carries a range descriptor; sender waits for every online target with bounded spin and panics on timeout. QEMU `-smp 2/4`: `ipi_seen=1/3`, `tlb shootdown OK` variants. |
| 3.8 | APIC timer | **Done (BSP and AP)** | Calibrated against PIT ticks, divisor 16, periodic vector `0x41`; PIT stopped and IRQ0 masked after success. Every AP starts its own timer. QEMU: `timer=lapic hz=100`. |
| 3.9 | SMEP/SMAP | **Done where advertised** | `Haswell-v1` reports `smep=on smap=off`; `-cpu max` reports `smep=on smap=on`. `STAC`/`CLAC` usercopy cores run; Wave 2/3 negative tests pass. |
| 3.10 | Guard pages | **Done** | Null guard, kernel-stack guard, user-stack guard remain; no new claim. |
| 3.11 | Kernel-stack randomization (KSR) | **Done (stack window only)** | `o0_kaslr_init()` derives one 2 MiB-aligned offset for the kernel-stack window. Entropy: `RDRAND` (up to 16 attempts) XOR `RDTSC`; without it, `RDTSC` XOR data-address shift, then MurmurHash3 finalizer and `% 512`. Not KASLR — the kernel image and HHDM do not move. QEMU: `kaslr layout offset=0x...`; host: `tests/kaslr_test.c`. |
| 3.12 | Buddy allocator | **Done (bitmap with buddy alignment)** | Bitmap stays authoritative; order-`n` allocation requires natural alignment; free walks the buddy tree upward while the pair is wholly free. `tests/pmm_test.c` covers alignment, coalescing, 2 000-iteration churn. |
| 3.13 | Bootloader-memory reclaim | **Done** | `o0_pmm_release_bootloader_memory()` releases Limine reclaimable regions after boot info, ACPI data and SMP response are copied. QEMU: `bootloader reclaim bytes=N`; host: PMM reclaim test. ACPI reclaimable is not released. |
| 3.14 | Lock-order checker | **Done (default in host tests and one CI cell)** | Ordered locks acquire in strictly increasing class and release in reverse. Host tests build with `O0_LOCK_ORDER_CHECK`; IRQ-safe runqueue locks stay class 0. Host: `tests/lock_order_test.c`; QEMU marker: `lock order check=on`. |

Wave 3 verification detail:
- **Host:** `make test` includes ACPI parser, PMM churn baseline, GPU discovery no-MMIO-write canary. `make test-sanitize` runs the same under ASan/UBSan.
- **QEMU:** local QEMU 10.0.13, BIOS and UEFI, `qemu64`, `Haswell-v1` and `max`, at `-smp 1`, `-smp 2` and `-smp 4`. Emulator evidence only; no result inferred from another cell.
- **Physical:** never exercised. No Wave 3 result is physical evidence.

ADRs: [0009](decisions/0009-smp-bringup-and-local-apic-timer.md), [0010](decisions/0010-memory-hardening-smep-smap.md), [0011](decisions/0011-wave3-ioapic-percpu-kaslr-pmm.md). All accepted-provisional for review.

## Wave 3.5 — Verification expansion

Items covered by the Wave 3 sign-off scope:
- CI `-smp 2` and `-cpu max` cells: active locally; CI expansion pending token scope.
- Final-commit log inspection: conclusions recorded locally; job logs require repository admin (`403`).
- Stack-randomization rename and entropy: documented as kernel-stack randomization (KSR), not KASLR.
- Lock-order checker by default: host tests compile with `O0_LOCK_ORDER_CHECK`; local BIOS/`qemu64` `-smp 1` and `-smp 2` boots pass with `lock order check=on`.
- Director-observed rows: reserved, not fabricated.
- `-smp 2` migration + user churn: done locally; `wave3_smp_stress()` runs 16 user churn processes on the BSP while one kernel worker per online CPU migrates across CPUs. PMM free-byte count unchanged.

## Wave 4 — Storage and filesystem

- Wave 4.1 — PCI, MSI/MSI-X and DMA foundations: **implemented.** Capability-list parsing, BAR size from all-ones mask, MSI/MSI-X programming and LAPIC vector dispatch, DMA allocator with pinning, host tests. No device driver consumes it yet.
- Waves 4.2/4.3 — PCI storage drivers: **implemented and build-verified.** Legacy virtio-blk, AHCI (one port with command list/FIS/PRDT/Identify Device), NVMe (one admin plus one I/O queue pair with MSI-X entries). Each has an injected past-end-of-medium read proof. `make kernel` passes with `-Werror` and `tests/storage_encode_test.c` passes. **No QEMU storage cell has been observed green** — not evidence until a run is recorded.
- Waves 4.4–4.7 — **partial.** Crash consistency per ADR 0012/0018 is implemented: redo journal (version 2) with commit header, ordered data, recovery replay and `o0fs_check`; host evidence `tests/o0fs_journal_test` 5/5 and `tests/fs_test` 10/10 pass (crash at commit header not committed; crash during live application replayed; consistency checker clean). MBR/GPT partition discovery and VFS with file descriptors and page cache remain open.

## Wave 5 — Platform services and security

- Wave 5.0 — capability handles, budgets and synchronous IPC: **implemented and emulator-verified.** `kernel/src/object.c`, `kernel/src/budget.c`, `kernel/src/handle.c`, `kernel/src/memory.c`, `kernel/src/endpoint.c`, `kernel/src/ipc.c`. Host test `tests/cap_test.c` passes under `make test` and `make test-sanitize`. Emulator boots print capability/IPC markers. Single-CPU and emulator-only.
- Remaining Wave 5 scope: IPC extensions, futex-style primitives, timers, signals; PCI/MSI-X device probe/remove; syscall-argument audit; `mmap` and `spawn` only after their own ADRs.

## Wave 6 — Quality engineering (not started)

Fuzzing, concurrency testing, deterministic replay, logging/backtraces, performance baselines.

## Wave 7 — Graphics (resumed)

Stage 6 is implemented and is not gated behind Wave 7. Stage 5 GPU hardware work
(Haswell engine MMIO, reset, interrupt) remains deferred until the Director authorizes
real-hardware testing. The software renderer remains the default path. No GPU hardware claim
is made; engine reset stays refused and the engine stays quarantined until a verified reset
sequence exists.

## Standing rules for every agent

- Every change needs a test and a documentation update. Every architectural choice needs an
  ADR with the alternatives considered.
- Evidence is reported by type in `docs/PROJECT_STATUS.md`; a boot or hardware box is never
  ticked on build or host-test evidence alone.
- Small, reviewable commits. `make check`, `make test-sanitize` and the analyzer pass before
  handoff.
- Anything that cannot be verified stays refused or quarantined, never assumed.

## Repository checks and what they cover

| Command | Covers | Does not cover |
|---------|--------|----------------|
| `make test` | Host unit suites: ACPI/MADT, PMM (including churn), slab, scheduler, CPU, ABI, ELF, usercopy, capability handles/budgets/IPC, block, OzeroFS, PCI, GPU (including discovery no-MMIO canary), display, input, PS/2, surface | Kernel execution, interrupts, firmware, SMP |
| `make test-sanitize` | The same suites under ASan/UBSan with leak detection | Kernel execution |
| `make check` | Doctor + `-Werror` freestanding kernel/ISO + host suites | Booting |
| `tools/qemu-smoke.sh` | BIOS or UEFI boot, full serial marker set, timer-driven scheduler, storage/GPU software self-tests, Wave 2 user mode, capability/IPC, AP bring-up and TLB shootdown when `QEMU_SMP>1`, SMEP/SMAP when the CPU model advertises them | Physical hardware, GPU engines, AP scheduling (BSP-only scheduler) |
| `make test-sanitize` + `-fanalyzer` | Memory/UB defects and static analysis in host-buildable code | Hardware behaviour |