# Boot and image area

Ozero uses the approved Limine boot protocol with an ELF64 x86-64 kernel. The pinned Limine binary release is fetched into the ignored `build/` directory during ISO construction; no bootloader binary is committed.

- Configuration: `boot/limine.conf`
- Kernel: `build/kernel.elf`
- ISO: `build/ozero.iso`
- QEMU run: `make run`
- Headless serial smoke test: `bash tools/qemu-smoke.sh`

The ISO contains BIOS and UEFI boot paths. QEMU CI currently exercises the BIOS path. Physical UEFI/BIOS hardware validation remains outstanding.
