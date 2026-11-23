# Stage acceptance

> A4 prepares this document. The Project Director signs it. A4 does not mark a stage accepted.

## Stage

| Field | Value |
| --- | --- |
| Stage | 0 |
| Prepared by | A4 |
| Date | YYYY-MM-DD |
| Commit under review | `<full sha>` |
| Decision requested | Accept / accept with conditions / reject |

## Acceptance criteria

| # | Criterion | Status | Evidence |
| --- | --- | --- | --- |
| 1 | A fresh checkout follows documented build instructions with no undocumented local state | | `make verify-clean` log |
| 2 | Every agent task has scope, owner, output, test and completion evidence | | agent reports |
| 3 | Agents work in isolated branches/workspaces and integration is reviewed | | branch/PR list |
| 4 | QEMU tests capture logs and fail clearly when expected markers are absent | | negative test log |
| 5 | No agent has unnecessary host, secret or raw-device privileges | | trust-boundary review |
| 6 | A4 reports distinguish proposed, implemented, tested and accepted | | dashboard |

Allowed values: **Met**, **Met with conditions**, **Not met**, **Not applicable**.

## Deliverables

| Deliverable | Status | Link |
| --- | --- | --- |
| Repository and ownership proposal | | `docs/architecture/repository-layout.md` |
| Build and test plan | | `docs/process/build-and-test.md` |
| ADR and interface templates | | `docs/adr/`, `docs/architecture/interfaces.md` |
| Integration checklist | | `docs/process/integration-checklist.md` |
| Director dashboard | | `docs/dashboard/director-dashboard.md` |

## Conditions

List every condition with an owner and a due date. A condition without an owner is not a condition,
it is a wish.

| Condition | Owner | Due |
| --- | --- | --- |

## Risks accepted by this approval

| Risk | Why accepted | Review date |
| --- | --- | --- |

## Director decision

- Decision:
- Name:
- Date:
- Link to approval:
