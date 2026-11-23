# ADR-0002: Branch isolation and review gate

| Field | Value |
| --- | --- |
| Status | Proposed |
| Date | 2026-11-23 |
| Owner | A4 |
| Approver | Project Director |
| Supersedes | — |
| Superseded by | — |
| Related | ADR-0001, `docs/process/integration-checklist.md` |

## Context

Multiple agents working in one checkout will overwrite files, corrupt the index and produce test
evidence that cannot be attributed to a commit. The project also needs a merge gate that is the same
for every agent, including A4.

## Decision

We will use one branch and one isolated workspace per task, with a reviewed squash-merged pull
request into a protected `main`:

- Branch name `a<N>/<ticket>-<slug>`.
- No two agents share a working tree. Docker or VM workspace for build and boot work.
- `CODEOWNERS` approval plus the integration checklist before merge.
- Linear history; revert is the rollback mechanism.
- A red build or a failing regression blocks merge, with no exception for A4.

Details are in `docs/process/branching-and-review.md` and
`docs/process/integration-checklist.md`.

## Options considered

| Option | Pros | Cons | Verdict |
| --- | --- | --- | --- |
| Shared working tree, direct commits | Fast | Lost work, unattributable evidence, races | Rejected |
| Long-lived per-agent branches | Fewer merges | Divergence, integration pain, stale interfaces | Rejected |
| One branch per task, reviewed PR | Isolated, auditable, reversible | More process per change | **Chosen** |

## Consequences

- Positive: every commit on `main` has an owner, a test and a rollback.
- Negative: small changes carry PR overhead. That overhead is the point.
- Follow-up: protect `main` in GitHub, create the team slugs, require the CI check.

## Risks

| Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- |
| Agents reuse a branch across tasks | Medium | Mixed evidence, hard review | One task per branch; A4 checks PR scope |
| Workspace reuse leaks build state | Medium | False green builds | `make verify-clean` from a fresh clone |
| Review becomes rubber-stamping | Medium | Gate is cosmetic | Checklist requires command output, not opinion |

## Evidence

- `docs/process/branching-and-review.md`
- `docs/process/integration-checklist.md`
- `CODEOWNERS`

Status is **Proposed**. Branch protection is a repository setting that the Director must enable.

## Approval

- Approved by:
- Date:
- Link:
