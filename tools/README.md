# Developer tooling

The initial tooling uses Python's standard library for host-side orchestration only.

- make doctor: checks required host tools and repository structure.
- make check: syntax-checks tooling and runs the readiness check.
- make clean: removes generated local build artifacts.

This is not a kernel compiler or image builder yet. The compiler, linker, assembler, target, boot protocol, and sysroot remain gated by ADR 0001.
