# Ozero Project Status

Last updated: 2026-11-23  
Current stage: **Stage 4 — storage, filesystem and drivers (substrate implemented; review and hardware work pending)**  
Stage 3 — processes, threads and scheduler: **implemented; boot-verified in QEMU BIOS for the Stage 3 revision**  
Stage 2 — physical and virtual memory: **implemented, verified by build + host tests + QEMU boot**  
Stage 1 — bootable x86-64 kernel: **implemented; see verification state**

## Repository baseline

- Repository: `Momegaii/Ozero`
- Default branch: `main`
- Project Director approved Limine + ELF64 and freestanding C on 2026-11-23.
- Stage 3 work lives on the `kilo/morning-dale-tmb` lineage; the kernel entry, IDT and
  serial path it builds on are the ones committed on `main`.

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
  persistence failure. These new tests are authored but still require execution.
- Documentation: `docs/o0fs-format.md` specifies the draft on-disk format;
  `docs/decisions/0003-stage4-storage.md` records the storage decisions, alternatives
  and open questions. The filesystem format remains a proposal for Project Director
  review, not an approved architecture decision.

## Verification state

Truthful split by evidence type:

- **Previous baseline build/tests:** the earlier Stage 4 revision was reported to link
  with `-Werror`; `make test` reported PMM 7/7, slab 5/5, scheduler 5/5, CPU pass,
  block layer 4/4 and OzeroFS 9/9.
- **Closing-fix revision:** the expanded block (6 cases) and filesystem (10 cases)
  suites have not yet been run. The latest kernel build has also not yet been rerun;
  the previous revision's results do not validate these changes.
- **QEMU / ISO boot (BIOS):** **performed and passing for the Stage 3 revision** with
  `make iso` + `tools/qemu-smoke.sh` on QEMU 10 (both `q35` and `pc` machines).
  That run predates the Stage 4 change set. The smoke test also asserts the Stage 4
  storage markers (block layer ready, RAM disk registered, OzeroFS formatted/mounted,
  file read-back, fault test, storage self-test complete), but **the Stage 4 revision
  has not been booted in an emulator here** because QEMU is unavailable in this
  environment.
- **UEFI boot:** **not performed for this revision.** The ISO carries a UEFI
  boot path (`limine-uefi-cd.bin`), but it has not been exercised under OVMF here.
- **Real storage hardware:** never exercised. There is no ATA, AHCI or NVMe driver yet;
  the Stage 4 storage evidence comes from the RAM disk only.
- **GitHub Actions:** not confirmed for this revision.
- **Physical hardware:** never performed.

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
   maskable interrupt was silently dropped and the PIT tick never fired. PIC init
   now clears the APIC global-enable bit so the 8259 delivers INTR directly;
   Stage 4 AP bring-up re-enables and configures it.

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
- [ ] Boot verification in QEMU (UEFI) on a machine with OVMF installed.
- [ ] Physical hardware validation.

## Stage 4 exit criteria

- [x] Device-independent block layer with validated registration and request checking.
- [x] An initial storage driver behind the block layer (RAM disk) plus a synchronous
      client path.
- [x] Filesystem operations: format, mount, create, read, write, truncate, unlink,
      rmdir and directory listing, with a checksummed on-disk superblock.
- [x] Baseline fault tests: bad superblock/checksum, path and type errors, `ENOSPC`,
      injected block I/O errors, failed-write block release and remount persistence.
- [ ] Execute new closing-fix regressions for late block completion, checksummed
      malformed superblocks, lookup I/O propagation and data/inode write rollback.
- [x] Host unit tests for the block layer and the filesystem; boot self-test added to
      `kmain` and to the smoke markers.
- [ ] Boot verification of the Stage 4 revision in QEMU (BIOS and UEFI).
- [ ] A real storage protocol driver (AHCI/NVMe) and partition discovery.
- [ ] An approved filesystem format and crash-consistency/recovery design.

## Immediate next engineering wave

1. Boot the Stage 4 revision in QEMU (BIOS, then OVMF) and confirm the storage markers
   and the injected-fault path on real emulated hardware.
2. Run a UEFI boot under OVMF; add the same serial markers to any UEFI smoke path.
3. PCI enumeration plus AHCI/NVMe storage drivers with DMA and IRQ completion, then
   partition discovery (MBR/GPT) on top of the block layer.
4. User-mode entry: GDT ring-3 segments, TSS with a kernel stack, `iretq` path and a
   syscall ABI decision (ADR 0002 follow-ups).
5. Replace the static page-table pool with PMM-backed per-address-space tables.
6. AP bring-up (ACPI MADT, local APIC, SMP trampoline) and the PIT → APIC timer
   migration; the LAPIC disable added for the PIC path is removed here.
7. Memory-map reclaim (`o0_pmm_release_bootloader_memory`) per `docs/boot-contract.md`.

## Reporting standard

Separate completed code, successful builds, host tests, emulator tests, and physical
hardware tests. Never infer one from another.
