# Ozero hardening roadmap (2026-11-24)

This document records the hardening directive accepted by the Project Director and tracks it
against the tree. It is a working plan, not a delivery promise: items are marked done only
with the evidence type named in the reporting standard (host test, emulator boot, physical
hardware — never inferred from one another).

**Rule 0 — new GPU/Stage 6 scope is paused.** Open Stage 6 phases are finished only where
they do not block the waves below. Stage 5/6 work resumes in Wave 7, after user mode, SMP
and a real storage driver exist. No Stage 6 phase is claimed complete by this document.

## Wave 1 — Close open verification debt

| # | Item | Status | Evidence |
|---:|---|---|---|
| 1 | Execute the authored-but-unrun Stage 4 regressions and tick the checklist | **Done** | Host tests: block layer 6/6, OzeroFS 10/10 (0 failures) in `make test` and again in `make test-sanitize`. The seven closing-fix regressions (late completion after timeout, driver-confirmed cancellation, transfer limits, checksummed malformed superblocks, lookup I/O propagation, partial-overwrite rollback, final inode persistence failure) all execute and pass. Checklist ticked in `docs/PROJECT_STATUS.md`. |
| 2 | Boot under OVMF (UEFI) and add smoke markers for it | **Done** | Kernel requests the Limine EFI system table and prints `OZERO: firmware=UEFI` / `OZERO: firmware=BIOS`. `tools/qemu-smoke.sh` gained `QEMU_FIRMWARE=uefi` (OVMF 4 MiB code + per-run variable store) and asserts the matching firmware marker. UEFI boots reach the full marker set, including the scheduler/storage/GPU self-tests. |
| 3 | Confirm GitHub Actions on the current branch, not just local runs | **Done** | Two distinct runs on `kilo/floral-eagle-2hb`, each with all five jobs (`boot` × 4 cells and `host-sanitizers`) and conclusion SUCCESS: [#132](https://github.com/Momegaii/Ozero/actions/runs/45462527735) on commit `d6626b4` and [#133](https://github.com/Momegaii/Ozero/actions/runs/45463475234) on commit `7e2c4bb`. They are separate runs with separate URLs; an earlier revision of this row could be read as giving them one link, which was wrong. Run #131 was also SUCCESS. The anonymous REST API is 403 from this sandbox, but an authenticated call with a token works: run #133's job logs were downloaded and inspected (see below). Run #132 was read from the Actions page only. |
| 4 | Add a CI matrix: `qemu64`, `Haswell-v1`, BIOS and UEFI, with ASan/UBSan host tests | **Done** | `.github/workflows/quality.yml` now has a 4-cell `kernel-and-boot` matrix (CPU × firmware) plus a `host-sanitizers` job running `make test-sanitize`. GitHub Actions run #133 exercised all five jobs successfully, and its logs show **QEMU 10.2.1** and **OVMF 2025.11-3ubuntu7.4** on `ubuntu-latest`. The same four cells also pass locally on QEMU 8.2.2, 10.0.13 and 10.1.0; those results are **local-only**. See `docs/decisions/0006-verification-matrix.md`. |

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

### Emulator and firmware versions: what CI runs versus what was local

| Environment | QEMU | Firmware package | Evidence type |
|---|---|---|---|
| GitHub Actions `ubuntu-latest` (runs #132, #133) | **10.2.1** (read from the #133 job logs) | **OVMF 2025.11-3ubuntu7.4** (same logs) | CI |
| This sandbox, local matrix | 8.2.2, 10.0.13, 10.1.0 | `ovmf` 2024.02-2 locally, 2025.11 in CI | Local only |

Two consequences, both recorded rather than glossed over:

1. CI does **not** run QEMU 8.2, so the 8.2.2 column above is local-only. It is the version that
   exposed the Wave 1 interrupt-delivery defect, so it stays in the local matrix on purpose.
2. **Real UEFI firmware is an open box.** Only OVMF has been exercised, in one version locally
   and another in CI. Vendor firmware, Secure Boot and CSM interactions are untested and no
   result here may be read as evidence about them.

## Wave 2 — Core kernel fundamentals (verified in emulation)

**Rule 0 for this wave:** no work beyond Wave 2 until a single-CPU user mode is verified. GPU
(Stage 5) and Stage 6 stay paused. Evidence stays split into host, QEMU and physical, and none
is inferred from another.

| Sub-wave | Scope | Status | Evidence |
|---|---|---|---|
| 2.0 | Housekeeping from Wave 1 | **Done** | This document and `docs/PROJECT_STATUS.md`: run numbers and URLs separated and verified, CI-versus-local emulator versions recorded, real UEFI firmware kept as an open box, stale LAPIC note corrected, SMP plan references ADR 0005. Run [#134](https://github.com/Momegaii/Ozero/actions/runs/45475446441) on commit `91af9fa` exercised all five jobs successfully. Later documentation-only commits are covered by the same matrix and are not cited run by run. |
| 2.1 | Syscall and executable ABI decision | **Done** | [ADR 0007](decisions/0007-user-abi-syscall-and-executable.md) accepted by the Director's Wave 2 execution directive (2026-11-24) and implemented in the same change set. Alternatives, error model, register convention, address layout and ELF subset are recorded there; `tests/abi_test.c` asserts the frozen selector, `STAR`, `FMASK` and error-mapping values. |
| 2.2 | Per-process page tables from the PMM, user/kernel split, null guard, stack guard page, fault classification, NX/WP/W^X | **Done** | Host: `tests/sched_test.c` still passes with the new address-space switch hook; the existing address-space map/protect/unmap self-test runs at boot. Emulator: every Wave 2 boot prints `OZERO: page tables active`, maps only 4 KiB user leaves with W^X, and contains the kernel-jump fault. Physical: not exercised. |
| 2.3 | Ring 3: GDT ring-3 segments, TSS with `RSP0`, #DF IST, `iretq` entry, `syscall` stub with `SFMASK`, selector/`STAR` host test | **Done** | Host: `tests/abi_test.c` checks null descriptor, kernel/user access bytes, DPL, L bit, TSS base/limit, TSS and per-CPU offsets, `STAR` split and `FMASK = 0x700`. Emulator: `OZERO: gdt+tss installed`, then ring-3 `syscall` and `iretq` round trips in every matrix cell. |
| 2.4 | `o0_copy_from_user`/`o0_copy_to_user` as the only user-memory path, SMAP-ready (`STAC`/`CLAC` hooks), argument validation helper | **Done** | `kernel/src/usercopy.c` validates the window and every page's U/S and write bits, then copies in the fixup-protected assembly core. The `badptr` user program exits 0 only if a kernel address returns `-O0_UE_FAULT`; every emulator cell prints `OZERO: syscall bad pointer rejected`. `STAC`/`CLAC` are not emitted yet: the cores are the single place to add them. |
| 2.5 | Static ELF64 `PT_LOAD` loader (no interpreters, relocations, overlaps or oversized segments), init process, syscalls `exit`/`yield`/`debug_write`/`getpid` | **Done** | Host: `tests/elf_test.c` accepts the loader subset and rejects wrong class/machine/type, interpreters, dynamic/TLS/PHDR directives, W+X, misalignment, non-congruent `p_offset`/`p_vaddr`, overlaps, truncated and oversized input. Emulator: `OZERO: user entry OK` from the static init image, then `OZERO: syscall exit status=0`. |
| 2.6 | Negative tests: NULL dereference dies alone, privileged instruction / kernel jump / bad syscall pointer contained, ELF parser fuzz, process churn without PMM leaks | **Done** | Host: `tests/elf_test.c` fuzzes 4 000 mutated and 1 000 random inputs plus random truncations, asserting parser invariants on every accepted result. Emulator: three contained user faults (`error=0x4`, `0x0`, `0x15`) with the scheduler continuing, the `badptr` rejection above, and `OZERO: user churn OK processes=16` after a free-page delta of zero. |
| 2.7 | Boot markers `user entry OK`, `syscall exit status=0`, `user fault contained` on the full BIOS/UEFI × `qemu64`/`Haswell-v1` matrix plus the sanitizer job; STATUS evidence by type | **Done** | `tools/qemu-smoke.sh` now requires the Wave 2 markers. All four cells pass locally on QEMU 10.0.13 and in CI. `make test-sanitize` (ASan + UBSan, leaks on) passes all suites locally and in CI. GitHub Actions run [#136](https://github.com/Momegaii/Ozero/actions/runs/45502990041) on commit `32c9e79` completed SUCCESS with all five jobs; its four boot logs show `user entry OK`, `syscall exit status=0`, `syscall bad pointer rejected`, `user churn OK processes=16`, `wave2 self-test complete` and `QEMU smoke test: PASS` on QEMU 10.2.1 / OVMF 2025.11. Physical hardware has not been exercised. |

Wave 2 verification detail:

- **Host tests:** `make test` and `make test-sanitize` pass. New suites are `abi_test`
  (ABI constants, selectors, TSS/per-CPU offsets, error table, user window) and `elf_test`
  (accept/reject matrix, overlap, fuzz and truncation).
- **Emulator:** `{bios, uefi} × {qemu64, Haswell-v1}` on QEMU 10.0.13 locally, full marker
  set, `QEMU smoke test: PASS` in every cell. This is emulator evidence only.
- **CI:** run [#136](https://github.com/Momegaii/Ozero/actions/runs/45502990041) on commit
  `32c9e79` completed SUCCESS with all five jobs (`boot` × 4 and `host-sanitizers`). The four
  boot logs carry the Wave 2 markers and report QEMU 10.2.1 with OVMF 2025.11-3ubuntu7.4. The
  logs were downloaded and inspected, not just the Actions summary page.
- **Physical hardware:** never exercised. No user-mode result here is physical evidence.

Sign-off is for the emulator scope of Rule 0: a single-CPU user mode is verified in QEMU on
both firmwares and CPU models. Wave 3 started only after this sign-off.

### Entry criteria carried forward from Wave 2

These are gaps, not failures. They must be closed by the wave that owns them, and none may be
assumed closed because the single-CPU emulator run passed.

1. **SMP must exercise `swapgs` and the per-CPU block.** Wave 2 only reserves them
   (`O0_MAX_CPUS = 1`). The SMP wave must run with more than one CPU and show a user process
   surviving migration between CPUs before it claims the ABI is SMP-safe.
2. **SMEP/SMAP must exercise `STAC`/`CLAC`.** The usercopy cores are structured for it, but no
   `STAC`/`CLAC` instruction is emitted and no fault path has been exercised. Structure is not
   evidence; the hardening wave must enable SMAP and run the user-memory negative tests again.
3. **Physical silicon is still an open box.** Interrupt delivery, `SYSCALL`/`SYSRET`
   canonical-address behaviour and TSS/IST handling can differ from QEMU. `Haswell-v1` under
   QEMU is a CPU model, not real hardware. No Wave 2 result may be used as physical evidence.
4. **Loader fuzzing should become a time-boxed CI job.** `elf_test` has fixed cases plus a
   bounded in-process fuzz loop. A longer-running fuzz target (libFuzzer/AFL) in CI would give
   more confidence than the current fixed budget.
5. **Wave 2 review lands as its own diff.** The branch baseline is far ahead of `main`, so the
   review PR is based on a Wave 2 base branch rather than `main`. This keeps the Wave 2 diff
   separate from earlier stages.

Deferred until Wave 2 is verified: SMP, SMEP/SMAP, KASLR, signals, IPC, `mmap`, dynamic linking
and `fork`.

Notes carried into the wave:

- **SMP** (deferred) starts from the Wave 1 LAPIC ExtINT configuration (ADR 0005) rather than
  from a "disable the LAPIC" state. AP bring-up does not remove that disable — there is none
  left in the tree — it has to program LVT/TPR per CPU, bring up the AP trampoline and migrate
  the PIT to the APIC timer while keeping the 8259 path only as a fallback.
- The **ABI** must reserve per-CPU data and `swapgs` now (ADR 0007 §2.8) so SMP cannot force an
  ABI change later.

## Wave 3 — Capability handles, budgets and synchronous IPC (verified in emulation)

Scope: single CPU, emulator-verified only. Physical hardware is deferred. The
design is [ADR 0008](decisions/0008-capability-handles-budgets-and-ipc.md).

| Sub-wave | Scope | Status | Evidence |
|---|---|---|---|
| 3.1 | Refcounted object header, per-process handle tables, generation-tagged handle values, per-handle rights | **Done** | Host: `tests/cap_test.c` covers lookup, stale generations, rights rejection, duplicate/close and table teardown. Emulator: `OZERO: handle negative tests OK`. |
| 3.2 | Hierarchical budgets, charge/uncharge, split, exhaustion and accounting return to baseline | **Done** | Host: `cap_test` charges objects, pages and a deliberately tight budget; a partially charged allocation must unwind to `used == 0`. Emulator: `OZERO: budget exhaustion OK`. |
| 3.3 | Memory objects with budget-charged pages, read/map policy and W^X checks | **Done** | Host: `cap_test` creates, reads, validates and destroys memory objects, including the partial-failure path. Emulator: the `handles` program exercises `memory_create`, `memory_map` and refusal of foreign-budget mappings. |
| 3.4 | Endpoints, synchronous send/receive/call, handle transfer and peer-death wakeups | **Done** | Host: `cap_test` covers endpoint creation and destruction. Emulator: `OZERO: ipc pair OK pair-1`, `OZERO: ipc negative tests OK`, and the `peer-server` test. |
| 3.5 | `process_spawn` syscall, grant installation and detached child reaping | **Done** | Emulator: `OZERO: spawn syscall OK`, `OZERO: wave3 done spawner status=0`. |
| 3.6 | Negative tests: forged/stale handles, rights escalation, foreign budget, bad IPC pointers, dead peers, budget exhaustion and process churn | **Done** | Host: `cap_test` rejects forged handles and rights escalation. Emulator: `handles`, `budget`, `ipcneg` and eight churn pairs; `OZERO: wave3 churn OK pairs=8` requires object count, budget used and PMM free pages to return to baseline. |
| 3.7 | Verification on the full BIOS/UEFI × `qemu64`/`Haswell-v1` matrix plus the sanitizer job; STATUS evidence by type | **Done** | All four cells pass locally on QEMU 10.0.13; `make test-sanitize` passes with leaks enabled. GitHub Actions run [#138](https://github.com/Momegaii/Ozero/actions/runs/45587151198) on commit `521bc04` completed SUCCESS with all five jobs on QEMU 10.2.1; the downloaded job logs contain the Wave 3 markers. `tools/qemu-smoke.sh` requires those markers. Physical hardware has not been exercised. |

Wave 3 verification detail:

- **Host tests:** `make test` and `make test-sanitize` pass, including `cap_test`.
- **Emulator:** `{bios, uefi} × {qemu64, Haswell-v1}` on QEMU 10.0.13 with the full
  Wave 2 and Wave 3 marker sets and `QEMU smoke test: PASS` in every cell.
- **CI:** run [#138](https://github.com/Momegaii/Ozero/actions/runs/45587151198) on commit
  `521bc04` completed SUCCESS with all five jobs (`boot` × 4 and `host-sanitizers`). The job
  logs were downloaded and inspected: they contain the Wave 3 markers and report QEMU 10.2.1
  with OVMF. The Actions page and API showed the same job conclusions.
- **Physical hardware:** never exercised. No Wave 3 result is physical evidence.
- **Defects found and fixed while closing the wave:** a partially charged memory object
  uncharged only the pages it had allocated, not the full page array (24-byte leak per
  failed create); `o0_ipc_call` took an extra reference on its reply endpoint and leaked one
  endpoint per call. Both are covered by the host partial-failure test and the emulator
  churn check respectively.

The wave is signed off for its emulator scope only. SMP, preemption, SMEP/SMAP, KASLR,
signals, asynchronous IPC, `mmap`, dynamic linking and `fork` remain deferred.

## Wave 4 — Storage and filesystem (planned, not started)

1. virtio-blk first, then AHCI/NVMe with DMA and IRQ completion.
2. MBR/GPT partition discovery.
3. Replace rollback with real crash consistency (journaling or copy-on-write), as an ADR,
   tested with injected power loss at every write point.
4. VFS with file descriptors, a page cache and a second filesystem.

## Wave 5 — Platform services (planned, not started)

1. Signals, futex-style primitives and timers.
2. PCI/MSI-X, ACPI power basics, device model with probe/remove.
3. Remaining security hardening: SMEP/SMAP, KASLR and user-memory fault paths under SMAP.

## Wave 6 — Quality engineering (planned, not started)

1. Fuzz syscalls, the filesystem image parser, the block layer and the ELF loader
   (libFuzzer/AFL in host builds), with a time-boxed CI job rather than fixed cases only.
2. Concurrency testing: lockdep-lite, scheduler/mutex race stress, deterministic replay in
   QEMU.
3. Kernel logging levels, panic backtraces, symbolised oops, GDB-over-serial stub.
4. Performance baselines (context switch, allocator throughput, boot time) tracked in CI.

## Wave 7 — Return to graphics (deferred)

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
| `make test` | Host unit suites: PMM, slab, scheduler, CPU, ABI, ELF, capability handles/budgets/IPC, block, OzeroFS, PCI, GPU, display, input, PS/2, surface | Kernel execution, interrupts, firmware |
| `make test-sanitize` | The same suites under ASan/UBSan with leak detection | Kernel execution |
| `make check` | Doctor + `-Werror` freestanding kernel/ISO + host suites | Booting |
| `tools/qemu-smoke.sh` | BIOS or UEFI boot, full serial marker set, timer-driven scheduler, storage/GPU software self-tests, Wave 2 user mode and Wave 3 capabilities/IPC | Physical hardware, GPU engines, SMP, SMEP/SMAP |
| `make test-sanitize` + `-fanalyzer` | Memory/UB defects and static analysis in host-buildable code | Hardware behaviour |
