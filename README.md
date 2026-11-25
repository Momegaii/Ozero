# Ozero

Ozero (o0) is an independent operating-system project: a native OS first, with a layered Windows compatibility environment as a long-term goal.

## Mandatory architecture rule for every agent

**Read [AGENTS.md](AGENTS.md) before doing any work in this repository.** It defines non-negotiable project-wide architecture constraints.

Ozero must define its own native executable format, loader, runtime, process model, ABI and syscall interface. **Do not use Linux as the architectural model or foundation for any of these.** Windows compatibility is a first-class goal and must remain architecturally independent of Linux compatibility infrastructure. Linux may be used as a host development environment; that does not make Linux runtime or executable conventions Ozero design requirements.

## Current milestone

**Stage 6: Native graphical application environment (active).** Stage 5 is closed as the software-foundation milestone; physical GPU execution and vendor-specific acceleration backends remain future work. Stage 6 begins with display and framebuffer support. Its scope, Phase 6.1 audit, implementation status and acceptance checklist are documented in [docs/stage6.md](docs/stage6.md); the roadmap is in [docs/ROADMAP.md](docs/ROADMAP.md).

The approved implementation uses Limine + ELF64 for the current kernel boot image and a freestanding C kernel. The kernel resolves its physical load address, builds and activates its own page tables (HHDM direct map + 4 KiB kernel-image mappings), runs a slab kernel heap, routes interrupts through a remapped PIC with a 100 Hz PIT tick, and preemptively schedules kernel threads with priorities, sleep, join, spinlocks and a blocking mutex.

On top of that, Stage 4 adds a device-independent block layer with validated device registration and request checking, a RAM disk as the first block driver (with fault injection for tests), and a draft native filesystem, OzeroFS: 4 KiB blocks, checksummed superblock, bitmap allocation, direct and single-indirect file blocks, directory entries, and format/mount/create/read/write/truncate/unlink/mkdir/rmdir/list operations. The filesystem format is a **proposal under review**, not an approved final format: see [docs/o0fs-format.md](docs/o0fs-format.md) and [docs/decisions/0003-stage4-storage.md](docs/decisions/0003-stage4-storage.md).

The current kernel boot-image format does **not** decide or constrain Ozero's future native user-executable format. There is still no user mode, no syscall interface, no real storage protocol driver (ATA/AHCI/NVMe), no partitions and no graphics, and the project is not a general-purpose OS.

## Build and run on Linux

Linux is documented here only as one convenient host development environment. This does not establish Linux as a model for Ozero's runtime, executable format, ABI, process model or application environment.

Install the required host packages (Debian/Ubuntu example):

```sh
sudo apt update
sudo apt install build-essential binutils curl xorriso qemu-system-x86
```

Then:

```sh
git clone https://github.com/Momegaii/Ozero.git
cd Ozero
make doctor
make test           # host unit tests: PMM, slab, scheduler, CPU, block, OzeroFS, ...
make test-sanitize  # the same suites under ASan/UBSan with leak detection
make check
make run
```

The first build downloads the pinned Limine v11.4.1 binary release into `build/`, compiles the kernel, and creates `build/ozero.iso`. QEMU displays the serial log in the terminal. Press Ctrl+C to stop it.

For a headless boot assertion:

```sh
bash tools/qemu-smoke.sh                        # BIOS, qemu64
QEMU_CPU=Haswell-v1 bash tools/qemu-smoke.sh    # BIOS, Haswell CPU model
QEMU_FIRMWARE=uefi bash tools/qemu-smoke.sh     # UEFI via OVMF
```

The UEFI path needs the `ovmf` package (defaults to
`/usr/share/OVMF/OVMF_CODE_4M.fd` and `OVMF_VARS_4M.fd`; override with
`QEMU_OVMF_CODE` / `QEMU_OVMF_VARS`). Both firmware paths assert the full serial marker
set, including the self-describing boot header, `firmware=BIOS`/`UEFI` and
`OZERO: interrupt delivery=`.

To verify the framebuffer pattern independently of a host window:

```sh
python3 tools/framebuffer-check.py --iso build/ozero.iso
```

