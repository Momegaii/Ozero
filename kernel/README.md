# Kernel implementation area

The kernel source is intentionally not started until ADR 0001 is approved.

Planned first milestone after approval:
- freestanding x86-64 entry point;
- explicit linker layout and boot handoff validation;
- serial diagnostics;
- fatal exception reporting;
- deterministic QEMU launch and debug scripts.

Do not add host-dependent runtime assumptions or claim a bootable image from this placeholder.
