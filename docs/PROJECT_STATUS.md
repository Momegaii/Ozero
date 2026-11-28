# Ozero Project Status

Last updated: 2026-11-27  
Current stage: **Stage 6 — Native graphical application environment (active).** Stage 6 desktop UI is implemented, host-tested, and QEMU-verified (BIOS + UEFI/OVMF). Stage 5 is closed as the software-foundation milestone; physical GPU execution and vendor-specific acceleration backends remain deferred. See docs/stage6.md for the Stage 6 phase breakdown and acceptance criteria.

## Repository baseline

- Repository: `Momegaii/Ozero`
- Default branch: `main`
- Project Director approved Limine + ELF64 and freestanding C on 2026-11-23.
- `main` is the only branch of record. All Stage 1–6 work and hardening Waves 1–3 and 5.0 are merged into it.
- Current state, evidence types and open items: [docs/handoff.md](handoff.md).
- Wave 4 (storage and filesystem) directive and gate: [docs/wave4.md](wave4.md).

## Commit line of record (this workspace)

| Commit | Description |
|--------|-------------|
| `e904fe7` | P0 baseline: boot timing, memory metrics, regression checks |
| `4f641e8` | P1: input acceptance — focus borders, cursor variants, window click routing |
| `49585b2` | P2: Stage 6 acceptance — focus border tests, off-screen fix |
| `173e0f3` | P1 fix: rect-based continuous focus border, 64-bit clipping, framebuffer-check layout updates |
| `a478b385` | Merge: P1 focus-border regression fix + P2 Stage 6 acceptance |

`main` is synced with `origin/main` at `a478b385`.

## Implemented

### Stage 1 — boot path

- Freestanding x86-64 C kernel entry through Limine, base revision 6.
- COM1 serial diagnostics, IDT for CPU exceptions 0–31, exception dispatch with vector/error/rip/cr2 and page-fault error-code decoding.
- ELF64 high-half link script at `0xffffffff80000000`.
- BIOS/UEFI hybrid ISO recipe and headless QEMU smoke test (`tools/qemu-smoke.sh`).
- Boot markers: `kernel entry reached`, `architecture=x86_64`, `boot protocol=Limine`, `IDT installed`, `PMM initialised`, `page tables active`, `kernel heap ready`, `PIC remapped`, `interrupt delivery=lapic-extint`, `scheduler initialised`, `timer IRQ enabled at 100 Hz`, `boot success; starting scheduler`.

### Stage 2 — memory

- `kernel/src/boot_info.c`: normalises the Limine memory map into `o0_boot_info`, resolves `kernel_phys` with a page-table walk.
- `kernel/src/pmm.c`: bitmap page-frame allocator with buddy coalescing checks and aligned orders, interrupt-masked allocation/free.
- `kernel/arch/x86_64/paging.c`: 4-level tables with HHDM direct map and 2 MiB kernel mapping, physical page-table addressing resolved by page walk, real CR3 switch, address-space create/map/unmap/protect, `virt_to_phys`/`phys_to_virt`, checked `o0_hhdm_*` conversions.
- `kernel/arch/x86_64/cpu.c`: CPUID feature detection.
- `tests/pmm_test.c`: host unit test for the allocator.

### Stage 3 — execution model

- `kernel/src/slab.c`: page-backed slab allocator behind `o0_kalloc`/`o0_kfree` (8 size classes, 16-byte payload alignment, per-class free lists).
- `kernel/src/thread.c`: TCBs, thread create/exit/join/sleep, processes, per-thread kernel stacks allocated from the PMM.
- `kernel/src/sched.c`: priority-aware preemptive round-robin scheduler with timeslices, explicit blocking, tick-driven sleeper scan, per-CPU runqueues with kernel-thread migration.
- `kernel/arch/x86_64/context.S`: callee-saved context switch.
- `kernel/arch/x86_64/pic.c`: 8259 remap to vectors 32–47, mask/EOI control and PIT programming; `kernel/arch/x86_64/irq.S` + `kernel/src/irq.c`: IRQ stubs, dispatch, registration and `o0_irq_save`/`o0_irq_restore`.
- `kernel/src/sync.c` + `include/ozero/sync.h`: spinlocks and a blocking mutex with ownership handoff.
- `kernel/src/serial.c`, `kernel/src/string.c`, `kernel/src/status.c`: shared serial console, freestanding memory primitives and status names.
- Boot self-tests in `kmain`: slab (allocation, alignment, reuse), address spaces (map/protect/unmap/ENOENT), spinlock and mutex semantics.
- `tests/slab_test.c` and `tests/sched_test.c`: host unit tests for the allocator and the scheduler rules; `make test` runs all three suites.

