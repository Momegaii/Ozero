# Ozero

Ozero (o0) is an independent operating-system project: a native OS first, with a layered Windows compatibility environment as a long-term goal.

## Current milestone

**Stage 1 bring-up: first x86-64 kernel boot path.**

The approved implementation uses Limine + ELF64 and a freestanding C kernel. The first kernel milestone initialises COM1 serial output, validates the Limine base revision, reports the bootloader memory map, and halts safely. It is intentionally not yet a general-purpose or daily-use OS.

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
| `OZERO: boot info populated` | Memory map normalised into `o0_boot_info` |
| `OZERO: PMM initialised` | Physical page-frame allocator ready |
| `OZERO: total memory=<bytes>` | Total usable RAM tracked by PMM |
| `OZERO: free memory=<bytes>` | Currently free pages |
| `OZERO: CPU long_mode=yes pae=yes ...` | CPUID feature detection results |
| `OZERO: page tables built` | 4-level paging tables constructed |
| `OZERO: kernel heap alloc/free OK` | Kernel heap self-test passed |
| `OZERO: PMM alloc/free OK` | Page allocator self-test passed |
| `OZERO: kernel self-test complete` | All self-tests passed |
| `OZERO: memory map entries=N` | N normalised memory regions reported |
| `OZERO: boot success; entering idle halt loop` | Stage 2 bring-up complete |

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

A successful QEMU smoke test proves only that this minimal kernel entered and received the Limine memory map under that emulated configuration. It does not prove physical hardware support, a working memory manager, interrupts, storage, graphics, userspace, or Windows compatibility.
