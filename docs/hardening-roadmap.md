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

## Wave 2 — Core kernel fundamentals (in progress)

**Rule 0 for this wave:** no Wave 3+ work until a single-CPU user mode is verified. GPU
(Stage 5) and Stage 6 stay paused. Evidence stays split into host, QEMU and physical, and none
is inferred from another.

| Sub-wave | Scope | Status | Evidence |
|---|---|---|---|
| 2.0 | Housekeeping from Wave 1 | **Done** | This document and `docs/PROJECT_STATUS.md`: run numbers and URLs separated and verified, CI-versus-local emulator versions recorded, real UEFI firmware kept as an open box, stale LAPIC note corrected, SMP plan references ADR 0005. |
| 2.1 | Syscall and executable ABI decision | **Proposed — awaiting Director approval** | [ADR 0007](decisions/0007-user-abi-syscall-and-executable.md) with alternatives, open questions and a verification plan. No code lands before approval. |
| 2.2 | Per-process page tables from the PMM, user/kernel split, null guard, stack guard page, fault classification, NX/WP/W^X | Not started | Blocked on 2.1 |
| 2.3 | Ring 3: GDT ring-3 segments, TSS with `RSP0`, #DF IST, `iretq` entry, `syscall` stub with `SFMASK`, selector/`STAR` host test | Not started | Blocked on 2.1 |
| 2.4 | `o0_copy_from_user`/`o0_copy_to_user` as the only user-memory path, SMAP-ready (`STAC`/`CLAC` hooks), argument validation helper | Not started | Blocked on 2.1 |
| 2.5 | Static ELF64 `PT_LOAD` loader (no interpreters, relocations, overlaps or oversized segments), init process, syscalls `exit`/`yield`/`debug_write`/`getpid` | Not started | Blocked on 2.1 |
| 2.6 | Negative tests: NULL dereference dies alone, privileged instruction / kernel jump / bad syscall pointer contained, ELF parser fuzz, process churn without PMM leaks | Not started | Required for sign-off |
| 2.7 | Boot markers `user entry OK`, `syscall exit status=0`, `user fault contained` on the full BIOS/UEFI × `qemu64`/`Haswell-v1` matrix plus the sanitizer job; STATUS evidence by type | Not started | — |

Deferred until Wave 2 is verified: SMP, SMEP/SMAP, KASLR, signals, IPC, `mmap`, dynamic linking
and `fork`.

Notes carried into the wave:

- **SMP** (deferred) starts from the Wave 1 LAPIC ExtINT configuration (ADR 0005) rather than
  from a "disable the LAPIC" state. AP bring-up does not remove that disable — there is none
  left in the tree — it has to program LVT/TPR per CPU, bring up the AP trampoline and migrate
  the PIT to the APIC timer while keeping the 8259 path only as a fallback.
- The **ABI** must reserve per-CPU data and `swapgs` now (ADR 0007 §2.8) so SMP cannot force an
  ABI change later.

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
