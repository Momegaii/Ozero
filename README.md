# Ozero

Ozero (o0) is an independent operating-system project: a native OS first, with a layered Windows compatibility environment as a long-term goal.

## Mandatory architecture rule for every agent

**Read [AGENTS.md](AGENTS.md) before doing any work in this repository.** It defines non-negotiable project-wide architecture constraints.

Ozero must define its own native executable format, loader, runtime, process model, ABI and syscall interface. **Do not use Linux as the architectural model or foundation for any of these.** Windows compatibility is a first-class goal and must remain architecturally independent of Linux compatibility infrastructure. Linux may be used as a host development environment; that does not make Linux runtime or executable conventions Ozero design requirements.

## Current milestone

**Stage 4: storage, filesystem and drivers (substrate).**

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
make test      # host unit tests: PMM, slab, scheduler, CPU, block, OzeroFS
make check
make run
```

The first build downloads the pinned Limine v11.4.1 binary release into `build/`, compiles the kernel, and creates `build/ozero.iso`. QEMU displays the serial log in the terminal. Press Ctrl+C to stop it.

For a headless boot assertion:

```sh
bash tools/qemu-smoke.sh
```

For GDB debugging, run `make run-debug`, then connect GDB to `localhost:1234`.

## Serial / debug workflow

The kernel writes all diagnostics to COM1 serial. QEMU forwards COM1 to the terminal (`-serial stdio`).

Boot log markers (in order):

| Marker | Meaning |
|--------|---------|
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

## Honest status

The Stage 3 revision was boot-verified in BIOS-mode QEMU with a smoke test that checks paging, heap, scheduler, kernel-thread execution and worker exit. The Stage 4 revision builds cleanly with `-Werror` and its host test suites pass (block layer 4/4, OzeroFS 9/9, plus the existing suites), and its boot self-test and smoke markers are in place, but the Stage 4 revision has **not** been booted in an emulator in this environment: QEMU is not installed there. The storage evidence covers the RAM disk, not real hardware; there is no ATA/AHCI/NVMe driver yet. None of this proves physical hardware support, UEFI/OVMF support, user-mode isolation, graphics, or Windows compatibility. Check `docs/PROJECT_STATUS.md` and the current smoke-test script for the exact tested configuration; do not claim broader validation than the evidence supports.
