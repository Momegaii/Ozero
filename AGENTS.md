# Repository context for agents

## Project

Ozero (o0) x86-64 kernel, agent A1 (kernel engineering). Current phase: Stage 0 —
contracts only. Do not start subsystem implementation without director approval of the
foundational decisions (see `docs/kernel-architecture.md` §6).

## Layout

- `docs/` — architecture note, boot contract, CPU matrix, interface draft, Stage 1 plan,
  Stage 0 report, source index, A2 review and A3 DMA contract.
- `docs/decisions/a1-global-decisions.md` — A1 proposals in the global decision ID space.
- `include/ozero/` — public interface sketch (freestanding C11). This is the only public
  header root; `include/o0/` is removed (D-009).
- `tests/abi_static_asserts.c` — compile-time ABI checks, owned by A1.
- `tools/check-headers.sh` — conformance script, owned by A1; A4 invokes it from CI
  unmodified (D-010).

## Interface review state (23 November 2026, action brief v2 and proceed briefs R1/R2)

- **IF-008 v1.1: ACCEPT.** A3 published at `b9a69d8`; A1 headers at `30ee4f0`.
  A3's `include/ozero/platform.h` and `block.h` conform; A3's copies of the ten
  A1 headers are byte-identical. A3 **accepted all eight §6 conditions** at
  `307b919`; A4 `b4232a0` records the interface decision. AHCI remains blocked on
  director D-007/D-021. Record: `docs/if008-v1.1-review.md` (D-032).
- **A2 A1-REV-01…12: answered and reconciled.** A2 code `6d305b8`, evidence
  `64b3458`; A1 headers `30ee4f0`. Four Accept, six Change required, two Defer.
  A2's `12` blob changed from `f5d08487…` to `90dbe42c…` only to record A2's
  acceptance and fix stale citations/IDs; the dispositions stand. A2 fixed the
  `90b004f`/`include/o0/` references and the D-009…D-023 collision. A2 must still
  update its A1 head to `74b1471` and record the IF-009 signatures/current pair.
  Record: `docs/a2-contract-review-response.md`.
- **IF-009 v1.0: ACCEPT (A1 and A3).** A2 published at `6d305b8`/`64b3458`;
  contract blob `184b6290…`, manifest blob `f505cb4d…` (unchanged). IF9-A1-1 is
  change required at the kernel-primitive level (no file-backed section at
  `30ee4f0`); IF9-A1-2 accepted with a mapping-lifetime condition; IF9-A1-3
  accepted. A3 accepted v1.0 for the hardware boundary at `307b919`; A4 `b4232a0`
  records both. Truncated headers and the missing DLL-name import case are gaps
  deferred to v1.1, not covered tests. A2 must record both signatures and the
  current pair; XB-003 stays open. Record: `docs/if009-review.md` (D-033).
- **XB-016 raw evidence: published.** `docs/evidence/a1-raw-checks-2026-11-23-r2.txt`
  contains the raw `check-headers.sh` output, header equality, A2 artifact hashes,
  A2 evidence, A3 acceptance and A4 registry. It is not runtime proof.

## Commands

```
./tools/check-headers.sh
```

Expected result: `RESULT: PASS`. It compiles each header standalone, the umbrella header
as C11 and C++17, the static assertions, greps for forbidden device-class terms and hosted
libc headers, and rejects `include/o0/` or legacy `o0/` includes.

## Conventions

- Every architectural claim cites a key from `docs/sources.md` ([SDM-*], [UEFI-*],
  [ACPI-*], [LIMINE-*]). Do not invent section numbers; cite by volume and chapter title
  when unsure.
- Generic kernel interfaces must stay device-neutral. No GPU generation, vendor or
  device-class assumptions in `include/ozero/mm.h`, `irq.h`, `driver.h` or `thread.h`.
- Headers: `o0_` prefix, include guard `O0_<NAME>_H`, `extern "C"`, fixed-width types,
  `o0_status` returns, no errno/NTSTATUS, no hosted libc. Include path is `<ozero/...>`;
  symbols keep the `o0_` prefix.
- The syscall ABI, handle encoding and user-visible error mapping are deliberately
  unfrozen (A2/A4).
- A4 owns repository, build and CI. A1 owns `tools/check-headers.sh` and
  `tests/abi_static_asserts.c`; A4 must invoke the script unmodified. A3 owns other
  content under `tools/` (D-010). A4's `CODEOWNERS` needs file-level overrides for the two
  A1 artifacts.
- Use global decision IDs in reports and cross-agent documents: D-005 and D-007 from A4's
  register, D-009 through D-033 from `docs/decisions/a1-global-decisions.md`. A4 owns the
  canonical register; A1's local D1–D14 map to the global IDs in that file.

## Pinned facts

- Limine base revision 6 is requested, but revision 6 changes only aarch64, riscv64 and
  loongarch64. Every x86-64 entry-state guarantee is a revision-5 guarantee. Do not claim
  otherwise.
- Pinned Limine protocol commit `877c2d4c`, `PROTOCOL.md` SHA-256 `3e702cd9…`, `limine.h`
  SHA-256 `bd2fa78d…`, bootloader `v12.11.1`. Full hashes and retrieval date are in
  `docs/sources.md`.
- The HHDM is partial and may overshoot region boundaries. Use `o0_hhdm_to_virt`; never
  treat `phys + hhdm_offset` as valid for arbitrary physical memory.
- Parked APs run bootloader trampoline code on bootloader stacks. `goto_address` must be
  the original `limine_mp_info`, and reclaim requires AP proof records.

## Repository integration

- Shared source of truth: <https://github.com/Momegaii/Ozero>. A1 works on branch
  `a1/stage0-review` and integrates through pull request #1. Never push to `main`.
- Reports must name commit hashes, changed files, commands actually run, raw results,
  tests not run, unresolved risks and cross-agent dependencies. Never claim Stage 0
  acceptance; that is the director's decision.

## Reporting

Use the briefing's fields: Status, Evidence (commit, files, command, raw output),
Decisions needed, Risks, Next step. Never mark work complete on the basis of a compile;
show the test and its result.
