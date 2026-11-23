# Boot and image area

Bootloader integration and disk-image generation are intentionally gated on approval of ADR 0001.

The first accepted boot path must provide a documented handoff contract, reproducible image generation, serial diagnostics, and a QEMU test path. Do not assume BIOS, UEFI, Limine, GRUB, or a specific protocol until the decision is approved.