It boots QEMU, takes a QMP `screendump`, checks the eight bar centres and both
edges, and compares the FNV checksum with the kernel's log marker. `make check`
runs the same check automatically when `qemu-system-x86_64` is installed.

For GDB debugging, run `make run-debug`, then connect GDB to `localhost:1234`.

## Serial / debug workflow

The kernel writes all diagnostics to COM1 serial. QEMU forwards COM1 to the terminal (`-serial stdio`).

Boot log markers (in order):

| Marker | Meaning |
|--------|---------|
| `OZERO: build commit=<sha>` | Build identity compiled from `git rev-parse --short=12 HEAD` |
| `OZERO: cpu vendor=… family=… model=… brand=…` | CPUID identity, including the brand string |
| `OZERO: platform=… hypervisor=… signature=… firmware=BIOS`/`UEFI` | Hypervisor vendor/signature and the firmware that loaded Limine |
| `OZERO: kernel entry reached` | Limine transferred control to `kmain` |
| `OZERO: architecture=x86_64` | CPU mode confirmed |
| `OZERO: boot protocol=Limine` | Bootloader identified |
| `OZERO: Limine base revision accepted` | Bootloader supports revision 6 |
| `OZERO: IDT installed (CPU exceptions 0-31)` | Exception dispatch table active |
| `OZERO: hhdm_offset=<addr>` | Bootloader direct-map offset |
| `OZERO: boot info populated` | Memory map normalised into `o0_boot_info` |
| `OZERO: kernel_phys=<addr> kernel_virt=<addr> size=<bytes>` | Physical load address resolved by page walk |
| `OZERO: PMM initialised` | Physical page-frame allocator ready |
| `OZERO: total memory=<bytes>` | Total usable RAM tracked by PMM |
| `OZERO: free memory=<bytes>` | Currently free pages |
| `OZERO: CPU long_mode=yes pae=yes ...` | CPUID feature detection results |
| `OZERO: page tables built` | 4-level paging tables constructed |
| `OZERO: page tables active` | CR3 switched to the kernel's own tables |
| `OZERO: kernel heap ready` | Slab allocator initialised |
| `OZERO: PIC remapped to vectors 32-47` | Legacy IRQ routing enabled |
| `OZERO: interrupt delivery=lapic-extint` | 8259 output delivered through LAPIC LINT0 ExtINT (or `pic-legacy` where no APIC exists) |
| `OZERO: scheduler initialised` | Scheduler ready (no threads yet) |
| `OZERO: timer IRQ enabled at 100 Hz` | PIT tick feeding the scheduler |
| `OZERO: heap OK total=<bytes> live=<n>` | Slab self-test passed |
| `OZERO: address spaces OK` | Map/protect/unmap self-test passed |
| `OZERO: synchronization OK` | Spinlock and mutex self-test passed |
| `OZERO: memory map entries=N` | N normalised memory regions reported |
| `OZERO: kernel threads created=<n>` | Demo kernel threads queued |
| `OZERO: boot success; starting scheduler` | Boot complete, scheduler takes over |
| `OZERO: worker-a iteration=...` | Scheduler is running kernel threads |
| `OZERO: block layer ready devices=<n>` | Block layer core initialised |
| `OZERO: ramdisk registered name=ram0 blocks=<n>` | RAM disk driver registered |
| `OZERO: o0fs formatted blocks=<n>` | Draft OzeroFS volume written |
| `OZERO: o0fs mounted free blocks=<n> inodes=<n>` | Volume mounted |
| `OZERO: o0fs file OK bytes=<n>` | Create/write/read-back through the filesystem |
| `OZERO: o0fs fault test OK` | Injected storage error reported and cleared |
| `OZERO: storage self-test complete` | Stage 4 boot self-test finished |
| `OZERO: framebuffer bars=8 width=… height=…` | Boot framebuffer accepted and eight bands drawn |
| `OZERO: framebuffer sample bar=N pixel=… expected=…` | Read-back pixel at each bar centre matches the deterministic pattern |
| `OZERO: framebuffer checksum=… expected=…` | FNV checksum over all eight sampled centres |
| `OZERO: user entry OK` | Static init reached ring 3 and issued `debug_write` |
| `OZERO: syscall exit status=0` | Init exited through the native `exit` syscall with status 0 |
| `OZERO: user fault contained vector=…` | A ring-3 fault was contained and the process killed |
| `OZERO: syscall bad pointer rejected` | Bad user pointer returned `-EFAULT` instead of faulting the kernel |
| `OZERO: user churn OK processes=16` | Sixteen spawn/join cycles returned PMM free bytes to baseline |

