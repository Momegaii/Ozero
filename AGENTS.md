# Repository context for agents

## Project

Ozero (o0) x86-64 kernel, agent A1 (kernel engineering). Current phase: Stage 0 —
contracts only. Do not start subsystem implementation without director approval of the
foundational decisions (see `docs/kernel-architecture.md` §6).

## Layout

- `docs/` — architecture note, boot contract, CPU matrix, interface draft, Stage 1 plan,
  Stage 0 report, source index.
- `include/o0/` — public interface sketch (freestanding C11).
- `tests/abi_static_asserts.c` — compile-time ABI checks.
- `tools/check-headers.sh` — the only runnable check today.

## Commands

```
./tools/check-headers.sh
```

Expected result: `RESULT: PASS`. It compiles each header standalone, the umbrella header
as C11 and C++17, the static assertions, and greps for forbidden device-class terms and
hosted libc headers.

## Conventions

- Every architectural claim cites a key from `docs/sources.md` ([SDM-*], [UEFI-*],
  [ACPI-*], [LIMINE-*]). Do not invent section numbers; cite by volume and chapter title
  when unsure.
- Generic kernel interfaces must stay device-neutral. No GPU generation, vendor or
  device-class assumptions in `include/o0/mm.h`, `irq.h`, `driver.h` or `thread.h`.
- Headers: `o0_` prefix, include guard `O0_<NAME>_H`, `extern "C"`, fixed-width types,
  `o0_status` returns, no errno/NTSTATUS, no hosted libc.
- The syscall ABI, handle encoding and user-visible error mapping are deliberately
  unfrozen (A2/A4).
- A4 owns repository, build and CI. `tools/check-headers.sh` is a developer aid, not CI.

## Reporting

Use the briefing's fields: Status, Evidence (commit, files, command, raw output),
Decisions needed, Risks, Next step. Never mark work complete on the basis of a compile;
show the test and its result.
