# Director dashboard — Ozero Stage 0

| Field | Value |
| --- | --- |
| Updated | 2026-11-23 |
| Prepared by | A4 |
| Commit under test | `e12983e` (full hash in `evidence/stage0/README.md`) |
| Stage | 0 — architecture, integration and QA control plane |
| Stage state | **Submitted for review.** Not accepted. |

## At a glance

- Five required outputs exist at the commit above.
- Four ADRs are **Proposed** and need a Director decision before Stage 1 starts.
- The build and QEMU harness are **Tested**; recorded evidence is attached in
  `evidence/stage0/` (see the evidence table below).
- No domain code has been written by A4. A1–A3 have not started.
- No stage acceptance is claimed. Only the Director can accept.

## Required outputs

| Output | Document | Status | Evidence |
| --- | --- | --- | --- |
| Repository and ownership proposal | [repository-layout.md](../architecture/repository-layout.md), [ownership.md](../architecture/ownership.md), `CODEOWNERS` | **Proposed** | ADR-0001 |
| Build and test plan | [build-and-test.md](../process/build-and-test.md) | **Tested** | `evidence/stage0/` |
| ADR and interface templates | [adr/template.md](../adr/template.md), [interface.md](../templates/interface.md), [interfaces.md](../architecture/interfaces.md) | **Implemented** | — |
| Integration checklist | [integration-checklist.md](../process/integration-checklist.md) | **Proposed** | ADR-0002 |
| Director dashboard | this page | **Implemented** | — |

## Stage 0 assignment coverage

| # | Assignment | Where | Status |
| --- | --- | --- | --- |
| 1 | Repository layout and CODEOWNERS/ownership map for A1–A4 | `docs/architecture/`, `CODEOWNERS` | Proposed |
| 2 | Branch isolation: one branch/workspace per task, reviewed PR | `docs/process/branching-and-review.md` | Proposed |
| 3 | Reproducible build: pinned toolchain, command, artifacts, debug symbols, clean checkout | `toolchain/`, `scripts/build.sh`, `Makefile` | Tested |
| 4 | QEMU boot smoke tests: serial capture, markers, timeout, failure artifacts | `scripts/test-qemu.sh`, `tests/qemu/` | Tested |
| 5 | Decision log template: context, options, recommendation, risks, approver, status | `docs/templates/decision-log.md` | Implemented |
| 6 | Agent report and stage acceptance templates with commit hashes, tests, evidence | `docs/templates/` | Implemented |
| 7 | Trust-boundary review: isolated workspaces, mounts, secrets, network, privileges | `docs/process/trust-boundaries.md` | Reviewed |

## Acceptance criteria

| # | Criterion | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Fresh checkout builds from documented instructions | Met by A4, awaiting Director | `make verify-clean` |
| 2 | Every task has scope, owner, output, test and evidence | Template ready; no domain tasks yet | `docs/templates/agent-report.md` |
| 3 | Agents work in isolated branches/workspaces; integration reviewed | Model proposed | ADR-0002 |
| 4 | QEMU tests capture logs and fail clearly on missing markers | Met: negative tests recorded | `evidence/stage0/` |
| 5 | No unnecessary host, secret or raw-device privileges | Reviewed | ADR-0004 |
| 6 | A4 reports distinguish proposed/implemented/tested/accepted | In use | this page |

## Blockers

| # | Blocker | Owner | Impact | Needed by |
| --- | --- | --- | --- | --- |
| 1 | ADR-0001 to ADR-0004 are unapproved | Project Director | Stage 1 cannot start with agreed ownership or review rules | Before Stage 1 |
| 2 | GitHub team slugs in `CODEOWNERS` are placeholders | Project Director | Review enforcement is not active | Before Stage 1 |
| 3 | Boot protocol is undecided; Limine is only a candidate | A1 | A1 cannot finalise the boot path; harness is protocol-neutral by design | Stage 1 design |
| 4 | Branch protection and required CI check are repository settings, not code | Project Director | The merge gate is advisory until enabled | Before Stage 1 |

## Decisions needed

| ID | Question | Recommendation | Why it matters |
| --- | --- | --- | --- |
| D-001 | Confirm the A1/A2/A3 domain split and team slugs | Accept the proposed split, then supply real slugs | Without it, ownership is ambiguous and CODEOWNERS is inert |
| D-002 | Approve branch isolation and the review gate | Accept ADR-0002 and enable protection on `main` | Prevents lost work and unattributable evidence |
| D-003 | Approve the Stage 0 build and QEMU harness approach | Accept ADR-0003 | Confirms the evidence used for Stage 0 acceptance |
| D-004 | Approve agent trust boundaries | Accept ADR-0004 | Sets the workspace, secret and network baseline for all agents |
| D-005 | Choose the boot protocol | A1 to propose; Limine is a candidate only | The harness must not pre-empt this |
| D-006 | Accept Stage 0 | Decide after D-001 to D-004 | Stage acceptance belongs to the Director |

## Risks

| Risk | Likelihood | Impact | Mitigation | Owner |
| --- | --- | --- | --- | --- |
| Domain split does not match real work | Medium | Overlapping trees, merge conflicts | Director confirms before Stage 1 | Director |
| Two agents share a workspace | Medium | Lost work, bad evidence | One branch/workspace per task; clean-checkout test | A4 |
| A test passes without proving anything | Medium | False confidence | Negative tests; evidence tied to commit | A4 |
| Reproducibility silently breaks | Medium | Evidence cannot be trusted | `make verify-repro` in CI, hard failure | A4 |
| Secret or host data reaches a workspace | Low | Disclosure | Least-privilege profiles; no host mounts or docker socket | A4 |
| QEMU is treated as proof of real hardware | Medium | Wrong compatibility claims | Documented limitation; real hardware is a later stage | A4 |

## Evidence

| Artifact | Path | Status |
| --- | --- | --- |
| Build and test log | `evidence/stage0/ci.log` | Recorded |
| Reproducibility log | `evidence/stage0/repro.log` | Recorded |
| Clean-checkout log | `evidence/stage0/clean-checkout.log` | Recorded |
| QEMU serial log | `evidence/stage0/qemu/serial.log` | Recorded |
| QEMU result JSON | `evidence/stage0/qemu/result.json` | Recorded |
| Build manifest | `evidence/stage0/manifest.json` | Recorded |

## Agent status

| Agent | Current task | Status | Blocked by |
| --- | --- | --- | --- |
| A1 | — | Not started | D-001, D-005 |
| A2 | — | Not started | D-001 |
| A3 | — | Not started | D-001 |
| A4 | Stage 0 control plane | Submitted for review | D-001 to D-004 |

## Change log

| Date | Change | Commit |
| --- | --- | --- |
| 2026-11-23 | Stage 0 control plane, build harness and QEMU smoke test | `e12983e` |
