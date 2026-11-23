# Branch isolation and review gate

| Field | Value |
| --- | --- |
| Status | **Proposed** (ADR-0002) |
| Owner | A4 |
| Approver | Project Director |

## The rule

**One task, one branch, one workspace, one reviewed pull request.**

No two agents write to the same working tree at the same time. Two agents in one directory will
overwrite each other's files, interleave index state and produce evidence that cannot be attributed.
Isolation is cheaper than reconciliation.

## Branch model

- `main` is the only long-lived branch and is protected.
- Every task gets `a<N>/<ticket>-<slug>`, for example `a1/boot-001-entry-contract`.
- A branch is created from the current `main` and rebased onto `main` before merge.
- One branch carries one task. If the task grows, split it and open a second branch.
- Branch lifetime target: 72 hours. A longer branch needs a note on the PR explaining why.
- Squash merge only. The resulting `main` history is linear and each commit maps to one PR.

## Workspace isolation

| Mode | When | Isolation |
| --- | --- | --- |
| OpenHands local workspace | Documentation, read-only review | Repository directory only |
| Docker workspace | Build and test work | Fresh container per task, repo mounted read-write, no host paths, no docker socket |
| VM workspace | Kernel boot work, anything that needs privileged operations | Disposable VM, snapshot rollback, no shared clipboard or host mounts |

Rules:

1. Each agent clones its own workspace. Never point two agents at one checkout.
2. Build directories are local to the workspace. `build/` is disposable and never shared.
3. Secrets are injected per task, never committed, never copied into a workspace image.
4. A workspace is destroyed after the task. Evidence is copied out before destruction.
5. If an agent needs the host OS, raw devices or elevated privileges, the task is wrong. Redesign it
   or escalate to the Director.

## Pull request gate

A PR merges only when all of the following are true:

| # | Gate | Evidence |
| --- | --- | --- |
| 1 | Scope stated: task id, owner, one-line outcome | PR description |
| 2 | `CODEOWNERS` reviewer approved | GitHub review |
| 3 | Build passes from a clean checkout | `make verify-clean` log or CI link |
| 4 | Required tests pass | `make test` result attached |
| 5 | No new compiler or linker warnings | build log |
| 6 | Interface changes versioned and registered | `docs/architecture/interfaces.md` diff |
| 7 | Agent report attached | `docs/templates/agent-report.md` |
| 8 | Rollback is a revert with no state migration | PR description |
| 9 | Trust-boundary impact reviewed | `docs/process/trust-boundaries.md` checklist |

A4 reviews process and integration. The domain owner reviews technical correctness. The Director
approves architecture changes and stage acceptance. A reviewer who is also the author cannot be the
only approver.

## Commit rules

- Subject: imperative, under 72 characters, prefixed with the area (`build:`, `docs:`, `harness:`).
- Body: why the change is needed and what was tested. Reference the task id.
- One logical change per commit. Do not mix formatting with behaviour.
- Never commit build outputs, secrets, editor state or generated logs outside `evidence/`.
- Agent commits carry `Co-authored-by: openhands <openhands@all-hands.dev>`.

## Conflict handling

- Rebase your branch on `main`; do not merge `main` into your branch.
- If two PRs touch the same interface, the second one waits. The interface owner decides the order.
- If a force-push is unavoidable on a task branch, announce it on the PR. Never force-push `main`.

## What the Director sees

The dashboard (`docs/dashboard/director-dashboard.md`) lists open PRs, their gate status and the
decisions that are waiting. A PR that is green locally but has no evidence link is not green.
