# Developer tooling

- `make doctor`: check host build dependencies.
- `make kernel`: compile the freestanding x86-64 ELF64 kernel.
- `make iso`: assemble and BIOS-install a hybrid BIOS/UEFI Limine ISO.
- `make run`: boot the ISO in QEMU with serial output on the terminal.
- `make run-debug`: start QEMU paused with a GDB server on TCP port 1234.
- `make check`: run readiness checks, build the bootable ISO and run the host tests.
- `tools/qemu-smoke.sh`: boot the ISO headlessly and assert kernel serial milestones.

Python remains host-side orchestration only; it is not part of kernel execution.
