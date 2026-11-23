# Ozero Project Status

Last updated: 2026-11-23  
Current stage: **Stage 0 — Architecture specification and toolchain**

## Verified repository baseline

- Repository: `Momegaii/Ozero`
- Default branch: `main`
- Previous remote head: `147cc86c027cef7cec42525024d26dcccf66d7db` (`Initial commit`)
- Before this Stage 0 documentation update, the tracked tree contained only `README.md`.
- No bootable kernel, compiler configuration, automated test suite, or hardware support was present in the inspected tree.

## Current work

This update establishes the first written project baseline:

- Product direction and architecture constraints.
- The authoritative 11-stage development roadmap.
- Explicit unresolved architecture and toolchain decisions.
- Engineering ownership, review, and agent-isolation rules.

This is documentation and project governance progress. It is not kernel implementation, and it does not establish that any agent has executed work.

## Stage 0 exit criteria

Stage 0 is complete only when all of the following have evidence and approval:

- [ ] Boot strategy and boot protocol selected with rationale.
- [ ] Kernel language and coding standard selected with rationale.
- [ ] Compiler, linker, assembler, sysroot, and host dependencies pinned.
- [ ] Reproducible build environment documented and exercised from a clean checkout.
- [ ] CI performs formatting/static checks and builds the agreed minimal target.
- [ ] Emulation/debug workflow documented and exercised.
- [ ] Initial physical hardware validation matrix agreed.
- [ ] Native ABI, address-space, interrupt/SMP, and driver-model decisions documented to the level required for Stage 1.
- [ ] Project Director approves the Stage 0 architecture package.

## Immediate next engineering actions

1. Evaluate boot options and choose a minimal, testable x86-64 boot path.
2. Compare C and Rust kernel implementation trade-offs against the project's toolchain and driver goals.
3. Pin a cross-compilation toolchain and create a reproducible hello-kernel build.
4. Add CI and a QEMU boot test once the boot path is approved.
5. Submit unresolved decisions for Project Director approval before implementation hardens around them.

## Reporting standard

Reports must separate: completed and verified work; tests actually run and their results; incomplete work; blockers; risks; and proposed decisions. Never report a build, test, commit, push, or hardware result that was not verified.
