# Ozero Agent Instructions

These instructions apply to **every human and AI agent** working anywhere in this repository, including A1–A4, coding agents, research agents, reviewers and automated assistants.

## Mandatory first step

**Before inspecting code, proposing architecture, editing files, running commands or making a plan, read this entire file.** Then read the repository `README.md` and the relevant current documents under `docs/`. If an agent cannot read this file, it must stop and ask for the instructions rather than proceed by assumption.

For every new task or resumed session, re-read this file. Do not rely on remembered or cached instructions from an earlier task.

## Non-negotiable architecture boundary: Ozero is not Linux

Ozero is an independent operating system. Linux is **not** the inspiration, reference architecture or design authority for Ozero's runtime or executable architecture.

Agents MUST follow these rules:

1. **Design Ozero's own native execution architecture.** Ozero must define its own native executable format, executable loader, runtime, process and thread model, application environment, ABI and syscall interface.
2. **Do not make Linux conventions foundational.** Do not require ELF, Linux syscalls, Linux process semantics, glibc, Linux userspace conventions or Linux compatibility infrastructure as the basis of native Ozero applications or runtime design.
3. **Keep Windows compatibility independent.** Windows compatibility is a first-class long-term goal. Its architecture must not depend on first implementing Linux compatibility. Research and implement the Windows execution contracts needed for compatibility as a distinct layer, without allowing them to dictate Ozero's native ABI.
4. **Treat external systems as references, not blueprints.** It is acceptable to study Linux, ELF, PE/COFF, Windows, BSD, or other systems to understand technical trade-offs and interoperability. No external system gets to define Ozero's architecture by default. Explain what is borrowed, why it is useful, and how it fits Ozero's own design.
5. **Do not confuse host tools with target architecture.** Developers may use Linux, GCC/Clang, binutils, QEMU, scripts or other host tools to build and test Ozero. This is a development-environment choice only; it does not authorize Linux runtime or ABI assumptions in Ozero.
6. **Do not infer the future user executable format from the current kernel boot format.** The current Limine + ELF64 kernel image is a boot/build implementation detail. It does not decide the format for native Ozero user applications.
7. **Preserve architectural separation.** Keep the Ozero-native loader/runtime/ABI and Windows compatibility layer as distinct, explicitly designed components with clear boundaries. Avoid hidden dependencies that make one contingent on Linux compatibility.

If a proposal would violate any of these rules, **do not implement it**. Flag the conflict, explain the consequences, and present an Ozero-native alternative for review.

## Engineering and research discipline

- Inspect the current branch, code and documentation before making claims or changes. Do not assume old status reports describe the current tree.
- Distinguish verified facts, proposals, assumptions and unknowns.
- Research hardware and platform claims against primary specifications or other reliable sources. Cite sources in research documents.
- Do not claim a build, test, emulator boot or hardware validation passed unless it was actually run and its output inspected. Report the exact environment and coverage.
- Keep changes focused, maintain existing interfaces unless a deliberate change is approved, and update relevant documentation when behavior or architecture changes.
- Do not silently turn an unresolved design question into an architectural decision. Record alternatives, trade-offs and open questions for review.
- Prefer the simplest design that satisfies Ozero's requirements. Avoid importing complexity merely because another OS uses it.
- Preserve the project's approved roadmap and call out conflicts before changing sequence or scope.

## Required response when architecture is unclear

Stop before implementing. State:
- the requirement that is unclear;
- the competing designs;
- how each affects Ozero-native execution and independent Windows compatibility;
- what evidence or decision is needed.

Do not quietly default to Linux or another operating system.
