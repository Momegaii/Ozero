# Decision register

| Field | Value |
| --- | --- |
| Owner | A4 |
| Approver | Project Director |
| Entry format | [`docs/templates/decision-log.md`](../templates/decision-log.md) |
| ADR index | [`docs/adr/README.md`](../adr/README.md) |

The register is the authoritative list of open decisions. A decision is **Approved** only when the
named Director approver records it with a date and a link. Silence is not approval.

## Status vocabulary

| Status | Meaning | Who may set it |
| --- | --- | --- |
| **Proposed** | Written down, awaiting a decision | A4 or the decision owner |
| **Approved** | Director accepted it, with a date and a link | Project Director |
| **Rejected** | Director declined it, with a reason | Project Director |
| **Deferred** | Director postponed it to a stated date or trigger | Project Director |

ADR status uses the same words. An ADR marked "Accepted" in older text maps to **Approved** here;
the register wins.

## Register

| ID | Decision | Status | Owner | Director approver | Raised | Needed by | Link |
| --- | --- | --- | --- | --- | --- | --- | --- |
| D-001 | Confirm the A1/A2/A3 domain split and GitHub team slugs | **Proposed** | A4 | Project Director | 2026-11-23 | Before Stage 1 | ADR-0001 |
| D-002 | Approve branch isolation and the review gate, then protect `main` | **Proposed** | A4 | Project Director | 2026-11-23 | Before Stage 1 | ADR-0002 |
| D-003 | Approve the Stage 0 build and QEMU MBR harness approach | **Proposed** | A4 | Project Director | 2026-11-23 | Stage 0 review | ADR-0003 |
| D-004 | Approve agent trust boundaries and workspace profiles | **Proposed** | A4 | Project Director | 2026-11-23 | Before Stage 1 | ADR-0004 |
| D-005 | Choose the boot protocol and kernel handoff (Limine is a candidate only) | **Proposed** | A1 | Project Director | 2026-11-23 | Stage 1 design | IF-001 |
| D-006 | Accept Stage 0 | **Proposed** | A4 | Project Director | 2026-11-23 | Stage 0 exit | `docs/templates/stage-acceptance.md` |
| D-007 | Approve the A3 DMA/platform interface and the required A1 sign-off gate | **Proposed** | A1 + A3 | Project Director | 2026-11-23 | Before A3 DMA work | IF-008 |
| D-008 | Approve publishing the A4 Stage 0 branch for independent verification | **Approved** | A4 | Project Director | 2026-11-23 | Stage 0 review | [#3](https://github.com/Momegaii/Ozero/pull/3), `evidence/stage0-review/director-review-2026-11-23.md` |
| D-009 | Adopt `include/o0/` as the canonical public header namespace | **Approved** | A4 | Project Director | 2026-11-23 | Before A1 merge | `docs/architecture/ownership.md` |
| D-010 | Assign `tools/check-headers.sh` to A1 with A3 review | **Approved** | A4 | Project Director | 2026-11-23 | Before A1 merge | `CODEOWNERS` |
| D-011 | Enable `main` branch protection and required CI checks | **Proposed** | A4 | Project Director | 2026-11-23 | Before Stage 1 | `evidence/stage0-review/github-settings.txt` |

## Decided

| ID | Decision | Outcome | Date | Approver | Link |
| --- | --- | --- | --- | --- | --- |
| D-008 | Publish the A4 Stage 0 branch | Approved; branch published and PR [#3](https://github.com/Momegaii/Ozero/pull/3) opened | 2026-11-23 | Project Director | `evidence/stage0-review/director-review-2026-11-23.md` |
| D-009 | Canonical public header namespace | Approved: `include/o0/` | 2026-11-23 | Project Director | `docs/architecture/ownership.md` |
| D-010 | Ownership of `tools/check-headers.sh` | Approved: A1 owns, A3 reviews | 2026-11-23 | Project Director | `CODEOWNERS` |

## Rules

- An entry leaves **Proposed** only with a named approver, a date and a link to the approval.
- A **Deferred** entry states the trigger or date that brings it back.
- A **Rejected** entry states the reason, so it is not proposed again unchanged.
- A4 records decisions; A4 does not approve its own architecture or process proposals.
- D-005 belongs to A1. A4 records it because the harness must not pre-empt the boot protocol.
- D-009 and D-010 were resolved under the Director's 2026-11-23 instruction to resolve the namespace
  and ownership conflicts. The linked instruction delegates the choice; the Director can reject the
  specific choice in PR review.