### Stage 4 — storage substrate

- `kernel/src/block.c`: block layer core. Validates device names, geometry, queues and every bio (op, vector bounds, block alignment, LBA range, transfer limits and feature flags), atomically delivers one completion per bio, synchronous single-segment read/write/flush API using static request slots and bounce buffers. Timed-out requests remain quarantined until late completion or driver-confirmed cancellation and quiescence.
- `kernel/src/ramdisk.c`: RAM disk, the first `o0_block_dev` driver. PMM-page backing reached through the direct map, 4 KiB logical blocks, two devices of up to 4 MiB, test-only fault-injection hook.
- `kernel/src/o0fs.c` + `include/ozero/fs.h`: the draft OzeroFS filesystem — superblock with checksum, block/inode bitmaps, 128-byte inodes with 12 direct and one indirect block, 32-byte directory entries, and format/mount/sync/unmount/stat/lookup/create/mkdir/read/write/truncate/unlink/rmdir/list operations. Failed writes capture undo images and attempt rollback; rollback is not crash recovery and can itself fail.
- `kernel/src/main.c`: Stage 4 boot self-test — registers a RAM disk, formats and mounts OzeroFS, creates a directory and file, writes and verifies data, injects a storage I/O error, clears the fault, syncs again, then removes the tree and unmounts.
- `tests/block_test.c` and `tests/fs_test.c`: expanded host suites (6 block-layer test cases and 10 filesystem test cases). New regressions cover late completion after timeout, driver-confirmed cancellation, transfer limits, checksummed malformed superblocks, directory lookup I/O errors, partial overwrite rollback and final inode persistence failure.
- Documentation: `docs/o0fs-format.md` specifies the draft on-disk format; `docs/decisions/0003-stage4-storage.md` records the storage decisions, alternatives and open questions. The filesystem format remains a proposal for Project Director review, not an approved architecture decision.

### Stage 5 — GPU memory and Haswell submission (closed as software milestone)

- `kernel/src/gpu_device.c` + `include/ozero/gpu_device.h`: GPU-independent discovery walk over the kernel PCI layer (display-class detection, BAR probing, validated MMIO selection, backend match and capability record), wired into the boot self-test. Haswell match claims Intel HD 4600 (PCI 8086:0412, revision 0x06); all other devices stay unsupported. Under QEMU the emulated VGA is not the HD 4600, so boot reports `backend=none supported=no`; the real match is proven by a host unit test against 8086:0412.
- `kernel/src/gpu.c`: first-fit VA range table, mapping bookkeeping and teardown guards, host-tested; `stage5_self_test` exercises create/alloc/map/unmap/free/destroy lifecycle on the real kernel heap.
- `kernel/src/gpu_hsw.c`: Haswell PPGTT and GGTT encoding, validation, map/unmap, teardown and invalidation-obligation tracking, covered by host tests.
- `kernel/src/gpu_queue.c`: vendor-neutral context, queue, command and fence model; Haswell ring state, batch validation, submission, completion tracking, timeouts, cancellation and engine quarantine. Nothing writes a GPU register — no completion, timeout or recovery claim is a hardware claim.
- No GPU functionality is claimed as hardware-verified. The PPGTT/GGTT backend and the submission/fence model are host-tested and boot-exercised only.

### Stage 6 — Native graphical application environment (active)

