# Ozero Project Status

Last updated: 2026-11-23  
Current stage: **Stage 0 — Architecture specification and toolchain**

## Verified repository baseline

- Repository: `Momegaii/Ozero`
- Default branch: `main`
- Starting head for this development wave: `6c01216ca4946b4d96fc15f3c3b79c65744e5e8e`
- Existing baseline contains the architecture, roadmap, status, and engineering workflow documents.
- No bootable kernel or hardware compatibility is claimed.

## Current work in this wave

- Added a minimal Makefile and Python standard-library readiness tool.
- Added repository hygiene files and source-area boundaries.
- Added GitHub Actions checks for Python tooling syntax and repository readiness.
- Added ADR 0001 comparing x86-64 boot and kernel-language options. The recommendation is provisional and awaits Project Director approval.
- Added a developer quick-start with explicit limitations.

This is a verified source/configuration change once committed. CI execution and local build execution must be reported separately after observed.

## Stage 0 exit criteria

- [ ] Boot strategy and boot protocol selected with rationale and approval.
- [ ] Kernel language and coding standard selected with rationale and approval.
- [ ] Compiler, linker, assembler, sysroot, and host dependencies pinned.
- [ ] Reproducible build environment documented and exercised from a clean checkout.
- [ ] CI performs formatting/static checks and builds the agreed minimal target.
- [x] CI repository/tooling syntax and readiness checks defined.
- [ ] Emulation/debug workflow documented and exercised.
- [ ] Initial physical hardware validation matrix agreed.
- [ ] Native ABI, address-space, interrupt/SMP, and driver-model decisions documented to the level required for Stage 1.
- [ ] Project Director approves the Stage 0 architecture package.

## Immediate next engineering actions

1. Obtain approval for the boot protocol and kernel-language direction in ADR 0001.
2. Pin the freestanding cross-toolchain and linker configuration.
3. Implement the first minimal kernel entry and boot handoff validation.
4. Add a QEMU boot test and debugger/serial workflow.
5. Begin physical hardware validation only after emulation produces repeatable evidence.

## Reporting standard

Reports must separate completed and verified work; tests actually run and their results; incomplete work; blockers; risks; and proposed decisions. Never report a build, test, commit, push, or hardware result that was not verified.
