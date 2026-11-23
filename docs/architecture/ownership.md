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
| `include/ozero/` | A1 | A1, A4 | Shared interface surface. Versioned. |
| `include/ozero/platform.h` | A3 | A3, A1 | IF-008 platform interface. A1 review is the merge gate. |
| `include/ozero/block.h` | A3 | A3, A1 | IF-008 block interface. A1 review is the merge gate. |
| `kernel/` | A1 | A1 | Kernel core. |
| `arch/` | A1 | A1 | Architecture-specific code and boot path. |
| `drivers/` | A2 | A2 | Device drivers. |
| `platform/` | A2 | A2 | Platform and board support. |
| `userland/` | A3 | A3 | User-space programs and runtime. |
| `userland/platform/dma/` | A3 | A3, A1 | DMA and platform interfaces. A1 sign-off is a required merge gate (IF-008). |
| `a3/` | A3 | A3, A4 | A3 Stage 0 package on the A4 integration branch. `a3/tools/` is A3-owned; root `tools/check-headers.sh` remains A1-owned (D-010). |
| `tools/` | A3 | A3 | Host-side tooling. |
| `tools/check-headers.sh` | A1 | A1, A4 | Header/ABI check. A1 owns the script; A4 owns CI integration (D-010). |
| `tests/abi_static_asserts.c` | A1 | A1, A4 | ABI static asserts. A1 owns the file; A4 invokes it in CI (D-010). |
| `docs/` | A4 | A4 | Director co-reviews `docs/adr/` and `docs/dashboard/`. |
| `scripts/`, `tests/`, `harness/`, `toolchain/`, `.github/` | A4 | A4 | Build and test control plane. |
| `third_party/` | A4 intake | A4, consuming owner | Pinned version and hash required. |
| `CODEOWNERS`, `AGENTS.md` | A4 | A4, Director | Process changes. |

The public header namespace is `include/ozero/` (D-009). A1's `03f8883` refactor makes it the only
public root, and `tools/check-headers.sh` rejects `include/o0/`. `tools/check-headers.sh` and
`tests/abi_static_asserts.c` are A1-owned; A4 owns CI integration and A3 owns other `tools/` content
(D-010). A1 supplied the exact entries at `30ee4f0`; both files list A1 and A4. The specific
`CODEOWNERS` entries override the `tools/` default.

A3 owns `include/ozero/platform.h` and `include/ozero/block.h` for IF-008, with A1 as the required
reviewer. A1 recorded **ACCEPT** for IF-008 v1.1 at A3 `b9a69d8` in A1 `74b1471`; A3 accepted all
eight §6 conditions at `307b919` (XB-019 Closed). A3 PR [#4](https://github.com/Momegaii/Ozero/pull/4)
adds the package under `a3/` and the root headers on top of A4 `b4232a0`; the final diff is 56 files,
44 under `a3/` and 12 under `include/ozero/`, with no A4 control-plane file changed. `CODEOWNERS`
now lists `/a3/` as A3-owned with A4 co-review. A4 `eaca921` and PR #3 still contain no `a3/` or
root `include/ozero/` paths, so A3 is not integrated (XB-020). On merge, the vendored header
snapshots are not authoritative and must not replace A1's files (XB-017 Closed). The file-level
`CODEOWNERS` entries for the two A3-owned interface files override the directory rule.

`CODEOWNERS` is the enforced copy of this table. If the two disagree, `CODEOWNERS` wins at review
time and this document is a bug.

## Decision rights

| Decision | Decides | Must consult | Approves |
| --- | --- | --- | --- |
| Internal design of a domain subsystem | A1 / A2 / A3 owner | A4 for interface impact | — |
| Shared interface shape and version | A1 (owner of `include/ozero/`) | Consumers A2/A3, A4 | Director for breaking changes |
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
