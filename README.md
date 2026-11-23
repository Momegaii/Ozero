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

## Repository map

- `kernel/`: freestanding kernel and linker layout.
- `arch/x86_64/`: architecture-specific implementation boundary.
- `boot/`: Limine configuration and image integration.
- `third_party/limine/`: official protocol header and license.
- `tools/`: host-side checks and QEMU smoke test.
- `docs/`: architecture, roadmap, governance, and decisions.

## Honest status

A successful QEMU smoke test proves only that this minimal kernel entered and received the Limine memory map under that emulated configuration. It does not prove physical hardware support, a working memory manager, interrupts, storage, graphics, userspace, or Windows compatibility.
