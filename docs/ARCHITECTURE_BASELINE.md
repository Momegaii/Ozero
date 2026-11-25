# Ozero Architecture Baseline

Status: **Working baseline — Stage 0**  
Decision authority: Project Director  
Scope: accepted project direction, constraints, and unresolved decisions. This document is not a claim that the architecture is implemented.

## 1. Product direction

Ozero is a hybrid operating-system project:

- **Native side:** an independent kernel, native system services, driver model, and application environment.
- **Compatibility side:** a Windows compatibility environment intended to grow toward broad application compatibility.

The native OS must not depend on a Linux or Windows kernel. Compatibility is a subsystem of Ozero, not the foundation of the native kernel.

## 2. Initial architecture target

- Initial CPU architecture: x86-64.
- Initial hardware validation priority: Intel Haswell (Gen7.5) alongside modern x86-64 systems.
- Vendor neutrality is a design goal. It does not imply automatic support for every CPU, GPU, motherboard, controller, or peripheral.
- Additional CPU architectures, including ARM, require deliberate ports and validation.

The boot firmware interface, boot protocol, kernel language, compiler, linker, and assembler are not frozen by this baseline. They require documented evaluation and Project Director approval before becoming binding implementation choices.

## 3. Windows compatibility scope

“Full Windows compatibility” is a long-term product ambition, not a current capability claim. It must be decomposed and tested in layers:

1. PE/COFF parsing, image mapping, relocations, imports, and loader behavior.
2. Win32 user-mode API surface and observable semantics.
3. Process, thread, handle, synchronization, memory, and I/O behavior expected by applications.
4. Runtime and subsystem support, including relevant .NET and graphics paths.
5. Kernel-mode driver compatibility, only where the architecture and security model make it feasible.
6. Difficult application classes such as anti-cheat, DRM, and software relying on undocumented behavior.

A PE loader alone is not Windows compatibility. Each layer needs a compatibility matrix, tests, and explicit known gaps.

## 4. Hardware and storage principles

- Use a device-independent block layer with separate drivers for storage protocols and controllers.
- HDDs and SSDs are both first-class targets. Do not assume flash-only behavior or hide rotational-media characteristics.
- SATA, NVMe, USB mass storage, and legacy interfaces should be evaluated as separate support targets. NVMe is not a generic SSD interface.
- GPU architecture differences must be respected. Intel Haswell is Gen7.5; Gen8+ per-context PPGTT assumptions must not be applied to Haswell.
- Driver availability, published specifications, firmware behavior, and physical test coverage determine actual device support.

## 5. Performance and correctness

Performance claims require controlled, reproducible comparisons on the same hardware and workload.

Storage measurements should include latency distributions (at least p50, p95, and p99), throughput, queue depth, I/O pattern, and CPU cost. Graphics and gaming measurements should include frame-time distributions, input latency where measurable, and CPU/GPU utilization.

Correctness gates precede optimization: memory safety, isolation, error handling, recovery, and deterministic tests.

## 6. Kernel and tooling boundary

Kernel-critical code must use implementation languages and runtime facilities suitable for low-level systems work. Python is appropriate for build orchestration, test harnesses, diagnostics, benchmarking, and binary-inspection tools; it is not proposed as the kernel's execution language.

The kernel language and toolchain remain open until Stage 0 evaluation is complete.

## 7. Explicitly unresolved decisions

The following are not yet approved and must not be silently assumed:

- UEFI-only versus broader boot strategy and the selected boot protocol.
- Kernel implementation language and coding standard.
- Compiler, linker, assembler, sysroot, and reproducible build strategy.
- Kernel/user address-space layout and ABI details. **Decided for Wave 2** in
  [ADR 0007](decisions/0007-user-abi-syscall-and-executable.md): fixed layout, native error
  model, syscall set and selectors. Physical-hardware validation of the layout is still open.
- Interrupt, timer, SMP, and early platform initialization design.
- Driver model and user/kernel driver boundary.
- Native executable format and system-call ABI. **Decided for Wave 2** in ADR 0007 for the
  first loader subset (static ELF64 container); a fully native executable format remains open.
- Filesystem selection and on-disk format. A Stage 4 draft (OzeroFS) exists for
  testing and is documented in `docs/o0fs-format.md`; it remains a proposal, not an
  approved decision.
- Windows compatibility isolation boundary and initial API milestone.
- CI matrix, emulation strategy, and physical hardware lab baseline.

Each decision should be recorded with alternatives, evidence, consequences, owner, and approval status.