- Phase 6.1 — Display and framebuffer: **verified.** Limine framebuffer request, descriptor validation, UC 4 KiB direct-map carveout, 8-band boot pattern, QEMU BIOS + UEFI boot verified, `tools/framebuffer-check.py` screendump check (width=1280 height=800 bars=8 checksum=0x8acf6953).
- Phase 6.2 — Input: **implemented.** PS/2 keyboard on IRQ1 (scan code set 2, make/break, extended codes, modifiers, toggle locks), PS/2 mouse on IRQ12 (stream mode, 3-byte packets), bounded input queue with overflow accounting, IRQ-safe event production.
- Phase 6.3 — Surfaces: **implemented and host-tested.** fill, fill_rect, clipped blit, put_pixel, fill_circle, draw_circle, blend_rgb, RGBX8888/BGRX8888.
- Phase 6.4 — Window manager: **implemented.** bounded window registry (max 16), create/destroy/move/resize/visibility/focus/hit-test, z-order re-stacking on focus, focus fallback on hide/destroy, geometry clipping, bounded resize (32 px minimum, bounded by backing surface).
- Phase 6.5 — Software compositor: **implemented.** back-to-front composition, z-order, clipping, off-screen culling, invisible window skip, **neon-green focus borders** (2 px rect-based, continuous, clipped to destination).
- Phase 6.6 — Graphics acceleration boundary: capability-driven interface; software fallback remains functional; no unsupported vendor backend claims.
- Phase 6.7 — Custom O0 desktop UI: **implemented per ADR 0020.** black background, neon-green sphere launchers (solid color, no pictograms), purple assistant spheres, green particles with trails, node network, bottom taskbar, right-side panel, system tray, O0 assistant interface, sphere state machine (idle→hover→active→feedback→idle), cursor variants (arrow/busy/ibeam), smooth fixed-point hover easing, rounded panel/taskbar corners, drop shadows, inner sphere rings, keyboard focus tracking.
- Phase 6.8 — Integration: **implemented.** desktop_main drives the desktop loop, input router connects to shell and window manager, window clicks take priority over launcher activation, keyboard events delivered to focused window's callback.

### Wave 1 — verification debt (complete locally)

