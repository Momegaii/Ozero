# Ozero

Ozero (o0) is an independent operating-system project: a native OS first, with a layered Windows compatibility environment as a long-term goal.

## Current milestone

**Stage 3: processes, threads and scheduler (kernel threads only).**

The approved implementation uses Limine + ELF64 and a freestanding C kernel. The kernel now
resolves its physical load address, builds and activates its own page tables (HHDM direct
map + 2 MiB kernel mapping), runs a slab kernel heap, routes interrupts through a remapped
PIC with a 100 Hz PIT tick, and preemptively schedules kernel threads with priorities,
sleep, join, spinlocks and a blocking mutex. There is still no user mode, no syscall
interface, no storage and no graphics, and the project is not a general-purpose OS.

## Build and run on Linux

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
make test      # host unit tests: PMM, slab allocator, scheduler
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

The kernel writes all diagnostics to COM1 serial. QEMU forwards COM1 to the
terminal (`-serial stdio`).

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
- `docs/`: architecture, roadmap, governance, and decisions.

## Honest status

A successful QEMU smoke test proves only that this kernel boots, initialises its memory
manager, timer and scheduler, and runs its own self-tests under that emulated
configuration. It does not prove physical hardware support, user-mode isolation, storage,
graphics, or Windows compatibility. The Stage 3 change set was produced in an environment
without QEMU, so its boot path is unverified until CI or a machine with an emulator runs
it; see `docs/PROJECT_STATUS.md`.
