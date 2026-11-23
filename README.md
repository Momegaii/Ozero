# Ozero

Ozero (o0) is an independent operating-system project with two long-term goals:

1. A native operating system with its own kernel and application environment.
2. A Windows compatibility environment, developed in layers and validated against real applications.

The initial architecture target is x86-64. Hardware support is intended to remain vendor-neutral, with practical support added through explicit drivers and testing—not assumed to be automatic.

## Project status

The repository is at **Stage 0: architecture specification and toolchain**. This stage establishes the engineering contract before kernel implementation begins. No bootable kernel or hardware compatibility is claimed yet.

- [Architecture baseline](docs/ARCHITECTURE_BASELINE.md)
- [Development roadmap](docs/ROADMAP.md)
- [Current status and Stage 0 exit criteria](docs/PROJECT_STATUS.md)
- [Engineering and agent workflow](docs/ENGINEERING_WORKFLOW.md)

## Principles

- Build an independent native OS; do not make Linux or Windows the kernel foundation.
- Treat Windows compatibility as a staged ABI and subsystem project, not as merely loading PE files.
- Keep hardware support modular and vendor-neutral.
- Support both HDD and SSD storage through a device-independent block layer and protocol-specific drivers.
- Keep kernel-critical code separate from orchestration and developer tooling.
- Measure performance and compatibility; do not claim them without reproducible tests.

## Development

The exact compiler, linker, assembler, boot protocol, kernel language, and build environment remain explicit Stage 0 decisions. See the architecture baseline before adding implementation assumptions.

Contributions should follow the review and ownership rules in [ENGINEERING_WORKFLOW.md](docs/ENGINEERING_WORKFLOW.md).
