# Architecture decision records

| Field | Value |
| --- | --- |
| Owner | A4 |
| Approver | Project Director |
| Index | `docs/decisions/decision-log.md` |

An ADR records a decision that is expensive to reverse. It is not a design document and not a task
list. One decision per record.

## Lifecycle

`Proposed` → `Accepted` → `Superseded` or `Deprecated`.

A4 writes proposals. The Director sets `Accepted` and adds the approval link. A4 never self-approves
an architecture decision. An accepted ADR can be superseded, but the old record stays in the tree
with a link to its replacement.

## Index

| ADR | Title | Status | Approver |
| --- | --- | --- | --- |
| [0001](0001-repository-layout-and-ownership.md) | Repository layout and ownership | Proposed | Project Director |
| [0002](0002-branch-isolation-and-review-gate.md) | Branch isolation and review gate | Proposed | Project Director |
| [0003](0003-stage0-build-and-qemu-harness.md) | Stage 0 build and QEMU harness | Proposed | Project Director |
| [0004](0004-trust-boundaries.md) | Agent trust boundaries and workspace isolation | Proposed | Project Director |

## Writing one

Copy [`template.md`](template.md). Fill every section. "None" is a valid answer; an empty section is
not. Keep it under two pages.
