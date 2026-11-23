# Decision log

| Field | Value |
| --- | --- |
| Owner | A4 |
| Approver | Project Director |

The live log of decisions that need a named approver. Entry format:
[`docs/templates/decision-log.md`](../templates/decision-log.md). Decisions that are expensive to
reverse are written as ADRs under [`docs/adr/`](../adr/) and referenced here.

## Open decisions

| ID | Decision | Status | Owner | Approver | Needed by | Link |
| --- | --- | --- | --- | --- | --- | --- |
| D-001 | Confirm the A1/A2/A3 domain split and GitHub team slugs | Proposed | A4 | Project Director | Before Stage 1 | ADR-0001 |
| D-002 | Approve branch isolation and the review gate, then protect `main` | Proposed | A4 | Project Director | Before Stage 1 | ADR-0002 |
| D-003 | Approve the Stage 0 build and QEMU harness approach | Proposed | A4 | Project Director | Stage 0 acceptance | ADR-0003 |
| D-004 | Approve agent trust boundaries and workspace profiles | Proposed | A4 | Project Director | Before Stage 1 | ADR-0004 |
| D-005 | Choose the boot protocol (Limine is a candidate, not approved) | Proposed | A1 | Project Director | Stage 1 design | — |
| D-006 | Confirm the Stage 0 acceptance evidence and sign off | Proposed | A4 | Project Director | Stage 0 exit | `docs/templates/stage-acceptance.md` |

## Decided

| ID | Decision | Outcome | Date | Approver | Link |
| --- | --- | --- | --- | --- | --- |
| — | — | — | — | — | — |

## Notes

- D-005 belongs to A1. A4 records it here because the harness must not pre-empt it.
- An entry leaves "Open" only with a named approver and a date. Silence is not approval.
