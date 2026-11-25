# Ozero Project Status

Last updated: 2026-11-25  
Current stage: **Stage 6 — Native graphical application environment (active; Phase 6.1 verified — display/framebuffer boot tested in QEMU with screenshot evidence; Phase 6.2 implemented — PS/2 keyboard + pointer driver host-tested, driver initialized at boot; Phase 6.3 implemented and host-tested — software surface primitives; Phases 6.4–6.8 implemented, host-tested and wired into the boot path (window manager, compositor, input router, desktop shell loop); no full desktop/QEMU acceptance claimed). Stage 5 is closed as the software-foundation milestone; physical GPU execution and vendor-specific acceleration backends remain deferred. See docs/stage6.md for the three-agent handoff and test plan. New GPU/Stage 6 scope is paused by Rule 0; hardening Wave 1 (verification debt) is complete locally, **Wave 2 (single-CPU user mode) is implemented and verified in QEMU on BIOS/UEFI × qemu64/Haswell-v1**, and **Wave 3 is now SMP and memory hardening under the Director's 2026-11-25 reorder: AP bring-up, xAPIC IPIs, range TLB shootdown, I/O APIC routing, per-CPU runqueues with kernel-thread migration, the LAPIC scheduler timer on every CPU, SMEP/SMAP, kernel-stack randomization (KSR), an aligned buddy PMM and bootloader-memory reclaim are implemented and verified in local QEMU; user threads remain BSP-pinned until per-CPU TSS/IST exists, and the wave is not signed off. The capability-handle, budget and synchronous-IPC work previously numbered Wave 3 is complete and is now recorded as Wave 5.0.** Wave Q adds the Director-observed evidence table, the self-describing boot header and an independent framebuffer screendump check; the Wave 2.7 sign-off is recorded below. No physical-hardware evidence exists for any wave. Waves 4–7 are planned in docs/hardening-roadmap.md.**
Stage 3 — processes, threads and scheduler: **implemented; boot-verified in QEMU BIOS for the Stage 3 revision**  
Stage 2 — physical and virtual memory: **implemented, verified by build + host tests + QEMU boot**  
Stage 1 — bootable x86-64 kernel: **implemented; see verification state**

## Repository baseline

- Repository: `Momegaii/Ozero`
- Default branch: `main`
- Project Director approved Limine + ELF64 and freestanding C on 2026-11-23.
- `main` is the only branch of record. All Stage 1–6 work and hardening Waves 1–3, 5.0
  and Q are merged into it; the older topic branches are contained in `main`.
- Current state, evidence types and open items: [docs/handoff.md](handoff.md).
- Wave 4 (storage and filesystem) directive and gate: [docs/wave4.md](wave4.md).

## Implemented

### Stage 1 — boot path

- Freestanding x86-64 C kernel entry through Limine, base revision 6.
- COM1 serial diagnostics, IDT for CPU exceptions 0–31, exception dispatch with
  vector/error/rip/cr2 and page-fault error-code decoding.
- ELF64 high-half link script at `0xffffffff80000000`.
- BIOS/UEFI hybrid ISO recipe and headless QEMU smoke test.
- GitHub Actions workflow building the kernel/ISO and running the smoke test.

### Stage 2 — memory

- `kernel/src/boot_info.c`: normalises the Limine memory map into `o0_boot_info`, and now
  resolves `kernel_phys` with a page-table walk instead of leaving it at 0.
- `kernel/src/pmm.c`: bitmap page-frame allocator, one bit per usable page, now with
  interrupt-masked allocation/free so it is safe against preemption.
- `kernel/arch/x86_64/paging.c`: 4-level tables with an HHDM direct map and a 2 MiB kernel
  mapping, physical page-table addressing resolved by page walk, real CR3 switch
  (`o0_paging_load`), address-space create/map/unmap/protect, `virt_to_phys`/`phys_to_virt`
  and checked `o0_hhdm_*` conversions.
- `kernel/arch/x86_64/cpu.c`: CPUID feature detection.
- `tests/pmm_test.c`: host unit test for the allocator.

### Stage 3 — execution model

- `kernel/src/slab.c`: page-backed slab allocator behind `o0_kalloc`/`o0_kfree`
  (8 size classes, 16-byte payload alignment, per-class free lists) replacing the
  Stage 2 bump heap.
- `kernel/src/thread.c`: TCBs, thread create/exit/join/sleep, processes, per-thread kernel
  stacks allocated from the PMM.
- `kernel/src/sched.c`: priority-aware preemptive round-robin scheduler with timeslices,
  explicit blocking and a tick-driven sleeper scan.
- `kernel/arch/x86_64/context.S`: callee-saved context switch.
- `kernel/arch/x86_64/pic.c`: 8259 remap to vectors 32–47, mask/EOI control and PIT
  programming; `kernel/arch/x86_64/irq.S` + `kernel/src/irq.c`: IRQ stubs, dispatch,
  registration and `o0_irq_save`/`o0_irq_restore`.
- `kernel/src/sync.c` + `include/ozero/sync.h`: spinlocks and a blocking mutex with
  ownership handoff.
- `kernel/src/serial.c`, `kernel/src/string.c`, `kernel/src/status.c`: shared serial console,
  freestanding memory primitives and status names.
- Boot self-tests in `kmain`: slab (allocation, alignment, reuse), address spaces
  (map/protect/unmap/ENOENT), spinlock and mutex semantics.
- `tests/slab_test.c` and `tests/sched_test.c`: host unit tests for the allocator and the
  scheduler rules; `make test` runs all three suites.

### Stage 4 — storage substrate

- `kernel/src/block.c`: block layer core. Validates device names, geometry, queues and
  every bio (op, vector bounds, block alignment, LBA range, transfer limits and feature
  flags), atomically delivers one completion per bio, and provides a synchronous
  single-segment read/write/flush API using static request slots and bounce buffers.
  Timed-out requests remain quarantined until late completion or driver-confirmed
  cancellation and quiescence.
- `kernel/src/ramdisk.c`: RAM disk, the first `o0_block_dev` driver. PMM-page backing
  reached through the direct map, 4 KiB logical blocks, two devices of up to 4 MiB, and
  a test-only fault-injection hook.
- `kernel/src/o0fs.c` + `include/ozero/fs.h`: the draft OzeroFS filesystem — superblock
  with checksum, block/inode bitmaps, 128-byte inodes with 12 direct and one indirect
  block, 32-byte directory entries, and format/mount/sync/unmount/stat/lookup/create/
  mkdir/read/write/truncate/unlink/rmdir/list operations. Failed writes capture undo
  images of existing data blocks and attempt to restore data, allocation state and the
  previous inode; rollback is not crash recovery and can itself fail.
- `kernel/src/main.c`: Stage 4 boot self-test — registers a RAM disk, formats and mounts
  OzeroFS, creates a directory and file, writes and verifies data, injects a storage
  I/O error (which `o0fs_sync` must report), clears the fault, syncs again, then removes
  the tree and unmounts.
- `tests/block_test.c` and `tests/fs_test.c`: expanded host suites (6 block-layer test
  cases and 10 filesystem test cases). New regressions cover late completion after
  timeout, driver-confirmed cancellation, transfer limits, checksummed malformed
  superblocks, directory lookup I/O errors, partial overwrite rollback and final inode
  persistence failure. These regressions are executed: `make test` builds and runs
  `tests/block_test.c` (6 cases) and `tests/fs_test.c` (10 cases) with 0 failures, and
  `make test-sanitize` runs the same suites under ASan/UBSan with 0 failures.
- Documentation: `docs/o0fs-format.md` specifies the draft on-disk format;
  `docs/decisions/0003-stage4-storage.md` records the storage decisions, alternatives
  and open questions. The filesystem format remains a proposal for Project Director
  review, not an approved architecture decision.

## Stage 5 — GPU memory and Haswell submission (active)

- Plan and phased task checklist: [docs/stage5-gpu.md](stage5-gpu.md).
- **Phase 0 — agent inspection and preparation: complete by Project Director acceptance.**
- Phase 1 is implemented and host-tested: `kernel/src/gpu_device.c` + `include/ozero/gpu_device.h`
  add a GPU-independent discovery walk over the kernel PCI layer (display-class detection, BAR
  probing, validated MMIO selection, backend match and a capability record), wired into the boot
  self-test by `stage5_self_test` in `kernel/src/main.c`. The Haswell match now claims the
  verified Intel HD 4600 (PCI 8086:0412, revision 0x06) read from a real host; all other devices
  stay unsupported. Under QEMU the emulated VGA is not the HD 4600, so boot reports
  `backend=none supported=no`; the real match is proven by a host unit test against 8086:0412.
  Wiring it exposed and fixed two latent PCI defects: `o0_pci_probe_bar` self-deadlocked on the
  non-recursive config lock, and 32-bit memory-BAR sizes were computed in 64 bits.
- Phase 2 is partial and integrated: `kernel/src/gpu.c` compiles against `include/ozero/gpu.h`
  with a first-fit VA range table, mapping bookkeeping and teardown guards, all host-tested, and
  `stage5_self_test` now exercises the create/alloc/map/unmap/free/destroy lifecycle on the real
  kernel heap. Residency, eviction, coherency and access control remain open.
- Phase 3 is implemented for 4 KiB pages: `kernel/src/gpu_hsw.c` provides Haswell PPGTT and GGTT
  encoding, validation, map/unmap, teardown and invalidation-obligation tracking, covered by host
  tests. The page directory is the single source of truth for its page tables; an earlier parallel
  `tables[512]` array made the object too large for the 4 KiB kernel slab, which only showed up at
  boot. Hardware installation (`PGTBL_CTL`) and engine register programming (`PP_DIR_BASE`,
  `PP_DCLV`) stay open because they need engine MMIO and a verified register sequence.
- Phase 4/5 are implemented in the software model: `include/ozero/gpu_queue.h` +
  `kernel/src/gpu_queue.c` hold the vendor-neutral context, queue, command and fence model, and
  `kernel/src/gpu_hsw.c` adds ring state, batch validation, submission, completion tracking,
  timeouts, cancellation and engine quarantine. Ring-read-pointer retirement releases pins but
  is never treated as completion: only a completion report signals a fence, because the PRM
  volumes available here do not document an interrupt-free completion mechanism. The boot self-test submits a validated batch,
  proves context isolation, times out a fence, refuses work on the faulted queue, refuses recovery
  without a verified reset and refuses to release unconsumed work. Nothing writes a GPU register,
  so no completion, timeout or recovery claim is a hardware claim.
- Phase 3 specification review is complete from the primary Intel Haswell PRM Volume 5
  (2013-12-18) and the supporting 2013 command/register volumes, with two checklist items still
  partial (GGTT `PGTBL_CTL` bit layout and TLB-invalidation completion semantics). Evidence and
  open questions: [docs/research/haswell-gen75-ppgtt-ggtt.md](research/haswell-gen75-ppgtt-ggtt.md).
- Remaining Stage 5 work: engine MMIO installation and register sequencing, interrupt delivery,
  a verified engine-reset sequence (without one, reset is refused and the engine stays
  quarantined), and preemption (reported unsupported). Phases 6 and 7 remain review and
  verification work.
- No GPU functionality is claimed as hardware-verified. The PPGTT/GGTT backend and the
  submission/fence model are host-tested and boot-exercised only: nothing programs a device,
  reads a GPU register or completes a fence on real hardware.
- Intel Haswell Gen7.5 is the first backend target. Intel HD Graphics 4600 is a concrete bring-up target, not the limit of Ozero GPU support.
- The architecture separates a GPU-independent Ozero core from generation/vendor-specific backends. Future Intel, AMD, NVIDIA and other devices require independently researched and verified backends; no broad vendor support is claimed.
- The plan avoids Gen8+ page-table assumptions in the Haswell backend, Linux DRM as a native dependency, and Windows WDDM as Ozero's native graphics contract.
- Host tests, QEMU boot, emulated GPU execution and physical GPU validation must be reported separately.

### Wave 2 — single-CPU user mode (2026-11-24)

The native user ABI decided in [ADR 0007](decisions/0007-user-abi-syscall-and-executable.md)
is implemented. ELF64 is used only as the image container; the syscall interface, layout,
error model and selector values are Ozero's own.

- `include/ozero/uapi.h`, `abi.h`, `cpu_local.h`: frozen syscall numbers, user error table,
  user address-space layout, selectors, `STAR = 0x0010_0008_0000_0000`, `FMASK = 0x700`, and
  the `swapgs` per-CPU block.
- `kernel/arch/x86_64/paging.c`: address spaces are allocated from the PMM (the static table
  pool is gone), the kernel half is shared, the user half is per-process, and teardown frees
  user leaf frames as well as page tables. `EFER.NXE` is enabled; leaves get W^X flags.
- `include/ozero/gdt.h` + `kernel/arch/x86_64/gdt.c`: GDT with ring-3 code/data, 64-bit TSS
  with `RSP0`, IST1 stack for the double fault; the IDT is reinstalled against it.
- `kernel/arch/x86_64/syscall.S` + `kernel/src/syscall.c`: `SYSCALL` entry, iretq return,
  `exit`, `yield`, `debug_write`, `getpid`. Interrupt and exception stubs `swapgs` on ring-3
  transitions.
- `kernel/src/usercopy.c` + `kernel/arch/x86_64/usercopy.S`: `o0_copy_from_user` /
  `o0_copy_to_user`/`o0_syscall_arg_check` are the only user-memory path. They validate the
  window and every page's U/S and write bits, then use fixup-protected copy cores.
- `kernel/src/elf.c` + `kernel/src/user.c`: static `PT_LOAD` loader and user process creation
  with a four-page stack and an unmapped guard page; `kernel/src/thread.c` gained user
  threads, guarded kernel stacks and resource release on join.
- Boot self-test: static `init` prints `user entry OK` and exits 0; three negative programs
  (NULL dereference, `cli`, jump to a kernel address) die alone; a bad syscall pointer returns
  `-O0_UE_FAULT`; 16-process churn returns the PMM to the same free-page count.

**Wave 2.7 marker assertions.** `tools/qemu-smoke.sh` asserts the following, in every matrix
cell (`{bios, uefi} × {qemu64, Haswell-v1}`) and in the CI boot job:

| Marker | What the check actually asserts |
|---|---|
| `OZERO: user entry OK` | The static `init` image reached ring 3 and successfully issued `debug_write`; the marker is emitted by user code, not by the kernel on its behalf. |
| `OZERO: syscall exit status=0` | `init` returned through `exit(0)` and `wave2_run_user` observed exactly status 0. |
| `OZERO: user fault contained` | Three negative programs (NULL read, `cli`, kernel-address jump) each took a user fault; the kernel prints `vector=`, `rip=`, `cr2=` and `error=`, and `wave2_run_user` requires status `-1` for each. |
| `OZERO: syscall bad pointer rejected` | The `badptr` program exits 0 only if the kernel returned `-O0_UE_FAULT`; the kernel panics if that status is not 0. |
| `OZERO: user churn OK processes=16` | Sixteen spawn/join cycles completed and `o0_pmm_free_bytes()` returned exactly to its pre-churn value. |
| `OZERO: wave2 self-test complete` | The whole Wave 2 sequence finished without a panic. |

Worker lines (`worker-a`/`worker-b` iteration and exit) are checked as a set, not in output
order, because the scheduler may interleave them differently between boots. UEFI logs are
matched after stripping ANSI CSI/OSC sequences.

## Hardening wave (2026-11-24)

The Project Director paused new GPU/Stage 6 scope (Rule 0) and ordered a seven-wave hardening
roadmap. Plan and per-item status: [docs/hardening-roadmap.md](hardening-roadmap.md).

**Wave 1 (verification debt) is complete locally except CI observation:**

- Stage 4 closing-fix regressions executed and passing (block 6/6, OzeroFS 10/10, also under
  ASan/UBSan).
- UEFI/OVMF boot performed for this revision. `tools/qemu-smoke.sh` now supports
  `QEMU_FIRMWARE=uefi`, and the kernel prints `OZERO: firmware=UEFI`/`BIOS` from the Limine EFI
  system table response.
- CI matrix added: `{bios, uefi} × {qemu64, Haswell-v1}` plus a sanitizer job.
- One kernel defect found and fixed: with the local APIC globally disabled, QEMU 8.2 never
  delivers the 8259 output, so the timer stopped after the first sleep. The kernel now uses the
  LAPIC ExtINT virtual-wire configuration (ADR 0005) and boots on QEMU 8.2.2 and 10.1.0.

**Wave 2 (single-CPU user mode) is implemented and verified in emulation;** its evidence is in
[hardening-roadmap.md](hardening-roadmap.md) and ADR 0007.

**Wave 5.0 (capability handles, budgets and synchronous IPC) is implemented and verified in
emulation ahead of the reordered schedule;** its design and implementation record are in
[ADR 0008](decisions/0008-capability-handles-budgets-and-ipc.md) and
[wave5-capability-ipc.md](wave5-capability-ipc.md). The evidence split is:

- **Host tests:** `tests/cap_test.c` passes under `make test` and `make test-sanitize`. It
  covers object refcounting, budget split/charge/uncharge, a tight budget whose partially
  charged memory allocation must unwind to zero, handle generations/rights/dup/close, memory
  read and W^X policy, and endpoint lifecycle.
- **Emulator boots:** all four cells of `{bios, uefi} × {qemu64, Haswell-v1}` on QEMU 10.0.13
  pass `tools/qemu-smoke.sh` with `QEMU smoke test: PASS`. Capability/IPC markers: `spawn syscall OK`,
  `ipc pair OK pair-1`, `handle negative tests OK`, `budget exhaustion OK`,
  `ipc negative tests OK`, `wave3 churn OK pairs=8` and `wave3 self-test complete`.
