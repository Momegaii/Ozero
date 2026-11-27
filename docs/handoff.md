# Ozero handoff — current state

Single source of truth for "where are we now". Every claim here cites a commit on
`main`; `main` is the only branch of record.

## Line of record

Branch: `main`, synced with `origin/main` at `bc4ad71`.

| Commit | Description |
|--------|-------------|
| `e904fe7` | P0 baseline: boot timing, memory metrics, regression checks |
| `4f641e8` | P1: input acceptance — focus borders, cursor variants, window click routing |
| `49585b2` | P2: Stage 6 acceptance — focus border tests, off-screen fix |
| `173e0f3` | P1 fix: rect-based continuous focus border, 64-bit clipping |
| `bc4ad71` | Merge: P1 focus-border regression fix + P2 Stage 6 acceptance |

## What is in `main` and how it is evidenced

| Area | State | Evidence type |
|------|-------|---------------|
| Stage 1 boot path | implemented | QEMU BIOS + UEFI/OVMF markers in local QEMU boots |
| Stage 2 memory | implemented | build, host tests, QEMU boot |
| Stage 3 processes/threads/scheduler | implemented | host tests, QEMU BIOS boot |
| Stage 4 storage (RAM disk, O0FS draft) | implemented at substrate level | host tests, QEMU smoke |
| Wave 4.2–4.3 PCI storage (virtio-blk, AHCI, NVMe) | implemented and build-verified; no QEMU storage cell observed green | `make kernel` with `-Werror`, `tests/storage_encode_test.c`; host tests only |
| Stage 5 GPU software foundation | closed as software milestone; physical GPU deferred | host tests, QEMU PCI discovery markers |
| Stage 6.1–6.8 graphics/desktop | implemented, host-tested, wired into `kmain`; Phase 6.7 custom O0 desktop UI implemented | host tests; QEMU BIOS + UEFI boot verified; QMP screendump verification passed |
| Hardening Wave 1–3, 5.0, Q | implemented; Wave 1 and Wave 2 complete; Wave 3 implemented and emulator-verified locally | local QEMU matrix, host tests, `make test-sanitize` |

## Wave 1 — verification debt (complete locally)

- Stage 4 closing-fix regressions executed and passing (block 6/6, OzeroFS 10/10, also under ASan/UBSan).
- UEFI/OVMF boot verified. `tools/qemu-smoke.sh` supports `QEMU_FIRMWARE=uefi` and asserts `OZERO: firmware=UEFI` / `OZERO: firmware=BIOS`.
- Local QEMU matrix passes on BIOS and UEFI with `qemu64` and `Haswell-v1` at `-smp 1` (QEMU 8.2.2, 10.0.13, 10.1.0). Emulator evidence only; not physical.

## Wave 2 — single-CPU user mode (implemented and emulator-verified)

ADR 0007 accepted. Per-process page tables from the PMM, ring 3 with TSS/IST, syscall ABI,
usercopy, static ELF64 loader, negative tests, process churn. All four `{bios, uefi} ×
{qemu64, Haswell-v1}` cells pass locally on QEMU 10.0.13. No physical-hardware evidence.

## Wave 3 — SMP and memory hardening (implemented in source, emulator-verified)

AP bring-up through Limine MP handoff, xAPIC IPIs, page/range/full TLB shootdown, I/O APIC
routing, per-CPU runqueues with kernel-thread migration, LAPIC timers on every CPU, SMEP/SMAP,
kernel-stack randomization (KSR), aligned buddy PMM with coalescing checks, bootloader-memory
reclaim. User threads remain BSP-pinned until per-CPU TSS/IST exists. ADRs 0009, 0010, 0011
record the choices. No physical-hardware evidence.

## Wave 4 — storage and filesystem

- Wave 4.1 — PCI, MSI/MSI-X and DMA foundations: implemented. Capability-list parsing, BAR size from all-ones mask, MSI/MSI-X programming and LAPIC vector dispatch, DMA allocator with pinning, host tests. No device driver consumes it yet.
- Waves 4.2/4.3 — PCI storage drivers: implemented and build-verified. Legacy virtio-blk, AHCI (one port), NVMe (one admin plus one I/O queue pair with MSI-X entries). `make kernel` passes with `-Werror` and `tests/storage_encode_test.c` passes. No QEMU storage cell observed green — not evidence until a run is recorded.
- Waves 4.4–4.7 — not started: MBR/GPT partition discovery, crash consistency per ADR 0012, VFS with file descriptors and page cache.

## Wave 5 — platform services and security

- Wave 5.0 — capability handles, budgets and synchronous IPC: implemented and emulator-verified. Host test `tests/cap_test.c` passes under `make test` and `make test-sanitize`. Single-CPU and emulator-only.
- Remaining Wave 5 scope not started: IPC extensions, futex-style primitives, timers, signals; PCI/MSI-X device probe/remove; syscall-argument audit; `mmap` and `spawn` only after their own ADRs.

## Wave 6 — Quality engineering (not started)

Fuzzing, concurrency testing, deterministic replay, logging/backtraces, performance baselines.

## Wave 7 — Graphics (resumed)

Stage 6 is implemented and is not gated behind Wave 7. Stage 5 GPU hardware work
(Haswell engine MMIO, reset, interrupt) remains deferred until the Director authorizes
real-hardware testing. The software renderer remains the default path. No GPU hardware claim
is made; engine reset stays refused and the engine stays quarantined until a verified reset
sequence exists.

## Open items

- Physical-hardware validation for all stages (never run, never claimed).
- Wave 4.4–4.7: partition discovery, crash consistency, VFS page cache.
- Real storage protocol driver (AHCI/NVMe) and observed green QEMU storage cells.
- Approved filesystem format and crash-consistency/recovery design (OzeroFS remains a proposal).
- Per-CPU TSS/IST before user threads leave the BSP.
- Stage 7–10: PE loader completion, Windows user-mode ABI, graphics compatibility, modern hardware expansion.
- QEMU `sendkey` does not generate observable PS/2 keyboard events; manual testing with `-usb -usbdevice keyboard -display sdl` required for full input acceptance.

## Rules that stay in force

- Evidence claims cite a commit on `main`.
- Host, QEMU/CI, Director-observed and physical evidence are reported separately.
- Anything unverifiable stays refused or quarantined, never assumed.