- Stage 4 closing-fix regressions executed and passing (block 6/6, OzeroFS 10/10, also under ASan/UBSan).
- UEFI/OVMF boot performed for this revision. `tools/qemu-smoke.sh` supports `QEMU_FIRMWARE=uefi`, and the kernel prints `OZERO: firmware=UEFI`/`BIOS` from the Limine EFI system table response.
- CI matrix added: `{bios, uefi} × {qemu64, Haswell-v1}` plus a sanitizer job (referenced from this workspace's CI configuration; run numbers not recorded locally).
- One kernel defect found and fixed (Wave 1): with the local APIC globally disabled, QEMU 8.2 never delivers the 8259 output, so the timer stopped after the first sleep. The kernel now uses the LAPIC ExtINT virtual-wire configuration (ADR 0005) and boots on QEMU 8.2.2 and 10.1.0.

### Wave 2 — single-CPU user mode (implemented and emulator-verified)

The native user ABI decided in [ADR 0007](decisions/0007-user-abi-syscall-and-executable.md) is implemented. ELF64 is used only as the image container; the syscall interface, layout, error model and selector values are Ozero's own.

- `include/ozero/uapi.h`, `abi.h`, `cpu_local.h`: frozen syscall numbers, user error table, user address-space layout, selectors, `STAR = 0x0010_0008_0000_0000`, `FMASK = 0x700`, `swapgs` per-CPU block.
- `kernel/arch/x86_64/paging.c`: address spaces allocated from the PMM (static table pool removed), kernel half shared, user half per-process, teardown frees user leaf frames and page tables. `EFER.NXE` enabled; leaves get W^X flags.
- `include/ozero/gdt.h` + `kernel/arch/x86_64/gdt.c`: GDT with ring-3 code/data, 64-bit TSS with `RSP0`, IST1 stack for double fault; IDT reinstalled against it.
- `kernel/arch/x86_64/syscall.S` + `kernel/src/syscall.c`: `SYSCALL` entry, `iretq` return, `exit`, `yield`, `debug_write`, `getpid`. Interrupt and exception stubs `swapgs` on ring-3 transitions.
- `kernel/src/usercopy.c` + `kernel/arch/x86_64/usercopy.S`: `o0_copy_from_user`/`o0_copy_to_user`/`o0_syscall_arg_check` are the only user-memory path. They validate the window and every page's U/S and write bits, then use fixup-protected copy cores. SMAP-ready (`STAC`/`CLAC` hooks present; cores are the single place to add them).
- `kernel/src/elf.c` + `kernel/src/user.c`: static `PT_LOAD` loader and user process creation with a four-page stack and unmapped guard page; `kernel/src/thread.c` gained user threads, guarded kernel stacks and resource release on join.
- Boot self-test: static `init` prints `user entry OK` and exits 0; three negative programs (NULL dereference, `cli`, jump to a kernel address) die alone; a bad syscall pointer returns `-O0_UE_FAULT`; 16-process churn returns the PMM to the same free-page count.

### Wave 3 — SMP and memory hardening (implemented in source)

The Director's 2026-11-25 order moved SMP and memory hardening into Wave 3. The work below is **implemented and emulator-verified locally**, but user threads still do not run on APs and no physical evidence exists.

| Feature | Implementation | Status |
|---------|---------------|--------|
| AP bring-up | `kernel/src/smp.c`, Limine MP protocol, `o0_ap_entry_c` loads page tables/GDT/IDT/LAPIC, marks AP online | Implemented, QEMU-verified |
| xAPIC IPIs | `kernel/src/smp.c` `x86_apic_ipi_dispatch`, TLB shootdown + reschedule IPIs | Implemented, QEMU-verified |
| TLB shootdown | single page, range, full CR3 reload with bounded acknowledgement protocol; timeout panic on stale TLB | Implemented, QEMU-verified |
| I/O APIC routing | `kernel/src/ioapic.c`, MADT parsing, redirection entries masked, ISA IRQ routing moved to I/O APIC, 8259 masked | Implemented, QEMU-verified |
| Per-CPU runqueues | `kernel/src/sched.c`, `runqueues[O0_MAX_CPUS]`, per-CPU lock/head/tail/count/current | Implemented, QEMU-verified |
| Kernel-thread migration | `kernel/src/sched.c` migration IPI retargets current thread | Implemented, QEMU-verified |
| LAPIC timer | `kernel/src/apic_timer.c`, PIT calibration, PIT stopped, IRQ0 masked, LAPIC timer runs scheduler at 100 Hz | Implemented, QEMU-verified |
| SMEP/SMAP | `kernel/arch/x86_64/cpu.c` `o0_cpu_enable_smep_smap()`, `kernel/src/usercopy.c` `STAC`/`CLAC` cores | Implemented, QEMU-verified (`smep=on smap=on` on `-cpu max`) |
| Kernel-stack randomization (KSR) | `kernel/src/kaslr.c`, `kernel/arch/x86_64/paging.c`, 2 MiB-aligned kernel-stack window randomized at boot | Implemented, QEMU-verified |
| Aligned buddy PMM | `kernel/src/pmm.c`, bitmap allocator with buddy coalescing checks and aligned power-of-two orders | Implemented, host-tested |
| Bootloader reclaim | `kernel/src/pmm.c` `o0_pmm_release_bootloader_memory()`, releases reclaimable regions after boot info and ACPI data copied | Implemented |

User threads remain BSP-pinned until per-CPU TSS/IST exists. Ring 3 and SMAP on non-boot CPUs are untested.

### Wave 4 — storage and filesystem

- Wave 4.1 — PCI, MSI/MSI-X and DMA foundations: **implemented.** capability-list parsing, BAR size from all-ones mask, MSI/MSI-X programming and LAPIC vector dispatch, DMA allocator with pinning, host tests. No device driver consumes it yet.
- Wave 4.2/4.3 — PCI storage drivers: **implemented and build-verified.** legacy virtio-blk, AHCI (one port with command list, FIS, PRDT, Identify Device), NVMe (one admin plus one I/O queue pair, MSI-X entries). Each has an injected past-end-of-medium read proof. Completion is a bounded poll; interrupts are counted but not load-bearing. `make kernel` passes with `-Werror` and `tests/storage_encode_test.c` passes. **No QEMU storage cell has been observed green** — not evidence until a run is recorded.
- Wave 4.4–4.7: **partial.** Crash consistency per ADR 0012/0018 implemented (redo journal v2, recovery replay, `o0fs_check`); host evidence `o0fs_journal_test` 5/5 and `fs_test` 10/10. MBR/GPT partition discovery and VFS with file descriptors and page cache remain open.

### Wave 5.0 — capability handles, budgets and synchronous IPC (implemented and emulator-verified)

- `kernel/src/object.c`, `kernel/src/budget.c`, `kernel/src/handle.c`, `kernel/src/memory.c`, `kernel/src/endpoint.c`, `kernel/src/ipc.c`: object refcounting, budget split/charge/uncharge, handle generations/rights/dup/close, memory read and W^X policy, endpoint lifecycle, synchronous IPC.
- `tests/cap_test.c`: host unit test, passes under `make test` and `make test-sanitize`.
- Emulator boots: `{bios, uefi} × {qemu64, Haswell-v1}` pass `tools/qemu-smoke.sh` with capability/IPC markers (`spawn syscall OK`, `ipc pair OK pair-1`, `handle negative tests OK`, `budget exhaustion OK`, `ipc negative tests OK`, `wave3 churn OK pairs=8`, `wave3 self-test complete`).
- Physical hardware: not exercised. None of the above is physical evidence.

### Wave Q — Director-observed evidence (separate from CI and physical)

Documented in `docs/PROJECT_STATUS.md` as reserved rows, not results. No Wave 3 Director-observed row has been reported. Physical hardware: never run, never claimed.

## Verification state

### Build
- `make kernel`: freestanding ELF64 kernel builds cleanly with `-Werror` (`-Wall -Wextra -Werror`).
- `make iso`: BIOS/UEFI bootable ISO builds successfully (Limine v11.4.1).
- `make check`: doctor, ISO build, host tests, and framebuffer check (when QEMU is installed).

### Host unit tests
All suites pass under `make test` and `make test-sanitize` (ASan + UBSan, leak detection):
- PMM, slab, scheduler, CPU, block (6/6), OzeroFS (10/10), PCI (1/1), GPU (26/26), display, input, PS/2, surface, window, compositor, shell, desktop_ui, abi, elf, pe, pe_fuzz, usercopy, cap, block, partition, fs, storage_encode, virtio_ring, virtio_blk_modern, o0fs_journal, kaslr, ioapic, lock_order, acpi, dma, gfx_backend.

### Emulator (QEMU) — local verification only
- BIOS boot (`q35`, `qemu64`, `-smp 1`): `tools/qemu-smoke.sh` PASS, full marker set including `OZERO: stage6 desktop active`, `OZERO: user entry OK`, `OZERO: syscall exit status=0`, three `OZERO: user fault contained` lines, `OZERO: user churn OK processes=16`.
- UEFI/OVMF boot (`q35`, `qemu64`, `-smp 1`, OVMF 4 MiB code + per-run variable store): PASS, full marker set including `OZERO: firmware=UEFI`, `OZERO: interrupt delivery=lapic-extint`.
- QEMU 10.0.13 locally; QEMU 8.2.2 and 10.1.0 also tested locally for Wave 1 regression coverage.
- `-smp 2` and `-cpu max` cells tested locally; CI coverage for these cells is not recorded in this workspace.

### Framebuffer screendump
- `tools/framebuffer-check.py`: PASS under BIOS and UEFI on QEMU 10.0.13: `width=1280 height=800 bar_width=160 bars=8 checksum=0x8acf6953`. Independent raw-framebuffer check, not a host-window screenshot.

### QEMU desktop acceptance (Stage 6)
- Desktop boots to `OZERO: stage6 desktop active` with P0 boot timing metrics (`boot-to-desktop-ticks`, `boot-to-desktop-ms`, `free memory after desktop init`).
- QMP screendump: 1280×800, 93% dark background, neon-green sphere pixels, demo windows rendered with correct colors (win1 BGR 160,96,64; win2 BGR 96,64,160), taskbar (5,5,5) and panel (10,10,10) present.
- `tools/desktop-check.py`: BGR color comparisons, window color checks, focus border checks — PASS.
- `tools/input-test.py`: automated QEMU input test — desktop boots and renders; `sendkey` via QEMU monitor does not produce observable PS/2 keyboard events (known QEMU limitation, documented).

### Physical hardware
- **Never exercised.** No physical-hardware evidence exists for any stage or wave. All verification is host, QEMU emulator, or CI (where applicable).

## Reporting standard

Separate completed code, successful builds, host tests, emulator tests, and physical hardware tests. Never infer one from another. Evidence claims in this document reference commits on `main` only; CI run numbers from parallel branches are not recorded here.

## Open items

- Physical hardware validation for all stages (never run, never claimed).
- Wave 4.4–4.7: partition discovery, crash consistency, VFS page cache.
- Real storage protocol driver (AHCI/NVMe) and observed green QEMU storage cells.
- Approved filesystem format and crash-consistency/recovery design (OzeroFS remains a proposal).
- Per-CPU TSS/IST before user threads leave the BSP.
- Stage 7–10: PE loader completion, Windows user-mode ABI, graphics compatibility, modern hardware expansion.
- QEMU `sendkey` does not generate observable PS/2 keyboard events; manual testing with `-usb -usbdevice keyboard -display sdl` required for full input acceptance.