On exception, `OZERO: EXCEPTION vector=<n> error=<e> rip=<r>` is printed.
For page faults (vector 14), `cr2=<a>` and the fault type
(page-not-present/protection-violation, read/write, user/supervisor) are
decoded. The kernel then halts.

For interactive debugging:

```sh
make run-debug   # QEMU paused, GDB stub on :1234
# in another terminal:
gdb build/kernel.elf
(gdb) target remote localhost:1234
```

Press Ctrl+C to stop QEMU.

## Repository map

- `kernel/`: freestanding kernel and linker layout.
- `arch/x86_64/`: architecture-specific implementation boundary.
- `boot/`: Limine configuration and image integration.
- `third_party/limine/`: official protocol header and license.
- `tools/`: host-side checks and QEMU smoke test.
- `docs/`: architecture, roadmap, governance, decisions and the draft OzeroFS format.
- `AGENTS.md`: mandatory project-wide instructions for every human and AI agent.

## Hardening roadmap

The Project Director paused new GPU/Stage 6 scope and ordered seven hardening waves. Plan,
status and evidence: [docs/hardening-roadmap.md](docs/hardening-roadmap.md). Wave 1
(verification debt) is complete locally: the Stage 4 regressions execute, BIOS and UEFI boots
pass on `qemu64` and `Haswell-v1`, CI has a firmware/CPU matrix plus ASan/UBSan host tests,
and one interrupt-delivery defect was fixed (see
[docs/decisions/0005-lapic-extint-interrupt-delivery.md](docs/decisions/0005-lapic-extint-interrupt-delivery.md)).
Wave 2 (single-CPU user mode) is implemented and emulator-verified. Wave Q records the
Director's separate evidence, adds a self-describing boot header and a framebuffer
screendump check. Under the Director's 2026-11-25 reorder, Wave 3 is SMP and memory hardening:
AP bring-up, xAPIC IPIs, single-page TLB shootdown, the LAPIC scheduler timer and SMEP/SMAP
are implemented and locally emulator-verified, while IOAPIC programming, per-CPU runqueues,
KASLR, a buddy allocator and bootloader reclaim are not started. The earlier capability/IPC
work is recorded as Wave 5.0. Physical hardware and Stage 5/6 GPU work remain paused.

## Honest status

The current revision builds cleanly with `-Werror`, and every host suite passes in the normal
build and under ASan/UBSan: PMM 7/7, slab 5/5, scheduler 5/5, CPU, block 6/6, OzeroFS 10/10,
PCI 1/1, GPU 25/25, display, input, PS/2 and surface. The boot smoke test passes in **BIOS and
UEFI/OVMF** on QEMU 8.2.2 and 10.1.0, for both `qemu64` and `Haswell-v1`: paging, heap,
timer-driven scheduler with worker exit, storage self-test and the GPU software-model
self-tests all reach their markers. UEFI support was verified in Wave 1 (2026-11-24) under
OVMF emulation, not on a physical UEFI machine.

What this does **not** prove: physical hardware support, a real storage controller (the
storage evidence is the RAM disk; there is no ATA/AHCI/NVMe driver), physical user-mode
isolation, graphics beyond the software framebuffer path, GPU engine execution (no register
is written outside the software model), or Windows compatibility. The Director's Windows 8.1
results are recorded separately as Director-observed and are not CI or physical-hardware
evidence. Check
`docs/PROJECT_STATUS.md` and `docs/hardening-roadmap.md` for the exact tested configuration;
do not claim broader validation than the evidence supports.
