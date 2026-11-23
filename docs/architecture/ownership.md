# Ownership map

| Field | Value |
| --- | --- |
| Status | **Proposed** (ADR-0001) |
| Owner | A4 |
| Approver | Project Director |

The ownership map answers one question: **who must review a change before it merges?** It does not
answer who is senior. A1–A3 are the technical authority inside their domains. A4 is the process and
integration authority. The Director approves architecture and stage acceptance.

## Directory ownership

| Path | Owner | Required reviewers | Notes |
| --- | --- | --- | --- |
| `include/o0/` | A1 | A1, A4 | Shared interface surface. Versioned. |
| `kernel/` | A1 | A1 | Kernel core. |
| `arch/` | A1 | A1 | Architecture-specific code and boot path. |
| `drivers/` | A2 | A2 | Device drivers. |
| `platform/` | A2 | A2 | Platform and board support. |
| `userland/` | A3 | A3 | User-space programs and runtime. |
| `userland/platform/dma/` | A3 | A3, A1 | DMA and platform interfaces. A1 sign-off is a required merge gate (IF-008). |
| `tools/` | A3 | A3 | Host-side tooling. |
| `tools/check-headers.sh` | A1 | A1, A3 | Header/ABI check. Specific ownership overrides the `tools/` default; A3 reviews host-tool placement (D-010). |
| `docs/` | A4 | A4 | Director co-reviews `docs/adr/` and `docs/dashboard/`. |
| `scripts/`, `tests/`, `harness/`, `toolchain/`, `.github/` | A4 | A4 | Build and test control plane. |
| `third_party/` | A4 intake | A4, consuming owner | Pinned version and hash required. |
| `CODEOWNERS`, `AGENTS.md` | A4 | A4, Director | Process changes. |

The public header namespace is `include/o0/` (D-009). A1's Stage 0 headers already use this path, so
the map follows the implementation instead of forcing a rename. `tools/check-headers.sh` is A1-owned
with A3 review (D-010); the specific `CODEOWNERS` entry overrides the `tools/` default.

`CODEOWNERS` is the enforced copy of this table. If the two disagree, `CODEOWNERS` wins at review
time and this document is a bug.

## Decision rights

| Decision | Decides | Must consult | Approves |
| --- | --- | --- | --- |
| Internal design of a domain subsystem | A1 / A2 / A3 owner | A4 for interface impact | — |
| Shared interface shape and version | A1 (owner of `include/o0/`) | Consumers A2/A3, A4 | Director for breaking changes |
| Boot protocol and kernel entry contract | A1 | A2, A3, A4 | Director |
| Build system, CI, test harness | A4 | A1–A3 | — |
| Merge gate and regression policy | A4 | A1–A3 | — |
| Repository layout and ownership | A4 proposes | A1–A3 | Director |
| Stage acceptance | Director | A4 provides evidence | Director |

## Escalation

1. Owner-to-owner disagreement: write the options in a decision log entry, recommend one, ask A4 to
   schedule a Director decision.
2. Interface conflict: A4 freezes the interface at the last accepted version until the Director
   decides. No silent divergence.
3. Security or trust-boundary concern: A4 blocks the merge and records the concern in the risk
   table. The Director decides whether to accept the risk.

## What A4 will not do

- A4 will not change A1–A3 source to "unblock" a build. A4 files the failure with evidence and asks
  the owner.
- A4 will not approve its own architecture proposals. Proposals stay **Proposed** until the
  Director signs them.
- A4 will not accept a stage on the Director's behalf.
