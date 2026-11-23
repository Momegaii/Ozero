# Director dashboard — Ozero Stage 0

| Field | Value |
| --- | --- |
| Updated | 2026-11-23 |
| Prepared by | A4 |
| Base commit | `5a50c0d` (A4 control plane imported into `Momegaii/Ozero`) |
| Review branch | `a4/stage0-review-followup` (not pushed) |
| Stage | 0 — architecture, integration and QA control plane |
| Stage state | **Submitted for review. Not approved.** |

## At a glance

- The A4 control plane is on the local review branch. The Stage 0 review actions are implemented;
  raw evidence is in `evidence/stage0-review/`.
- A1 published `a1/stage0-review` at `90b004f` (PR #1, draft). A2 published `agent/a2-stage0` at
  `f324b28` (PR #2, draft). A3 has no branch.
- A1's evidence is an inline report, not raw logs. A2's evidence is static-only and tied to
  `8bac650`, not the branch head. Neither branch is integrated.
- The build and QEMU MBR harness are **Tested**. The 512-byte MBR fixture is not the Ozero kernel
  and is never described as kernel boot.
- All Director decisions are **Proposed**. None is approved.
- The A4 review branch cannot be pushed: the authenticated token has no contents write. Independent
  verification needs XB-006 / D-008.

## Stage 0 review actions

| # | Action | Status | Where |
| --- | --- | --- | --- |
| 1 | Verify A1/A2/A3/A4 commits; record graph and clean-tree state | Done. A1–A3 absent; A4 imported. | `evidence/stage0-review/repo-audit.txt` |
| 2 | Rerun checks in a clean environment; keep logs, versions, exit codes | Done at the review commit | `evidence/stage0-review/` |
| 3 | Separate target labels | Done | `tests/targets.tsv`, `docs/process/test-targets.md`, `make targets` |
| 4 | Decision register with Proposed/Approved/Rejected/Deferred and approver | Done; all Proposed | `docs/decisions/decision-log.md` |
| 5 | A1 review of A3 DMA/platform interfaces as a merge gate | Done | IF-008, `CODEOWNERS`, `docs/process/integration-checklist.md` |
| 6 | Safe CI: no privileges, raw disks or Windows binaries | Done and enforced | `docs/process/ci-safety.md`, `scripts/check-ci-safety.sh` |

## Required outputs

| Output | Document | Status | Evidence |
| --- | --- | --- | --- |
| Repository and ownership proposal | [repository-layout.md](../architecture/repository-layout.md), [ownership.md](../architecture/ownership.md), `CODEOWNERS` | **Proposed** | ADR-0001 |
| Build and test plan | [build-and-test.md](../process/build-and-test.md) | **Tested** | `evidence/stage0/`, `evidence/stage0-review/` |
| ADR and interface templates | [adr/template.md](../adr/template.md), [interface.md](../templates/interface.md), [interfaces.md](../architecture/interfaces.md) | **Implemented** | — |
| Integration checklist | [integration-checklist.md](../process/integration-checklist.md) | **Proposed** | ADR-0002 |
| Director dashboard | this page | **Implemented** | — |

## Stage 0 assignment coverage

| # | Assignment | Where | Status |
| --- | --- | --- | --- |
| 1 | Repository layout and CODEOWNERS/ownership map for A1–A4 | `docs/architecture/`, `CODEOWNERS` | Proposed |
| 2 | Branch isolation: one branch/workspace per task, reviewed PR | `docs/process/branching-and-review.md` | Proposed |
| 3 | Reproducible build: pinned toolchain, command, artifacts, debug symbols, clean checkout | `toolchain/`, `scripts/build.sh`, `Makefile` | Tested |
| 4 | QEMU MBR harness smoke test: serial capture, markers, timeout, failure artifacts | `scripts/test-qemu.sh`, `tests/qemu/` | Tested |
| 5 | Decision log template and live register: context, options, recommendation, risks, approver, status | `docs/templates/decision-log.md`, `docs/decisions/decision-log.md` | Implemented |
| 6 | Agent report and stage acceptance templates with commit hashes, tests, evidence | `docs/templates/` | Implemented |
| 7 | Trust-boundary review: isolated workspaces, mounts, secrets, network, privileges | `docs/process/trust-boundaries.md` | Reviewed |

## Acceptance criteria

| # | Criterion | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Fresh checkout builds from documented instructions | Met by A4, awaiting Director | `make verify-clean` in `evidence/stage0-review/clean-checkout.log` |
| 2 | Every task has scope, owner, output, test and evidence | Template ready; no domain tasks yet | `docs/templates/agent-report.md` |
| 3 | Agents work in isolated branches/workspaces; integration reviewed | Model proposed | ADR-0002 |
| 4 | QEMU tests capture logs and fail clearly on missing markers | Met: negative tests recorded | `evidence/stage0-review/negative-*.log` |
| 5 | No unnecessary host, secret or raw-device privileges | Reviewed and enforced in CI | ADR-0004, `docs/process/ci-safety.md` |
| 6 | A4 reports distinguish proposed/implemented/tested/approved | In use | this page |

## Definition of done

| Item | Status | Evidence |
| --- | --- | --- |
| Clean clone reproduces A4 results | Met on this branch | `evidence/stage0-review/clean-checkout.log` |
| Evidence links exact commits and raw outputs | Met | `evidence/stage0-review/README.md` |
| MBR harness never described as kernel boot | Met by naming rule and wording audit | `docs/process/test-targets.md` |
| Director decisions pending until approved | Met: all Proposed | `docs/decisions/decision-log.md` |
| Cross-agent API sign-off precedes dependent implementation | Met by merge gate | `docs/process/integration-checklist.md` |

## Not tested

- UEFI boot
- Limine loading an Ozero kernel
- Ozero kernel entry, memory setup or any kernel subsystem
- Real-hardware boot or device matrix
- PE parsing, loading or Windows compatibility
- Public header ABI
- Any A1 runtime: no kernel, ISO, QEMU boot, AP takeover or DMA execution
- Any A2 Windows execution: PE evidence is static-only, and all 17 corpus entries are `not-run`
- Any A3 code, AHCI probe or device test: no A3 branch exists

## Blockers

| # | Blocker | Owner | Impact | Needed by |
| --- | --- | --- | --- | --- |
| 1 | ADR-0001 to ADR-0004 are unapproved | Project Director | Stage 1 cannot start with agreed ownership or review rules | Before Stage 1 |
| 2 | GitHub team slugs in `CODEOWNERS` are placeholders | Project Director | Review enforcement is not active | Before Stage 1 |
| 3 | Boot protocol is proposed as Limine v12.11.1, base revision 6; Director D-005 is open | A1 → Director | A1 cannot finalise the boot path; the MBR harness is protocol-neutral | Stage 1 design |
| 4 | Branch protection and required CI check are repository settings, not code | Project Director | The merge gate is advisory until enabled | Before Stage 1 |
| 5 | A4 review branch cannot be published | Project Director (XB-006 / D-008) | `git push` and ref creation return 403; the token cannot write contents | Stage 0 review |
| 6 | A1 and A2 branches are not integrated; A3 has no branch | A1, A2, A3, A4 | Cross-agent gates are defined but untested; merge conflicts are known | Stage 1 |
| 7 | Nine cross-agent dependencies are Open | A1, A2, A3, A4 | Dependent work cannot merge without owner sign-off | Stage 1 |
| 8 | A1 uses `include/o0/`; the map and `CODEOWNERS` say `include/ozero/` | A1, A4 → Director | A1 cannot merge without a namespace decision | Before A1 merge |
| 9 | A1 adds `tools/check-headers.sh` under A3-owned `tools/` | A1, A3 | Ownership gate would block the A1 merge | Before A1 merge |

## Cross-agent dependencies

The live register is [`docs/process/cross-agent-blockers.md`](../process/cross-agent-blockers.md).
All rows are **Open**; none is signed.

| ID | Dependency | Owner | State |
| --- | --- | --- | --- |
| XB-001 | A1 reviews the A3 DMA/platform/block interface before AHCI probe | A1 → A3 | Open; A3 has no branch, A1 reviewed its own draft |
| XB-002 | A2 contracts match A1 process/address-space/handle/syscall | A1 → A2 | Open; A2 published requirements, A1 review pending |
| XB-003 | A2 PE fixture contract before any PE test | A2 → A1/A3 | Open; A2 static-only package published |
| XB-004 | A1 pins Limine and the kernel handoff | A1 → Director | Open; v12.11.1 / base 6 pinned, D-005 pending |
| XB-005 | A3 names device tests and hardware requirements with evidence | A3 → A4 | Open; no A3 branch |
| XB-006 | A4 publishes the review branch and PR | A4 → Director | Open; push returns 403 |
| XB-007 | Header namespace `include/o0/` vs `include/ozero/` | A1 → A4 → Director | Open |
| XB-008 | A1's `tools/check-headers.sh` is in A3-owned `tools/` | A1 → A3 | Open |
| XB-009 | A2 evidence cites `8bac650`, head is `f324b28` | A2 → A4 | Open |

## Decisions needed

| ID | Question | Status | Approver | Recommendation |
| --- | --- | --- | --- | --- |
| D-001 | Confirm the A1/A2/A3 domain split and team slugs | **Proposed** | Project Director | Accept the proposed split, then supply real slugs |
| D-002 | Approve branch isolation and the review gate | **Proposed** | Project Director | Approve ADR-0002 and enable protection on `main` |
| D-003 | Approve the Stage 0 build and QEMU MBR harness approach | **Proposed** | Project Director | Approve ADR-0003 |
| D-004 | Approve agent trust boundaries | **Proposed** | Project Director | Approve ADR-0004 |
| D-005 | Choose the boot protocol | **Proposed** | Project Director | A1 to propose; Limine is a candidate only |
| D-006 | Accept Stage 0 | **Proposed** | Project Director | Decide after D-001 to D-004 and D-008 |
| D-007 | Approve the A3 DMA/platform interface and A1 sign-off gate | **Proposed** | Project Director | Approve IF-008 and the merge gate |
| D-008 | Approve publishing the A4 review branch | **Proposed** | Project Director | Push the branch so the Director can fetch it |

## Risks

| Risk | Likelihood | Impact | Mitigation | Owner |
| --- | --- | --- | --- | --- |
| A4 work is unpushed and cannot be fetched | High | Review cannot be independent | XB-006 / D-008; push the branch when a writable credential exists | A4 |
| A1 and A2 branches are not integrated; A3 is absent | High | Cross-agent gates are untested; merge conflicts are known | A4 merge order; A1 namespace and `tools/` fixes; A3 publishes IF-008 | Director, A1, A3 |
| A1's "A3 review signed" is read as A3 acceptance | Medium | False cross-agent sign-off | A4 record: A3 has no branch; XB-001 stays Open | A4 |
| A1 evidence is inline, not raw logs | Medium | Claims are harder to audit | Copy A1 output to `evidence/a1-stage0/` before merge | A1, A4 |
| A2 evidence and branch head differ | Medium | Provenance mismatch | Regenerate at final head; XB-009 | A2 |
| MBR fixture is mistaken for kernel boot | Medium | False boot claim | Naming rule; explicit not-tested list | A4 |
| Domain split does not match real work | Medium | Overlapping trees, merge conflicts | Director confirms before Stage 1 | Director |
| Two agents share a workspace | Medium | Lost work, bad evidence | One branch/workspace per task; clean-checkout test | A4 |
| A test passes without proving anything | Medium | False confidence | Negative tests; evidence tied to commit | A4 |
| Reproducibility silently breaks | Medium | Evidence cannot be trusted | `make verify-repro` in CI, hard failure | A4 |
| CI safety check is bypassed | Low | Host or data exposure | Static check plus review; no opt-in jobs yet | A4 |
| QEMU is treated as proof of real hardware | Medium | Wrong compatibility claims | Documented limitation; hardware is a later stage | A4 |

## Evidence

| Artifact | Path | Status |
| --- | --- | --- |
| Stage 0 build and test log | `evidence/stage0/ci.log` | Recorded at `e12983e` |
| Stage 0 reproducibility log | `evidence/stage0/repro.log` | Recorded at `e12983e` |
| Stage 0 clean-checkout log | `evidence/stage0/clean-checkout.log` | Recorded at `e12983e` |
| Stage 0 QEMU serial and result | `evidence/stage0/qemu/` | Recorded at `e12983e` |
| Stage 0 review report | `evidence/stage0-review/README.md` | Recorded |
| Repository audit | `evidence/stage0-review/repo-audit.txt` | Recorded |
| Review build, boot and repro logs | `evidence/stage0-review/` | Recorded |
| Review negative tests | `evidence/stage0-review/negative-*.log` | Recorded |
| CI safety log | `evidence/stage0-review/ci-safety.log` | Recorded |
| Cross-agent verification | `evidence/stage0-review/cross-agent-verification-2026-11-23.md` | Recorded at `596fc6f` + this commit |

## Agent status

| Agent | Current task | Status | Blocked by |
| --- | --- | --- | --- |
| A1 | Stage 0 review response; PR #1 draft at `90b004f` | Submitted; not integrated | D-005, XB-007, XB-008 |
| A2 | Static-only PE package; PR #2 draft at `f324b28` | Submitted; not integrated | A1 review (XB-002), XB-009 |
| A3 | — | No branch, commit or interface | D-001; must publish IF-008 |
| A4 | Stage 0 review follow-up; local `596fc6f` + this commit | Submitted locally; no PR | XB-006 / D-008 (no contents write) |

## Change log

| Date | Change | Commit |
| --- | --- | --- |
| 2026-11-23 | Stage 0 control plane, build harness and QEMU MBR harness test | `e12983e` (local source), imported as `5a50c0d` |
| 2026-11-23 | Stage 0 review follow-up: targets, decision register, DMA gate, CI safety, evidence | `4396155` (evidence in the next commit) |
| 2026-11-23 | Review evidence: audit, toolchain, CI, repro, clean clone, negative tests | `f990bf7` |
| 2026-11-23 | Cross-agent blocker register; refreshed audit | `596fc6f` |
| 2026-11-23 | A1/A2 branch verification; A3 absence; namespace and ownership findings | this commit |