- **Sanitizers:** `make test-sanitize` passes with leak detection enabled.
- **CI:** GitHub Actions run [#138](https://github.com/Momegaii/Ozero/actions/runs/45587151198) on commit `521bc04` completed SUCCESS with all five jobs (`boot` × 4 and `host-sanitizers`) on QEMU 10.2.1. Its downloaded job logs contain the capability/IPC markers; this was not inferred from the Actions summary alone.
- **Physical hardware:** not exercised. None of the above is physical evidence.

The churn check requires live object count, root budget usage and PMM free pages to return to
baseline after eight process/endpoint/memory cycles. Two leaks were found and fixed while
closing the wave: a partial memory allocation undercharged its page array, and `o0_ipc_call`
held an extra reference on its reply endpoint.

### Wave Q — Director-observed evidence (separate from CI and physical)

The Project Director ran the cells below on **Windows 8.1**, an **Intel Core i5-4460** and
QEMU machine type **`q35`**. They are **Director-observed** evidence. They are neither CI
evidence nor physical-hardware evidence, and no result here is inferred into or from the CI
matrix.

| Emulator / accelerator | Firmware | CPU model | Host display | Result |
|---|---|---|---|---|
| QEMU 8.1.0 TCG | BIOS | `qemu64` | SDL | Full Wave 2 marker set passed |
| QEMU 8.1.0 TCG | BIOS | `qemu64` | GTK | Full Wave 2 marker set passed |
| QEMU 8.1.0 TCG | BIOS | `Haswell-v1` | not recorded | Full Wave 2 marker set passed |
| QEMU 8.1.0 TCG | UEFI (edk2) | `qemu64` | not recorded | Full Wave 2 marker set passed |
| QEMU 7.2.0 dev snapshot + HAXM 7.8.0 | BIOS | `qemu64` | not recorded | Full Wave 2 marker set passed |

**Commit identification:** the Director's historical logs predate the self-describing boot
header, so their commit is **unknown**. The header added in Wave Q item 3 now prints the build
commit, CPU vendor/family/model/brand and the hypervisor/firmware identification, so future
logs identify their own build. The commit is not reconstructed after the fact.

**Pending cells:** UEFI/`Haswell-v1` and HAXM/`Haswell-v1` have not been run by the Director.

**Wave Q CI observation:** GitHub Actions run
[#143](https://github.com/Momegaii/Ozero/actions/runs/45604746811) on branch head `4333352`
(PR merge commit `289f8057`) completed SUCCESS with all five jobs. The downloaded job logs
were inspected: each of the four boot cells contains `OZERO: build commit=289f80573815`, the
new CPU/platform header, the Wave 2 markers and `QEMU smoke test: PASS` on QEMU 10.2.1. Each
cell also contains `framebuffer-check: PASS width=1280 height=800 bar_width=160 bars=8
checksum=0x8acf6953`. This is CI evidence, not Director-observed and not physical evidence.

**Environment findings, not Ozero defects:**

- QEMU 8.1.0 aborts in the HAX accelerator before guest start. No guest marker was produced,
  so this is an accelerator/environment failure and is not counted as an Ozero result.
- The black margins in the window screenshot were QEMU window scaling. The raw 1280×800
  screendump shows eight 160 px bars with no margins. The CI screendump check now verifies
  the raw framebuffer layout independently of any host window.

### PR #6 scope and base (merged into `main`)

- **PR:** [#6](https://github.com/Momegaii/Ozero/pull/6), head `wave3-capability-ipc`.
- **History:** the PR was based on `kilo/floral-eagle-2hb` at `599bda1`, not `main`, so at
  the time of review it did not put the Wave 2/3/5 work on `main`. That integration branch
  has since been merged: `main` at `5636e83` is the line of record and contains this work.
- **Scope at `c9a6a9e`:** 13 commits and 94 files, not a Wave 3-only diff. It carries:
  - Wave 5.0 capability handles, budgets and synchronous IPC (`521bc04`);
  - the Wave 2.7 sign-off and framebuffer work (`1488830`, `4333352`, `274f9fb`);
  - Wave 3 SMP/memory hardening (`51dde72` through `d6b28d8`);
  - documentation and the scheduler migration validation (`a0e9e4c`, `c9a6a9e`).
- **Review consequence:** the PR had to be reviewed as the combined Wave 2.7 + Wave 5.0 +
  Wave 3 change. A Wave 3-only review of the diff would have missed the capability/IPC and
  Wave 2.7 material.

### Director-observed rows

The Wave Q rows above remain the only Director-observed evidence. No Wave 3
Director-observed row has been reported. The rows below are reserved, not results; they must
not be marked pass until the Director actually runs them and supplies the logs.

| Evidence type | Environment | Scope | Result |
|---|---|---|---|
| Director-observed, BIOS | Windows 8.1, Intel Core i5-4460, QEMU `q35`, `qemu64`, `-smp 1` | Wave 3 marker set + lock-order build | **Not run / not reported** |
| Director-observed, BIOS | same host, `qemu64`, `-smp 2` | Wave 3 marker set + SMP churn/migration stress | **Not run / not reported** |
| Director-observed, UEFI | same host, `qemu64`, `-smp 2` | Wave 3 marker set + SMP churn/migration stress | **Not run / not reported** |
| Director-observed, BIOS | same host, `Haswell-v1`, `-smp 2` | SMEP on, SMAP off; migration stress | **Not run / not reported** |
| Director-observed, BIOS | same host, `max`, `-smp 2` | SMEP/SMAP on; migration stress | **Not run / not reported** |
| Physical hardware | Any | Anything | **Never run; not claimed** |

### Wave 3 — SMP and memory hardening (implemented; local QEMU evidence; not signed off)

The Director's 2026-11-25 order moved SMP and memory hardening into Wave 3. The work below is
**not a wave sign-off**: I/O APIC routing, per-CPU runqueues, kernel-thread migration, range
TLB shootdown, kernel-stack randomization (KSR), aligned buddy allocation and bootloader reclaim are now
implemented and exercised, but user threads still do not run on APs and no physical evidence
exists.

| Evidence type | What was actually run | Result |
|---|---|---|
| Host | `make test` on 2026-11-25: ACPI MADT PASS, I/O APIC encoding/GSI PASS, lock-order runtime PASS (two nested locks), KSR layout PASS, PMM 9/9 (including reclaim and the 2 000-iteration churn), slab 5/5, scheduler 6/6 (including per-CPU migration), block 6/6, OzeroFS 10/10, GPU 26/26 (including the no-MMIO-write canary), CPU/ABI/ELF/usercopy/capability pass, surface 6 checks, PS/2 all pass | Passes locally, exit 0 |
| Host sanitizers | `make test-sanitize` (ASan/UBSan, leak detection) on the same suite | Passes locally, exit 0; no leak or UB report |
| Host analyzer | `gcc -fanalyzer` on I/O APIC, KASLR, SMP, scheduler, sync, IRQ, APIC timer, paging and PMM sources | No findings |
| QEMU, BIOS and UEFI × `qemu64`/`Haswell-v1`/`max` × `-smp 1/2/4` | `tools/qemu-smoke.sh`, QEMU 10.0.13, 18 cells | All 18 cells `QEMU smoke test: PASS`; each log has `ioapic=active gsis=24`, `kaslr layout offset=`, `bootloader reclaim bytes=`, `smp cpus=N online=N` and `timer=lapic hz=100`; N>1 logs add `tlb shootdown OK`, `tlb shootdown range OK`, `tlb shootdown full OK` and `smp scheduling OK cpus=N threads=N iterations=...`; `max` logs add `smep=on smap=on` |
| CI | GitHub Actions run [#45632539872](https://github.com/Momegaii/Ozero/actions/runs/45632539872) on commit `a0e9e4c`, QEMU 10.2.1 | SUCCESS, all five jobs. The logs were downloaded and inspected, not read from the summary: each of the four boot cells (BIOS/UEFI × `qemu64`/`Haswell-v1`) has `kaslr layout offset=`, `ioapic=active gsis=24`, `bootloader reclaim bytes=`, `smp cpus=1 online=1`, `timer=lapic hz=100`, `user entry OK`, `syscall exit status=0`, three `user fault contained` lines, `user churn OK processes=16` and `QEMU smoke test: PASS`; `Haswell-v1` prints `smep=on smap=off` and `qemu64` prints `smep=off smap=off`. The `host-sanitizers` job ran `make test-sanitize` over the full suite. CI still has only `-smp 1` and no `-cpu max`; the 2/4-CPU and SMAP cells are **local-only** until the workflow is extended. The available token cannot edit `.github/workflows/quality.yml`. |
| Physical | Never run | Not claimed |

Observed markers and what they prove:

- `OZERO: acpi cpus=N ioapics=1 overrides=N lapic=… bsp_apic=…` — the MADT was parsed from the
  firmware-provided RSDP; the CPU count follows `-smp`.
- `OZERO: smp cpus=N online=N` — every AP published by Limine reached the Ozero AP entry,
  loaded the kernel page tables/GDT/IDT, enabled its LAPIC and marked itself online.
- `OZERO: tlb shootdown OK status=0 ipi_seen=N-1` — the BSP sent one fixed-delivery IPI per AP
  and received every acknowledgement before invalidating locally.
- `OZERO: tlb shootdown range OK` and `OZERO: tlb shootdown full OK` — the range descriptor and
  full CR3-reload paths used the same bounded acknowledgement protocol.
- `OZERO: ioapic=active gsis=24` — the MADT I/O APIC was mapped, every redirection entry was
  masked, and ISA IRQ routing moved to the I/O APIC; the 8259 was masked.
- `OZERO: kaslr layout offset=0x...` — a 2 MiB-aligned stack-window offset was derived once and
  fed to every kernel-stack allocation; kernel text and the HHDM stayed at link-time addresses.
- `OZERO: bootloader reclaim bytes=N` — Limine reclaimable regions were released after boot
  information and ACPI data were copied.
- `OZERO: smp scheduling OK cpus=N threads=N iterations=...` — kernel worker threads ran on
  more than one CPU and migrated between per-CPU runqueues.
- `OZERO: timer=lapic hz=100` — the PIT calibration succeeded, the PIT was stopped, IRQ0 was
  masked and the LAPIC timer ran the scheduler; the Wave 2/3 worker and self-test markers still
  complete.
- `OZERO: smep=on smap=on` on `-cpu max` — CR4 was programmed and the usercopy path used the
  `STAC`/`CLAC` cores without faulting. `Haswell-v1` correctly reports `smap=off` because that
  CPU model has no SMAP.

Still open and explicitly not claimed: user threads on APs (per-CPU TSS/IST), work stealing,
ACPI-reclaimable release, kernel-image relocation, and physical-hardware evidence. See
[ADR 0009](decisions/0009-smp-bringup-and-local-apic-timer.md),
[ADR 0010](decisions/0010-memory-hardening-smep-smap.md),
[ADR 0011](decisions/0011-wave3-ioapic-percpu-kaslr-pmm.md) and
[hardening-roadmap.md](hardening-roadmap.md).

### Wave 3.5 — verification expansion (implemented locally; CI run pending; not signed off)

| Evidence type | What was actually run | Result |
|---|---|---|
| Host | `make check` on 2026-11-25 after the Wave 3.5 changes: full suite plus `lock_order_test` linked against `kernel/src/sync.c` | Exit 0; `lock-order: PASS`; the host build compiles with `O0_LOCK_ORDER_CHECK` and asserts `O0_LOCK_ORDER_DEFAULT == 1`. Runqueue locks are explicitly class 0; other spinlocks get class 1 |
| Host sanitizers | `make test-sanitize` on 2026-11-25 (ASan/UBSan/leak detection), same suite | Exit 0; no sanitizer report |
| Host analyzer | `gcc -fanalyzer` on `sync.c`, `gpu.c`, `gpu_hsw.c`, `main.c` | No findings |
| QEMU local matrix | QEMU 10.0.13, `{bios, uefi} × {qemu64, Haswell-v1, max} × -smp {1,2,4}`, `tools/qemu-smoke.sh` | 18/18 PASS. Every `-smp 2/4` log has `smp user churn OK processes=16` and `smp scheduling OK cpus=N ... migrations=N`; every log has exactly three `user fault contained` lines |
| QEMU lock-order boot | BIOS/`qemu64`, `make LOCK_ORDER=1 iso`, `-smp 1` and `-smp 2` | PASS; `OZERO: lock order check=on`; nested GPU locks were ordered explicitly after the checker found queue→engine and VM→memory inversions |
| CI, existing 4-cell matrix | Run [#45642000355](https://github.com/Momegaii/Ozero/actions/runs/45642000355) on PR head `803d84c` (PR merge commit `9755a595e8c5`), QEMU 10.2.1 | SUCCESS, all five jobs. Artifacts and the `host-sanitizers` job log were downloaded and inspected: the four BIOS/UEFI × `qemu64`/`Haswell-v1` cells run at `-smp 1`; each has `build commit=9755a595e8c5`, `lock order check=off`, three `user fault contained` lines, `user entry OK`, `syscall exit status=0`, `user churn OK processes=16`, the KSR/I/O APIC/reclaim/timer markers and `QEMU smoke test: PASS`. `host-sanitizers` has `lock-order: PASS` and exits 0. This is **not** evidence for `-smp 2`, `-cpu max` or the lock-order boot: those cells are not active until a workflow-scoped token is supplied. |
| CI, Wave 3.5 cells | Blocked | The prepared workflow has 12 boot cells (`-smp 1/2`, including `max`) plus `lock-order-boot`, but the supplied token is refused for `.github/workflows` without `workflow` scope. The patch is kept on local branch `wave3.5-ci-matrix` and in `/tmp/wave35-quality-workflow.patch`. |
| Physical | Never run | Not claimed |

**Wave 2.7 items, answered one by one:**

1. **Boot markers.** The markers are `OZERO: user entry OK`, `OZERO: syscall exit status=0`
   and `OZERO: user fault contained`. `tools/qemu-smoke.sh` already required the first two and
   the existence of at least one fault marker; it now also requires **three** fault markers, so
   a run that loses the NULL, privileged-instruction or kernel-address case fails. All 18 local
   guest logs contain exactly three, with vectors 14 (NULL), 13 (privileged) and 14
   (kernel-address dereference). The marker is a containment claim, not proof that the process
   was the intended one; the three programs are fixed and run in sequence.
2. **Full matrix plus sanitizer.** Local `{bios, uefi} × {qemu64, Haswell-v1, max} ×
   -smp {1,2,4}` is 18/18 PASS, and `make test-sanitize` exits 0. CI previously covered only
   `-smp 1` and no `max`; Wave 3.5 extends it to 12 cells plus the lock-order job. The final CI
   logs are not inspected yet, so CI remains unclaimed in this revision.
3. **STATUS by evidence type and same-commit docs.** This section and the Wave 3.5 roadmap
   table are updated in the same commit as the code and test changes. The older CI row above is
   retained as history but explicitly predates the new matrix; it must not be read as CI
   evidence for Wave 3.5.

**Missing fixes landed by this audit:** the three-fault assertion, the `-smp 2`/`max` CI
matrix patch, the default-on host lock checker plus one checker boot, explicit inner lock
classes, the KSR rename with entropy disclosure, the PR #6 scope/base statement, reserved
Director rows, and the `-smp 2` user-churn/migration stress. The CI matrix patch is prepared
but cannot be pushed with the supplied token; that is a credential limitation, not a test
result. No Wave 2.7 functional marker defect was found.

### Wave P.0 — physical boot gate (the Director decides; agents do not schedule it)

A real boot of the i5-4460 (HD 4600, `8086:0412` rev 06) waits for all five conditions:

1. **Boot-stage markers and fault output drawn to the framebuffer.** Open: serial markers and
   the eight-bar pattern are implemented; fault text is serial-only.
2. **Self-describing boot header.** Implemented: `build commit=`, `cpu vendor=… family=…
   model=… brand=…` and `platform=… hypervisor=… firmware=…` are the first kernel lines.
3. **Wave 2 sign-off complete.** Complete: CI run [#143](https://github.com/Momegaii/Ozero/actions/runs/45604746811) passed all five jobs, and its four boot logs were inspected. The post-reorder run [#45613293821](https://github.com/Momegaii/Ozero/actions/runs/45613293821) also passed all five jobs with the same single-CPU markers. This is emulator evidence; it does not satisfy item 1.
4. **Kernel holds its final screen instead of rebooting.** Implemented in emulation: the
   kernel halts forever after the self-tests; QEMU uses `-no-reboot`. Not exercised on
   physical hardware.
5. **A test proves the GPU discovery path writes no MMIO.** Implemented as a host test:
   `tests/gpu_test.c` supplies a display device whose selected BAR points at a canary page,
   runs `o0_gpu_discover()`, and checks every canary byte. Discovery still uses PCI
   configuration space (including BAR sizing writes and restoration); those are not MMIO.
   The test is host evidence, not a physical-boot result.

No agent may treat this list as a schedule or as permission to boot physical hardware. The
operator checklist for the eventual physical boot is in
[physical-boot-gate.md](physical-boot-gate.md).

Waves 4–7 are planned and not started. Physical hardware remains unexercised for every wave.
The capability/IPC implementation is complete as Wave 5.0. Wave 3 (SMP and memory hardening) is
partially complete and is not signed off; the remaining items are listed in the roadmap.

## Verification state

Truthful split by evidence type:

- **Build:** `make check` passed in GitHub Actions run [#43](https://github.com/Momegaii/Ozero/actions/runs/45298240681) on commit `7bb6a446f1f81fefe2102b2ab7e79e34b36cc1ef`. This includes the freestanding `-Werror` kernel/ISO build and host test suite.
- **Build (Stage 5 submission branch):** `make test` passes locally in this cloud sandbox
  (Debian 13, GCC 14.2, QEMU 10.0.13): 25 GPU tests, 0 failures, plus the existing CPU, PMM,
  slab, sync, block, FS and scheduler suites at 0 failures. `make iso` then
  `tools/qemu-smoke.sh` boots the kernel and the serial log reaches
  `OZERO: gpu submission self-test complete`, after `OZERO: gpu context isolation OK`,
  `OZERO: gpu submit seqno=1`, `OZERO: gpu fence timeout OK`,
  `OZERO: gpu faulted queue refused work`, `OZERO: gpu recovery refused without verified reset`
  and `OZERO: gpu unconsumed work quarantined`. The emulated display device is unsupported
  (`backend=none`), so this is a software-model boot, not GPU execution. No GitHub Actions run
  has been observed for this revision yet.
- **Build (previous Stage 5 branch):** `make check` passes locally in this cloud sandbox (Debian 13, GCC 14.2, binutils, xorriso, QEMU 10.0.13, KVM available): doctor, the freestanding `-Werror` kernel/ISO build and every host suite, including 15 GPU tests (the 10 Haswell PPGTT/GGTT/core tests plus 5 discovery/capability tests). QEMU smoke tests pass on both `qemu64` and `Haswell-v1`. The Stage 5 boot self-test now runs the GPU core memory-object lifecycle and GPU discovery, printing `OZERO: gpu core self-test complete`, `OZERO: gpu pci display devices=`, per-device `OZERO: gpu device ...` lines and `OZERO: gpu discovery complete`. This is local verification; the GitHub Actions run for this branch [45385070008](https://github.com/Momegaii/Ozero/actions/runs/45385070008) also passed (`x86_64-kernel` job, `make check` + both QEMU boots), observed via the GitHub API.
- **Host unit tests:** PMM 7/7, slab 5/5, scheduler 5/5, CPU, block layer 6/6, OzeroFS 10/10,
  PCI 1/1 and GPU 25/25 in the submission branch run (GPU was 15/15 on the previous branch).
- **Wave 3 local verification (2026-11-24, QEMU 10.0.13):** `make check` exit 0; `make test`
  exit 0 with ACPI PASS, I/O APIC PASS, lock-order PASS, KSR PASS, PMM 9/9, slab 5/5,
  scheduler 6/6, block 6/6, OzeroFS 10/10, GPU 26/26, CPU/ABI/ELF/usercopy/capability pass,
  surface 6 checks and PS/2 all pass; `make test-sanitize` exit 0 with leak detection;
  `gcc -fanalyzer` reports nothing on the changed I/O APIC, KASLR, SMP, scheduler, sync, IRQ,
  APIC timer, paging and PMM sources. The full BIOS/UEFI × `qemu64`/`Haswell-v1`/`max` ×
  `-smp 1/2/4` matrix is 18/18 PASS. Every cell prints `ioapic=active gsis=24`,
  `kaslr layout offset=0x...`, `bootloader reclaim bytes=...`, `smp cpus=N online=N` and
  `timer=lapic hz=100`; N>1 cells add range/full shootdown and `smp scheduling OK`; `max`
  cells add `smep=on smap=on`. The framebuffer check reports `PASS width=1280 height=800
  bar_width=160 bars=8 checksum=0x8acf6953`. CI run
  [#45632539872](https://github.com/Momegaii/Ozero/actions/runs/45632539872) on commit
  `a0e9e4c` passed all five jobs on QEMU 10.2.1; its downloaded logs were inspected and
  contain the same single-CPU markers plus the new I/O APIC, KASLR and reclaim markers. CI
  does not run the 2/4-CPU cells or `-cpu max`. This is emulator evidence, not physical
  evidence.
- **Branch audit (2026-11-24):** clean rebuild from an empty `build/`; every header compiles as
  its own translation unit; `nm -u build/kernel.elf` reports zero undefined symbols; every
  `kernel/src/*.c` and `kernel/arch/x86_64/*.c` is in the link; all host suites also pass under
  `-fsanitize=address,undefined` with no leak or UB reports; `gcc -fanalyzer` reports nothing on
  the GPU sources and one false positive in `ramdisk_copy`; the largest kernel stack frame is
  400 bytes. Fixed by this audit: `tests/pci_test.c` was never built or run (now wired into
  `make test`), `tests/gpu_test.c` leaked the resources its faulted environments deliberately
  keep alive (now released through the documented retirement path), and one `void *` arithmetic
  expression in `o0fs_format`. New closing-fix regressions passed for late completion after timeout, driver-confirmed cancellation, transfer limits, checksummed malformed superblocks, directory lookup I/O errors, partial overwrite rollback and final inode persistence failure.
- **QEMU / ISO boot (BIOS):** **Stage 4 revision boot-verified** by `tools/qemu-smoke.sh` in QEMU on `q35` with both `qemu64` and `Haswell-v1` CPU models in run #43. Both runs reached the storage self-test completion marker and reported `QEMU smoke test: PASS`. This validates the RAM-disk/OzeroFS boot self-test, not a persistent storage controller or disk image.
- **UEFI boot (Wave 1, 2026-11-24):** **verified in emulation.** The same hybrid ISO booted
  under OVMF (`OVMF_CODE_4M.fd` + per-run `OVMF_VARS_4M.fd`, package `ovmf` 2024.02-2) with
  QEMU 8.2.2 and QEMU 10.1.0, on `qemu64` and `Haswell-v1`. Every run reached the full marker
  set and `QEMU smoke test: PASS`; each log contains `OZERO: firmware=UEFI` and
  `OZERO: interrupt delivery=lapic-extint`. This is OVMF/UEFI emulation evidence, not evidence
  for a specific physical UEFI firmware. **Real UEFI firmware stays an open box:** only OVMF has
  been exercised (2024.02-2 locally, 2025.11 in CI), and vendor firmware, Secure Boot and CSM
  interactions are untested.
- **BIOS boot matrix (Wave 1):** BIOS boots on `qemu64` and `Haswell-v1` pass on QEMU 8.2.2
  and 10.1.0 with the same markers (`OZERO: firmware=BIOS`).
- **Host sanitizer run (Wave 1):** `make test-sanitize` (ASan + UBSan, leak detection on)
  passes every suite: PMM 7/7, slab 5/5, scheduler 5/5, CPU, block 6/6, OzeroFS 10/10, PCI 1/1,
  GPU 25/25, display, input, PS/2 6/6 and surface. `gcc -fanalyzer` reports nothing for
  `kernel/arch/x86_64/pic.c` or `kernel/src/main.c`.
- **Real storage hardware:** never exercised. There is no ATA, AHCI or NVMe driver yet; the Stage 4 storage evidence comes from the RAM disk only.
- **GitHub Actions:** run #43 passed build, all host tests and both QEMU BIOS smoke boots on an
  earlier revision. For the Wave 1 branch there are two **distinct** runs, each SUCCESS with all
  five jobs (`boot (bios, qemu64)`, `boot (uefi, qemu64)`, `boot (bios, Haswell-v1)`,
  `boot (uefi, Haswell-v1)`, `host-sanitizers`):
  [#132](https://github.com/Momegaii/Ozero/actions/runs/45462527735) on commit `d6626b4` and
  [#133](https://github.com/Momegaii/Ozero/actions/runs/45463475234) on commit `7e2c4bb`. They
  have different URLs; a previous note that linked both numbers to one URL was wrong. Run #131
  was also SUCCESS. **Inspection limits:** the anonymous REST API is 403 from this sandbox, but
  an authenticated call works — run #133's job logs were downloaded and read (they show
  `QEMU 10.2.1`, `OVMF 2025.11-3ubuntu7.4`, `OZERO: firmware=BIOS`/`UEFI`,
  `OZERO: interrupt delivery=lapic-extint` and `QEMU smoke test: PASS` in all four cells, plus
  every host suite passing in `host-sanitizers`). Run #132 was read from the Actions page only;
  its logs were not inspected.
- **CI versus local emulator versions:** CI runs **QEMU 10.2.1** with OVMF 2025.11 on
  `ubuntu-latest`. The local matrix additionally covers QEMU 8.2.2, 10.0.13 and 10.1.0, and those
  results are **local-only**. QEMU 8.2 matters because it exposed the Wave 1 interrupt-delivery
  defect (ADR 0005) and CI does not run it.
- **Host tests (Wave 2):** `make test` passes every suite, including `abi_test` (selectors,
  `STAR`/`FMASK`, GDT access bytes, TSS/per-CPU offsets, error mapping and user-window rules),
  `elf_test` (accept/reject matrix, overlapping segments and fuzz of 4 000 mutated + 1 000
  random + random-truncated inputs) and `usercopy_test` (null guard, kernel half, unmapped
  page, read-only write, range overflow, zero length, faulting raw core and fixup lookup).
  `make test-sanitize` (ASan + UBSan, leaks on) passes the same set. GCC `-fanalyzer` reports
  nothing on the changed kernel sources.
- **QEMU boot matrix (Wave 2, local):** the full marker set now includes `gdt+tss installed`,
  `user entry OK`, `syscall exit status=0`, three `user fault contained vector=…` lines,
  `syscall bad pointer rejected`, `user churn OK processes=16` and
  `wave2 self-test complete`. All four cells (`{bios, uefi} × {qemu64, Haswell-v1}`) pass
  locally on **QEMU 10.0.13** with `QEMU smoke test: PASS`. The Wave Q re-run also passes all
  four cells with the new boot header, signed fault status, `vector=` and framebuffer
  markers. This is emulator evidence, not physical evidence.
- **Framebuffer screendump (Wave Q.8, local):** `tools/framebuffer-check.py` passed under BIOS
  and UEFI on QEMU 10.0.13: `width=1280 height=800 bar_width=160 bars=8
  checksum=0x8acf6953`. It validates the eight bar centres at three heights, both edges and
  the kernel's FNV checksum. This is an independent raw-framebuffer check, not a host-window
  screenshot. `make check` runs it automatically when QEMU is installed, so the CI
  `kernel-and-boot` job exercises it; the serial log is covered by the existing
  `build/qemu-serial-*.log` artifact glob. A separate PPM artifact remains a workflow-file
  follow-up; it is an artifact-retention gap, not a missing check.
- **CI (Wave 2 and Wave Q):** GitHub Actions run
  [#143](https://github.com/Momegaii/Ozero/actions/runs/45604746811) on branch head `4333352`
  (PR merge commit `289f8057`) completed SUCCESS with all five jobs (`boot` × 4 and
  `host-sanitizers`). The four boot logs were downloaded and inspected: each shows
  `OZERO: build commit=289f80573815`, the CPU/platform header, the Wave 2 markers,
  `framebuffer-check: PASS` and `QEMU smoke test: PASS` on QEMU 10.2.1, with OVMF
  2025.11-3ubuntu7.4 in the UEFI cells. The `host-sanitizers` log shows `usercopy_test`
  compiled and run under ASan/UBSan with no sanitizer report. This is CI evidence, not
  physical evidence.
- **Physical hardware (Wave 2):** never exercised. No user-mode, fault-containment or
  process-churn result here is physical evidence.
Boot verification of this revision surfaced and fixed three defects that build +
host tests alone could not catch:

1. **Kernel image mapping (`paging.c`).** The kernel is linked at
   `0xffffffff80000000` but Limine loads it at a physical address that is not
   congruent to the link address modulo 2 MiB. The old 2 MiB-page mapping aligned
   virtual and physical down independently, dropping the sub-2 MiB offset, so the
   first instruction after the CR3 switch read the wrong bytes and triple-faulted.
   The kernel image is now mapped with 4 KiB pages so the offset is preserved.
2. **`O0_ALIGN_UP`/`O0_ALIGN_DOWN` (`types.h`).** The `~((a) - 1u)` mask was
   computed in 32-bit unsigned, so aligning a 64-bit pointer with an `int`
   literal (e.g. `O0_ALIGN_DOWN(stack_top, 16)` in `thread.c`) zeroed bits 32..63
   and produced a near-NULL stack pointer. The mask is now computed in
   `o0_paddr_t`.
3. **Local APIC left enabled (`pic.c`).** Limine enables the LAPIC
   (`IA32_APIC_BASE` bit 11). With the APIC active, the 8259's INTR is routed
   through LINT0 and only reaches the CPU when LVT0 is programmed for ExtINT
   (virtual-wire) delivery; this PIC-only stage never does that, so every
   maskable interrupt was silently dropped and the PIT tick never fired. The
   Stage 3 fix cleared the APIC global-enable bit so the 8259 delivers INTR
   directly. **Superseded by ADR 0005 (Wave 1, 2026-11-24):** clearing the enable
   bit is not portable — QEMU 8.2 then drops the 8259 output entirely — so the
   kernel now programs the virtual-wire path (APIC enabled, `LVT0` ExtINT,
   `TPR` = 0) through a dedicated uncached mapping.

The `tests/pmm_test.c` failure recorded in review (free-page accounting) was a
defect in the test — `struct o0_boot_info` was used uninitialised — and is fixed;
the test now zeroes the structure. The documented build command for that test was
wrong (it linked `pmm.c` twice) and is fixed as well.

## Follow-up review corrections (2026-11-23)

A second review pass over the Stage 3 branch fixed further defects. None of them changes the
stage scope, the QEMU BIOS verification above, or the decisions recorded in ADR 0002.

- `kernel/src/pmm.c`: the allocator reserved its "kernel" frames from the first usable
  region instead of the kernel's own physical range, so frames backing the image could be
  handed out. It now reserves the pages of
  `[kernel_phys, kernel_phys + kernel_phys_size)` that it actually tracks, and keeps the
  first-region heuristic only when `kernel_phys` is unknown. `tests/pmm_test.c` covers the
  case with a new kernel-image reservation test.
- `kernel/src/thread.c`: `o0_thread_join` could miss an exit that happened between the
  `exited` test and the block, leaving the joiner blocked forever. The test, the waiter
  registration and the block now happen in one interrupt-disabled section.
- `kernel/src/sync.c`: the no-scheduler fallback in `o0_mutex_lock` spun until the mutex
  looked free and then returned without acquiring it. It now retries the acquisition.
- `kernel/arch/x86_64/paging.c`: the direct map is built from 2 MiB pages, so the HHDM
  base in force is now the 2 MiB-aligned offset and `virt = phys + hhdm_offset` holds for
  the kernel's own map. `o0_as_map` bounded the mapping *length* against the user-space
  split instead of the end address; it now bounds the end address.
- Documentation and comments: broken cross-references (`docs/boot-contract.md` section
  numbers, missing `docs/cpu-baseline.md`, `docs/interface-draft.md` and
  `docs/a3-dma-contract.md`), stale stage references, a misleading spurious-IRQ comment,
  a leftover authoring note in a test comment, and grammar errors were corrected.
  `tools/README.md` now states that `make check` also runs the host tests.

`make test` therefore reports PMM 7/7, slab 5/5, scheduler 5/5 and CPU pass.

## Stage 3 exit criteria

- [x] Kernel runs on its own page tables (CR3 switched to kernel-built tables).
- [x] Virtual memory: address-space create/map/unmap/protect with page-level protection.
- [x] General-purpose kernel allocator that can free (slab).
- [x] Thread control blocks, process structures and per-thread kernel stacks.
- [x] Context switch implemented in assembly.
- [x] Timer-driven preemptive scheduler with priorities and timeslices.
- [x] Synchronization primitives: spinlock and blocking mutex.
- [x] Host-side tests for the allocator and scheduler rules.
- [x] Boot verification in QEMU (BIOS): boot milestones and the preemptive
      scheduler running the demo threads to completion (smoke test PASS).
- [x] Boot verification in QEMU (UEFI) under OVMF (ovmf 2024.02-2, QEMU 8.2.2 and 10.1.0,
      `qemu64` and `Haswell-v1`) — Wave 1 evidence.
- [ ] Physical hardware validation.

## Stage 4 exit criteria

- [x] Device-independent block layer with validated registration and request checking.
- [x] An initial storage driver behind the block layer (RAM disk) plus a synchronous
      client path.
- [x] Filesystem operations: format, mount, create, read, write, truncate, unlink,
      rmdir and directory listing, with a checksummed on-disk superblock.
- [x] Baseline fault tests: bad superblock/checksum, path and type errors, `ENOSPC`,
      injected block I/O errors, failed-write block release and remount persistence.
- [x] Execute new closing-fix regressions for late block completion, checksummed
      malformed superblocks, lookup I/O propagation and data/inode write rollback
      (block 6/6, OzeroFS 10/10, also under ASan/UBSan).
- [x] Host unit tests for the block layer and the filesystem; boot self-test added to
      `kmain` and to the smoke markers.
- [x] Boot verification of the Stage 4 revision in QEMU BIOS (`q35`, `qemu64` and
      `Haswell-v1`) with storage self-test markers passing.
- [x] Boot verification of the Stage 4 revision in QEMU UEFI/OVMF (Wave 1, see above).
- [ ] A real storage protocol driver (AHCI/NVMe) and partition discovery.
- [ ] An approved filesystem format and crash-consistency/recovery design.

## Immediate next engineering wave

Wave 1 closed the emulator/firmware verification debt. Wave 2 added and verified single-CPU
user mode in emulation. The capability/IPC work is complete as Wave 5.0. Wave 3 is now SMP and
memory hardening; it is **partially complete and not signed off**. No wave has physical-hardware
evidence.

Ordered by `docs/hardening-roadmap.md`:

1. **Finish Wave 3:** close the remaining review items and add per-CPU TSS/IST before user
   threads leave the BSP. I/O APIC routing, per-CPU runqueues, kernel-thread migration, range
   shootdown, kernel-stack randomization (KSR), aligned buddy allocation and bootloader reclaim are
   implemented and locally verified.
2. **Wave 4 — storage and filesystem:** virtio-blk first, then AHCI/NVMe with DMA and IRQ
   completion; MBR/GPT partition discovery; crash consistency as an ADR; then a VFS with file
   descriptors and a page cache. No part of Wave 4 is started.
3. **Wave 5 — remaining platform services:** IPC extensions, futex-style primitives, timers,
   signals, PCI/MSI-X, device probe/remove, syscall-argument audit, `mmap` and `spawn` ADRs.
   The capability-handle/budget/IPC core is already complete.

GPU engine work stays paused until Wave 7.

## Reporting standard

Separate completed code, successful builds, host tests, emulator tests, and physical
hardware tests. Never infer one from another.
