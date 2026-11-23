# Reporting rules

| Field | Value |
| --- | --- |
| Status | **Implemented** |
| Owner | A4 |
| Approver | Project Director |

## Why this exists

An agent can generate plausible text faster than a human can verify it. The only defence is a
report format that forces verifiable facts: a commit hash, a command, and its real output. A report
without those is a status update, not evidence.

## Status vocabulary

| Status | Required evidence |
| --- | --- |
| Not started | Nothing. |
| Researching | The question being answered and sources consulted. |
| Draft ready | A link to the draft and a list of open questions. |
| Blocked | The blocker, who can unblock it, and what was tried. |
| Submitted for review | Commit hash, files, test command and output. |
| Tested | A passing command with output, plus the commit it ran on. |
| Accepted | Director approval link. A4 cannot set this. |

Never write "done", "complete" or "working" without one of the above and its evidence.

## Required fields

Every agent report uses `docs/templates/agent-report.md` and contains:

1. Agent, task id, branch, commit hash.
2. Scope: what changed and what deliberately did not.
3. Files changed.
4. Tests: exact command, exit code, and the relevant output. Not a summary of the output.
5. Evidence: paths under `evidence/` or CI links.
6. Decisions needed: the question, the options, the recommendation.
7. Risks: what can fail, impact, mitigation.
8. Next step: one concrete action.

## Evidence rules

- Evidence is tied to a commit. If the commit changes, the evidence is stale.
- Logs are copied into `evidence/<stage>/<date>/` and referenced by relative path.
- No secrets, tokens, home directories or host names in evidence.
- A screenshot is not evidence when a log would do.
- "It works" is not evidence. `make test` exiting 0 with the serial log is.

## Distinguishing the four states

| Question | Proposed | Implemented | Tested | Accepted |
| --- | --- | --- | --- | --- |
| Is it written down? | yes | yes | yes | yes |
| Does the code exist at a commit? | maybe | yes | yes | yes |
| Was it run, with output? | no | no | yes | yes |
| Did the Director approve? | no | no | no | yes |

A4 reports use the weakest true state. A document plus a passing test is **Tested**, not
**Accepted**. Only the Director moves an item to **Accepted**.

## Cadence

- Every task ends with an agent report on the PR.
- A4 updates the director dashboard after each merge that changes stage state.
- Blockers are reported immediately, not held for the next update.
- A report that says "no change" is still a report when a task is blocked.
