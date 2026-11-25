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
| 2.4 | `o0_copy_from_user`/`o0_copy_to_user` as the only user-memory path, SMAP-ready (`STAC`/`CLAC` hooks), argument validation helper | **Done** | `kernel/src/usercopy.c` validates the window and every page's U/S and write bits, then copies in the fixup-protected assembly core. Host: `tests/usercopy_test.c` covers the null guard, kernel half, unmapped page, read-only write, range overflow, zero length, a faulting raw core and the fixup-label lookup. The `badptr` user program exits 0 only if a kernel address returns `-O0_UE_FAULT`; every emulator cell prints `OZERO: syscall bad pointer rejected`. `STAC`/`CLAC` are not emitted yet: the cores are the single place to add them. |
| 2.5 | Static ELF64 `PT_LOAD` loader (no interpreters, relocations, overlaps or oversized segments), init process, syscalls `exit`/`yield`/`debug_write`/`getpid` | **Done** | Host: `tests/elf_test.c` accepts the loader subset and rejects wrong class/machine/type, interpreters, dynamic/TLS/PHDR directives, W+X, misalignment, non-congruent `p_offset`/`p_vaddr`, overlaps, truncated and oversized input. Emulator: `OZERO: user entry OK` from the static init image, then `OZERO: syscall exit status=0`. |
| 2.6 | Negative tests: NULL dereference dies alone, privileged instruction / kernel jump / bad syscall pointer contained, ELF parser fuzz, process churn without PMM leaks | **Done** | Host: `tests/elf_test.c` fuzzes 4 000 mutated and 1 000 random inputs plus random truncations, asserting parser invariants on every accepted result. Emulator: three contained user faults (`error=0x4`, `0x0`, `0x15`) with the scheduler continuing, the `badptr` rejection above, and `OZERO: user churn OK processes=16` after an exact `o0_pmm_free_bytes()` delta of zero. |
| 2.7 | Boot markers `user entry OK`, `syscall exit status=0`, `user fault contained` on the full BIOS/UEFI × `qemu64`/`Haswell-v1` matrix plus the sanitizer job; STATUS evidence by type | **Done** | `tools/qemu-smoke.sh` requires the Wave 2 markers, checks the worker lines as an unordered set and strips ANSI CSI/OSC sequences from UEFI logs before matching. The marker-to-assertion map is in `docs/PROJECT_STATUS.md`. All four cells pass locally on QEMU 10.0.13 and in CI. `make test-sanitize` (ASan + UBSan, leaks on) passes all suites locally and in CI. The fault line now carries `vector=` and process status is printed signed. GitHub Actions run [#136](https://github.com/Momegaii/Ozero/actions/runs/45502990041) on commit `32c9e79` completed SUCCESS with all five jobs; the current commit's run is recorded in STATUS after inspection. Physical hardware has not been exercised. |

Wave 2 verification detail:

- **Host tests:** `make test` and `make test-sanitize` pass. New suites are `abi_test`
  (ABI constants, selectors, TSS/per-CPU offsets, error table, user window) and `elf_test`
  (accept/reject matrix, overlap, fuzz and truncation).
- **Emulator:** `{bios, uefi} × {qemu64, Haswell-v1}` on QEMU 10.0.13 locally, full marker
  set, `QEMU smoke test: PASS` in every cell. Wave 3.5 adds `max` and `-smp 2` cells and a
  lock-order-enabled boot. This is emulator evidence only.
- **CI:** run [#136](https://github.com/Momegaii/Ozero/actions/runs/45502990041) on commit
  `32c9e79` completed SUCCESS with all five jobs (`boot` × 4 and `host-sanitizers`). The four
  boot logs carry the Wave 2 markers and report QEMU 10.2.1 with OVMF 2025.11-3ubuntu7.4. The
  logs were downloaded and inspected, not just the Actions summary page.
- **Physical hardware:** never exercised. No user-mode result here is physical evidence.

Sign-off is for the emulator scope of Rule 0: a single-CPU user mode is verified in QEMU on
both firmwares and CPU models. Wave 3 SMP work started only after this sign-off.

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

Deferred until Wave 2 was verified, and now partly delivered: SMP and SMEP/SMAP in Wave 3, and
IPC in Wave 5.0. Still deferred: KASLR, signals, `mmap`, dynamic linking and `fork`.

Notes carried into the wave:

- **SMP** (now Wave 3) starts from the Wave 1 LAPIC ExtINT configuration (ADR 0005) rather than
  from a "disable the LAPIC" state. There is no LAPIC disable left in the tree to remove; AP
  bring-up programs LVT/TPR per CPU, uses the Limine MP handoff for the AP entry, and migrates
  the scheduler timer from the PIT to the LAPIC timer while keeping the 8259 path as a fallback.
- The **ABI** must reserve per-CPU data and `swapgs` now (ADR 0007 §2.8) so SMP cannot force an
  ABI change later.

## Wave Q — evidence capture, self-description and framebuffer verification (2026-11-25)

| Item | Scope | Status | Evidence |
|---|---|---|---|
| Q.1 | Record the Director's Windows 8.1 / i5-4460 / `q35` cells separately from CI | **Done** | `docs/PROJECT_STATUS.md` has a **Director-observed** table: QEMU 8.1.0 TCG (BIOS SDL, BIOS GTK, BIOS `Haswell-v1`, UEFI edk2 `qemu64`) and QEMU 7.2.0 + HAXM 7.8.0 (BIOS `qemu64`). Commit is recorded as unknown for the historical logs. |
| Q.2 | Record environment findings without turning them into Ozero defects | **Done** | STATUS records the QEMU 8.1.0 HAX pre-guest abort and the window-scaling margins. The raw 1280×800 screendump is eight 160 px bars with no margins. |
| Q.3 | Self-describing boot header | **Done** | The first kernel lines are `OZERO: build commit=`, `OZERO: cpu vendor=… family=… model=… brand=…` and `OZERO: platform=… hypervisor=… signature=… firmware=…`. The commit is compiled in from `git rev-parse --short=12 HEAD`. |
| Q.4 | Pending Director cells | **Open** | UEFI/`Haswell-v1` and HAXM/`Haswell-v1` have not been run by the Director. |
| Q.8.1 | Sampled-pixel/checksum marker for the eight bars | **Done** | The kernel prints `framebuffer bars=8`, eight `framebuffer sample bar=N pixel=… expected=…` lines and `framebuffer checksum=… expected=…`. Host `display_test` asserts the reference pixels and checksum. |
| Q.8.2 | CI screendump comparison against the eight-bar layout | **Done** | `tools/framebuffer-check.py` boots QEMU, takes a QMP `screendump`, validates all eight bar centres at three heights, checks both edges for margins, and compares the FNV checksum with the kernel marker. `make check` runs it when QEMU is installed, so the `kernel-and-boot` CI job exercises it. Run [#143](https://github.com/Momegaii/Ozero/actions/runs/45604746811) shows `framebuffer-check: PASS` in all four cells. The serial log matches the existing `build/qemu-serial-*.log` artifact glob; the PPM remains in the job workspace. |

**P.0 physical boot gate:** recorded in `docs/PROJECT_STATUS.md`. It is not scheduled here and
no agent may boot the physical i5-4460 on the strength of emulator evidence. Items 1 and 5 of
that gate remain open.

## Wave 3 — SMP and memory hardening (implemented, emulator-verified, not signed off)

The Director's 2026-11-25 order puts SMP and memory hardening here. APs are
online, the I/O APIC carries ISA interrupts, per-CPU runqueues schedule kernel
threads, range TLB shootdown works, kernel-stack randomization (KSR) is active, the PMM honours
buddy alignment and bootloader memory is reclaimed. User threads still do not
run on APs because the TSS/IST layout is shared. Wave 4 work has not started.

| Sub-wave | Scope | Status | Evidence |
|---|---|---|---|
| 3.1 | ACPI RSDP/RSDT/XSDT/MADT discovery, checksums, CPU/IOAPIC/ISO records | **Done** | Host: `tests/acpi_test.c` accepts a synthetic XSDT/MADT and rejects bad signatures, checksums, truncated tables and non-whole entries. QEMU: `OZERO: acpi cpus=… ioapics=… overrides=… lapic=… bsp_apic=…` on every cell. |
| 3.2 | LAPIC xAPIC MMIO setup, EOI, fixed-delivery IPI | **Done** | QEMU: APs online with xAPIC; `tlb shootdown OK` uses the IPI path. x2APIC is deliberately not assumed. |
| 3.3 | IOAPIC redirection programming and GSI routing | **Done (emulator)** | `kernel/src/ioapic.c` maps each MADT I/O APIC, masks every RTE, resolves ISA overrides and routes to the BSP. QEMU: `ioapic=active gsis=24`; the 8259 is masked. Host: `tests/ioapic_test.c`. |
| 3.4 | AP release and entry | **Done (bootloader handoff + Ozero entry)** | Limine MP releases each AP; `kernel/arch/x86_64/ap_entry.S` switches to a kernel-owned stack and `kernel/src/smp.c` owns the rest of the setup. QEMU: `smp cpus=4 online=4`. |
| 3.5 | Per-CPU data, GS base, online accounting | **Done** | `O0_MAX_CPUS` is 4; each AP installs its own `o0_cpu_local`, APIC ID and stack top. QEMU: online count equals `-smp`. |
| 3.6 | Per-CPU runqueues and AP scheduling | **Done for kernel threads** | Each CPU owns a runqueue and idle thread; the LAPIC timer preempts its own queue; `o0_thread_migrate` moves ready/running kernel threads. QEMU: `smp scheduling OK cpus=N threads=N iterations=...`. User threads stay BSP-pinned until per-CPU TSS/IST exists. |
| 3.7 | TLB shootdown | **Done (page, range, full)** | Vector `0x40` carries a range descriptor in the target's per-CPU block; the sender waits for every online target with a bounded spin and panics on timeout. QEMU `-smp 2/4`: `ipi_seen=1/3`, `tlb shootdown OK`, `tlb shootdown range OK`, `tlb shootdown full OK`. |
| 3.8 | APIC timer | **Done (BSP and AP)** | Calibrated against ten PIT ticks, divisor 16, periodic vector `0x41`; the PIT is stopped and IRQ0 masked only after success. Every AP starts its own timer. QEMU: `timer=lapic hz=100`, worker threads complete. PIT remains the fallback. |
| 3.9 | SMEP/SMAP | **Done where advertised; SMAP path verified on `-cpu max`** | `Haswell-v1` reports `smep=on smap=off` (the model has no SMAP). `-cpu max` reports `smep=on smap=on`; the `STAC`/`CLAC` usercopy cores run and the Wave 2/3 negative tests still pass. |
| 3.10 | Guard pages | **Done** | Existing null guard, kernel-stack guard and user-stack guard remain; no new claim. |
| 3.11 | Kernel-stack randomization (KSR) | **Done (stack window only)** | `o0_kaslr_init()` derives one 2 MiB-aligned offset for the kernel-stack window. **Entropy:** CPUID.01H:ECX bit 30 `RDRAND` (up to 16 attempts) XOR `RDTSC`; without it, `RDTSC` XOR `(address of layout_offset << 17)`, then a MurmurHash3 finalizer and `% 512`. `RDRAND` is a hardware DRBG; the TSC fallback is low-entropy and is not a cryptographic ASLR boundary. The kernel image and HHDM do not move; this is **not KASLR**. QEMU: `kaslr layout offset=0x...` (marker name retained); host: `tests/kaslr_test.c`. |
| 3.12 | Buddy allocator | **Done (bitmap with buddy alignment)** | The bitmap stays authoritative; order-`n` allocation requires natural alignment and free walks the buddy tree upward while the pair is wholly free. `tests/pmm_test.c` covers alignment, coalescing state and the 2 000-iteration churn baseline. |
| 3.13 | Bootloader-memory reclaim | **Done** | `o0_pmm_release_bootloader_memory()` releases Limine reclaimable regions after boot info, ACPI data and the SMP response are copied. QEMU: `bootloader reclaim bytes=N`; host: PMM reclaim test. ACPI reclaimable is not released. |
| 3.14 | Lock-order checker | **Done (default in host tests and one CI cell)** | Ordered locks must acquire in strictly increasing class and release in reverse. `O0_LOCK_ORDER_CHECK` gives every `o0_spin_init()` lock a default class of 1; host tests build with it, and one BIOS/`qemu64` CI cell boots a checker-enabled ISO. The default matrix stays checker-off and the IRQ-safe runqueue locks remain class 0. Host: `tests/lock_order_test.c`; QEMU marker: `lock order check=on`. |

Wave 3 verification detail:

- **Host:** `make test` includes the ACPI parser, the PMM churn baseline, and the GPU
  discovery no-MMIO-write canary test. `make test-sanitize` is the same suite under
  ASan/UBSan.
- **QEMU:** local QEMU 10.0.13, BIOS and UEFI, `qemu64`, `Haswell-v1` and `max`,
  at `-smp 1`, `-smp 2` and `-smp 4`. The matrix log set is recorded in
  `docs/PROJECT_STATUS.md`; no result is inferred from another cell.
- **CI:** the uploaded workflow still runs four BIOS/UEFI × `qemu64`/`Haswell-v1`
  cells at `-smp 1`; run
  [#45642000355](https://github.com/Momegaii/Ozero/actions/runs/45642000355) on
  `803d84c` passed all five jobs and its artifacts were inspected. The Wave 3.5 patch
  would extend `kernel-and-boot` to `{bios, uefi} × {qemu64, Haswell-v1, max} ×
  -smp {1, 2}` and add a separate BIOS/`qemu64` `lock-order-boot` job, but the supplied
  token lacks workflow scope, so it is not active. The `-smp 4` cells remain local-only.
- **Physical:** never exercised. No Wave 3 result is physical evidence.

ADRs: [0009](decisions/0009-smp-bringup-and-local-apic-timer.md) (SMP, IPI, timer),
[0010](decisions/0010-memory-hardening-smep-smap.md) (SMEP/SMAP order) and
[0011](decisions/0011-wave3-ioapic-percpu-kaslr-pmm.md) (I/O APIC, per-CPU
scheduling, kernel-stack randomization, buddy PMM, reclaim and lock classes). All are
accepted-provisional for review.

## Wave 3.5 — Verification expansion (2026-11-25, not signed off)

The Director asked for CI `-smp 2` and `-cpu max` cells, final-commit log inspection, an
item-by-item Wave 2.7 answer, the stack-randomization rename, default lock-order checking,
PR #6 scope/base, Director-observed rows, and an `-smp 2` stress test that migrates kernel
threads while a user process churns on the BSP.

| Item | Status | Evidence |
|---|---|---|
| CI `-smp 2` and `-cpu max` | **Local patch only; blocked by token scope** | The supplied fine-grained token can read and write code but not `.github/workflows` (GitHub returns "without `workflow` scope"). The prepared `.github/workflows/quality.yml` patch sets `{bios, uefi} × {qemu64, Haswell-v1, max} × -smp {1, 2}` and adds `lock-order-boot`; it is not active on GitHub. Local 18-cell matrix, including `-smp 4`, is PASS. |
| Inspect final-commit CI logs | **Done for the existing matrix; new cells blocked** | Run [#45642000355](https://github.com/Momegaii/Ozero/actions/runs/45642000355) on `803d84c` succeeds; its four `-smp 1` boot artifacts and `host-sanitizers` log were downloaded and inspected. It cannot exercise `-smp 2`/`max`/lock-order without the workflow-scoped token. |
| Wave 2.7 item-by-item answer | **Done** | `docs/PROJECT_STATUS.md` records the three items and the exact marker counts. The smoke test now rejects a run with fewer than three `user fault contained` markers. |
| Stack-randomization rename and entropy | **Done** | Docs call it kernel-stack randomization (KSR), not KASLR. Entropy is `RDRAND` (16 attempts) mixed with `RDTSC`, else `RDTSC` XOR a data address; TSC fallback is low-entropy. ADR 0011 and roadmap 3.11 record the limit. |
| Lock-order checker by default | **Done locally; CI cell blocked** | Host tests compile with `O0_LOCK_ORDER_CHECK`; the checker-enable build gives every `o0_spin_init()` lock class 1, except the IRQ-safe runqueue locks, which stay class 0. Local BIOS/`qemu64` `-smp 1` and `-smp 2` boots pass with `lock order check=on`. The prepared CI job would boot it, but the token cannot push the workflow. Nested GPU locks were given explicit inner classes. |
| PR #6 scope and base | **Recorded in STATUS** | Base is `kilo/floral-eagle-2hb`, not `main`; the PR is a combined Wave 2.7 + Wave 5.0 + Wave 3 diff. |
| Director-observed rows | **Reserved, not fabricated** | STATUS has rows for the Director's Windows 8.1 / i5-4460 / `q35` host; every Wave 3 row says not run / not reported until the Director supplies a log. |
| `-smp 2` migration + user churn | **Done locally** | `wave3_smp_stress()` runs 16 quiet user churn processes on the BSP while one kernel worker per online CPU migrates itself across CPUs. QEMU `-smp 2`: `smp user churn OK processes=16` and `smp scheduling OK cpus=2 ... migrations=7`; `-smp 4`: `cpus=4 ... migrations=7..11`. The PMM free-byte count must be unchanged. |

## Wave 4 — Storage and filesystem (not started)

1. virtio-blk first, then AHCI/NVMe with DMA and IRQ completion.
2. MBR/GPT partition discovery.
3. A crash-consistency ADR (journal or copy-on-write) and implementation, tested with
   injected power loss at every write point.
4. VFS, file descriptors, a page cache and file syscalls through an ABI ADR revision.

## Wave 5 — Platform services and security (partially complete ahead of order)

The capability handles, budgets and synchronous IPC work was completed before the
reorder and is recorded in [wave5-capability-ipc.md](wave5-capability-ipc.md). It is
single-CPU and emulator-only. The remaining Wave 5 scope is not started:

1. IPC extensions, futex-style primitives, timers and signals.
2. PCI/MSI-X, device probe/remove and a capability model for devices.
3. An audit of every syscall argument.
4. `mmap` and `spawn` only after their own ADRs.

## Wave 6 — Quality engineering (not started)

1. Fuzz the ELF loader, filesystem parser, block layer and syscalls (libFuzzer/AFL in host
   builds), with a time-boxed CI job rather than fixed cases only.
2. A lock-order checker, race stress tests and deterministic replay in QEMU.
3. Leveled logging, panic backtraces, symbolised oops and a GDB-over-serial stub.
4. CI performance baselines (context switch, allocator throughput, boot time).

## Wave 7 — Graphics (deferred)

Resume Stage 5/6 only after items 1–5 of the Director's run order are verified. Keep the
software renderer as the default path. Do the Haswell engine MMIO, reset and interrupt work
against real hardware only when the Director authorises it. Until a verified reset sequence
exists, engine reset stays refused and the engine stays quarantined. No GPU hardware claim is
made.

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
| `make test` | Host unit suites: ACPI/MADT, PMM (including churn), slab, scheduler, CPU, ABI, ELF, usercopy, capability handles/budgets/IPC, block, OzeroFS, PCI, GPU (including discovery no-MMIO canary), display, input, PS/2, surface | Kernel execution, interrupts, firmware, SMP |
| `make test-sanitize` | The same suites under ASan/UBSan with leak detection | Kernel execution |
| `make check` | Doctor + `-Werror` freestanding kernel/ISO + host suites | Booting |
| `tools/qemu-smoke.sh` | BIOS or UEFI boot, full serial marker set, timer-driven scheduler, storage/GPU software self-tests, Wave 2 user mode, capability/IPC (Wave 5.0), AP bring-up and TLB shootdown when `QEMU_SMP>1`, SMEP/SMAP when the CPU model advertises them | Physical hardware, GPU engines, AP scheduling (BSP-only scheduler) |
| `make test-sanitize` + `-fanalyzer` | Memory/UB defects and static analysis in host-buildable code | Hardware behaviour |
