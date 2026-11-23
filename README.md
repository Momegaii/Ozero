# Ozero (o0) — Stage 0 engineering control plane

This repository is the shared control plane for the four-agent Ozero project. It is owned by
**A4 (Architecture, Integration & QA)** and is deliberately small: it holds process, contracts,
templates, the build harness and the evidence trail. Domain source trees are created by A1–A3 in
their own tasks.

**Stage 0 status: proposal submitted for Director review.** See
[`docs/dashboard/director-dashboard.md`](docs/dashboard/director-dashboard.md) for the exact state
of every deliverable.

## Status vocabulary

Every A4 claim uses one of four words. They are not interchangeable:

| Status | Meaning |
| --- | --- |
| **Proposed** | Written down and open for review. No claim that it works. |
| **Implemented** | Code or document exists in the tree at a stated commit. |
| **Tested** | A named command was run and its output is attached as evidence. |
| **Accepted** | The Project Director approved the stage or decision, with a link to the approval. |

## Quick start

Requirements: `bash`, GNU `as`/`ld`/`objcopy`, GNU `make`, `qemu-system-x86_64`.
Exact and minimum versions are in [`toolchain/toolchain.lock`](toolchain/toolchain.lock).

```sh
make check-toolchain   # verify local tools against the lock file
make build             # build the Stage 0 boot fixture into build/
make test              # boot it in QEMU and check serial markers
make ci                # check + build + test
```

`make test` writes a machine-readable result and the raw serial log to `build/qemu/`.
A failed run exits non-zero and names the missing markers.

## What is here

| Path | Purpose | Owner |
| --- | --- | --- |
| `docs/architecture/` | Repository layout, ownership map, interface registry | A4 |
| `docs/process/` | Branch/review rules, build & test plan, trust boundaries, integration checklist | A4 |
| `docs/adr/` | Architecture decision records (proposals for Director approval) | A4 |
| `docs/templates/` | Agent report, stage acceptance, decision log | A4 |
| `docs/dashboard/` | Director-facing roadmap, blockers, risks, evidence links | A4 |
| `harness/fixture/` | Stage 0 boot fixture used to prove the harness. **Not the Ozero kernel.** | A4 |
| `tests/qemu/` | Expected boot markers | A4 |
| `scripts/` | Build, QEMU test, reproducibility and clean-checkout checks | A4 |
| `toolchain/` | Toolchain lock and container definition | A4 |
| `evidence/` | Recorded test output tied to commits | A4 |
| `include/`, `kernel/`, `arch/`, `drivers/`, `platform/`, `userland/`, `tools/` | Proposed domain trees, created by A1–A3 | A1–A3 |

The boot fixture is a 512-byte BIOS MBR whose only job is to emit serial markers. It exists so the
QEMU harness can be tested before A1 delivers a kernel. It makes no architecture decision and must
not be treated as the Ozero boot path.

## Rules that apply to every agent

1. One task, one branch, one isolated workspace. Never share a working tree with another agent.
2. Every change lands through a reviewed pull request; `main` is protected.
3. No task is "done" without a command, its output, and a commit hash in the agent report.
4. Shared interfaces are owned and versioned; see [`docs/architecture/interfaces.md`](docs/architecture/interfaces.md).
5. No agent receives host, secret or raw-device privileges that the task does not need; see
   [`docs/process/trust-boundaries.md`](docs/process/trust-boundaries.md).

## Scope boundary

A4 owns process, integration and evidence. A1–A3 own technical correctness inside their domains.
A4 proposes; A4 does not silently decide domain questions or grant stage acceptance. Anything
marked **Proposed** needs the Project Director.
