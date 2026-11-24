# Developer tooling

- `make doctor`: check host build dependencies.
- `make kernel`: compile the freestanding x86-64 ELF64 kernel.
- `make iso`: assemble and BIOS-install a hybrid BIOS/UEFI Limine ISO.
- `make run`: boot the ISO in QEMU with serial output on the terminal.
- `make run-debug`: start QEMU paused with a GDB server on TCP port 1234.
- `make check`: run readiness checks, build the bootable ISO and run the host tests.
- `make test`: host unit tests only, covering the PMM, the slab heap, the scheduler,
  CPU feature rules, the block layer and RAM disk, OzeroFS, PCI/GPU, display, input and
  surface primitives.
- `make test-sanitize`: the same host suites built with AddressSanitizer and
  UndefinedBehaviorSanitizer, with leak detection enabled.
- `tools/qemu-smoke.sh`: boot the ISO headlessly and assert kernel serial milestones,
  including the firmware marker, interrupt delivery, and the Stage 4/5 self-tests.
  Environment: `QEMU_CPU` (default `qemu64`), `QEMU_FIRMWARE=bios|uefi` (UEFI uses OVMF,
  overridable with `QEMU_OVMF_CODE`/`QEMU_OVMF_VARS`), `QEMU_TIMEOUT_SECONDS`, `QEMU_DEBUG_LOG`.
  The PASS line names the firmware, CPU model and emulator version it validated.

Python remains host-side orchestration only; it is not part of kernel execution.
