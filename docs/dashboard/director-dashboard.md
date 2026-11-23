# Director dashboard — Ozero Stage 0

| Field | Value |
| --- | --- |
| Updated | 2026-11-23 |
| Prepared by | A4 |
| Base commit | `5a50c0d` (A4 control plane imported into `Momegaii/Ozero`) |
| Review branch | `a4/stage0-review-followup` (published) |
| Review round | Director integration review, 2026-11-23 |
| Stage | 0 — architecture, integration and QA control plane |
| Stage state | **Submitted for review. Not accepted.** |

## At a glance

- The A4 control plane is published on `a4/stage0-review-followup`. The Stage 0 review actions and
  the Director's 23 Nov A4 actions are implemented; evidence is in `evidence/stage0-review/`.
- A1 published `a1/stage0-review` at `30ee4f0` (PR #1, draft). A2 published `agent/a2-stage0` at
  `807c447` (PR #2, draft); its evidence was regenerated at code revision `50b7853`. A3 has no branch.
- A1's evidence is an inline report, not raw logs (XB-016). A2's evidence is static-only and now
  current, but its review-response document still cites `8bac650` (XB-013). A1 reviewed A2 at the
  older `f324b28` (XB-014). Neither branch is integrated.
- The build and QEMU MBR harness are **Tested**. The 512-byte MBR fixture is not the Ozero kernel
  and is never described as kernel boot.
- D-008, D-009 and D-010 are **Approved** by the 23 Nov Director instruction. The remaining
  decisions are **Proposed**. Stage 0 is **not accepted** (D-006).
- The A4 Stage 0 PR is open: [#3](https://github.com/Momegaii/Ozero/pull/3). `main` is **not
  protected** and has **no required status checks** (XB-012 / D-031).

## Stage 0 review actions

| # | Action | Status | Where |
| --- | --- | --- | --- |
| 1 | Verify A1/A2/A3/A4 commits; record graph and clean-tree state | Done. A1/A2 verified; A3 absent; A4 published. | `evidence/stage0-review/cross-agent-verification-2026-11-23-r2.md` |
| 2 | Rerun checks in a clean environment; keep logs, versions, exit codes | Done at the review commit | `evidence/stage0-review/` |
| 3 | Separate target labels | Done | `tests/targets.tsv`, `docs/process/test-targets.md`, `make targets` |
| 4 | Decision register with Proposed/Approved/Rejected/Deferred and approver | Done; D-008/D-009/D-010 Approved; rest Proposed | `docs/decisions/decision-log.md` |
| 5 | A1 review of A3 DMA/platform interfaces as a merge gate | Done | IF-008, `CODEOWNERS`, `docs/process/integration-checklist.md` |
| 6 | Safe CI: no privileges, raw disks or Windows binaries | Done and enforced | `docs/process/ci-safety.md`, `scripts/check-ci-safety.sh` |

## Director integration review — A4 actions

| # | Action | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Open the A4 Stage 0 review PR | Done: [#3](https://github.com/Momegaii/Ozero/pull/3) | A4 PR against `main` |
| 2 | Refresh the repository audit after A3 is published | Pending A3 | XB-011; audit refreshed for A1/A2/A4 |
| 3 | Verify A1/A2/A3 evidence and commit ancestry | Done for A1/A2; A3 absent | `cross-agent-verification-2026-11-23-r2.md` |
| 4 | Resolve the shared namespace and ownership conflicts | Done | D-009, D-010, `CODEOWNERS`, `ownership.md` |
| 5 | Keep MBR harness results separate from kernel boot | Done | `tests/targets.tsv`, this dashboard, PR body |
| 6 | Confirm branch protection and required CI checks | Confirmed **not enabled** | `github-settings.txt`, D-031 |

## Required outputs

| Output | Document | Status | Evidence |
| --- | --- | --- | --- |
| Repository and ownership proposal | [repository-layout.md](../architecture/repository-layout.md), [ownership.md](../architecture/ownership.md), `CODEOWNERS` | **Proposed**; namespace and tools ownership resolved (D-009/D-010) | ADR-0001 |
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
| Director decisions pending until approved | Met: D-008/D-009/D-010 Approved; D-001–D-007 and D-011–D-031 Proposed | `docs/decisions/decision-log.md` |
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
| 4 | Branch protection and required CI checks are not enabled | Project Director (XB-012 / D-031) | The merge gate is advisory until enabled | Before Stage 1 |
| 5 | A4 Stage 0 PR [#3](https://github.com/Momegaii/Ozero/pull/3) is open; Director review pending | Project Director (XB-010) | Review is in progress; no acceptance is implied | Stage 0 review |
| 6 | A1 and A2 branches are not integrated; A3 has no branch | A1, A2, A3, A4 | Cross-agent gates are defined but untested; merge conflicts are known | Stage 1 |
| 7 | Eight cross-agent dependencies are Open | A1, A2, A3, A4 | Dependent work cannot merge without owner sign-off | Stage 1 |
| 8 | A3 has not published; audit refresh is pending | A3 → A4 (XB-011) | The audit covers A1/A2/A4 only | After A3 publication |
| 9 | A2's review response cites `8bac650`; evidence is at `50b7853` | A2 → A4 (XB-013) | Response provenance is stale until A2 updates the header | Before A2 merge |

## Cross-agent dependencies

The live register is [`docs/process/cross-agent-blockers.md`](../process/cross-agent-blockers.md).
Ten rows are **Open**; XB-006, XB-007, XB-008, XB-009, XB-010 and XB-015 are **Closed**. None is
**Signed**.

| ID | Dependency | Owner | State |
| --- | --- | --- | --- |
| XB-001 | A1 reviews the A3 DMA/platform/block interface before AHCI probe | A1 → A3 | Open; A1 conditions in `docs/a3-dma-contract.md`; A3 has no branch |
| XB-002 | A2 contracts match A1 process/address-space/handle/syscall | A1 → A2 | Open; A1 reviewed `f324b28`; A2 moved to `807c447`; XB-014 re-review |
| XB-003 | A2 PE fixture contract before any PE test | A2 → A1/A3 | Open; A2 static-only package published |
| XB-004 | A1 pins Limine and the kernel handoff | A1 → Director | Open; v12.11.1 / base 6 pinned, D-005 pending |
| XB-005 | A3 names device tests and hardware requirements with evidence | A3 → A4 | Open; no A3 branch |
| XB-006 | A4 publishes the review branch | A4 → Director | Closed at `84fe81a` |
| XB-007 | Public header namespace conflict | A1 → A4 | Closed by D-009: canonical `include/ozero/` |
| XB-008 | A1's `tools/check-headers.sh` is in A3-owned `tools/` | A1 → A3 | Closed by D-010: A1 owns script and ABI asserts; A4 CI; A3 other tools |
| XB-009 | A2 evidence revision mismatch | A2 → A4 | Closed: evidence regenerated at `50b7853`; `807c447` is evidence-only |
| XB-010 | A4 opens the Stage 0 review PR | A4 → Director | Closed: [#3](https://github.com/Momegaii/Ozero/pull/3) |
| XB-011 | A3 publishes so A4 can refresh the audit | A3 → A4 | Open; A3 has no branch |
| XB-012 | Enable `main` protection and required checks | A4 → Director | Open; `protected:false`, rulesets 0 |
| XB-013 | A2 response still cites `8bac650` | A2 → A4 | Open; update response header to `50b7853` / `807c447` |
| XB-014 | A1 review of A2 is at `f324b28` | A1 → A2 | Open; A2 moved to `50b7853` / `807c447`; A1 re-review |
| XB-015 | A1 decision block collided with A4 IDs | A4 → Director | Closed: D-009/D-010 merged; A1 D-011 kept; A4 D-031 |
| XB-016 | A1 command output is inline, not a raw log | A1 → A4 | Open; copy to `evidence/a1-stage0/` before merge |

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
| D-008 | Approve publishing the A4 review branch | **Approved** | Project Director | Published; PR [#3](https://github.com/Momegaii/Ozero/pull/3) open |
| D-009 | Adopt `include/ozero/` as the canonical header namespace | **Approved** | Project Director | Implemented by A1 `03f8883`; `include/o0/` rejected by the check |
| D-010 | Assign `tools/check-headers.sh` and `tests/abi_static_asserts.c` to A1; A4 owns CI integration; A3 owns other `tools/` | **Approved** | Project Director | Implemented in `CODEOWNERS` |
| D-011 | Accept A2's process, address-space, handle and syscall requirements as Stage 7 input | **Proposed** | Project Director | A1 reviewed at `f324b28`; A2 has since moved; re-review pending |
| D-031 | Enable `main` branch protection and required CI checks | **Proposed** | Project Director | Enable protection and require the Stage 0 CI check |
| D-012–D-030 | A1 architecture and A2 contract proposals | **Proposed** | Project Director | Recorded in the decision log; no approval implied |

## Risks

| Risk | Likelihood | Impact | Mitigation | Owner |
| --- | --- | --- | --- | --- |
| `main` is unprotected and has no required checks | High | Any merge can bypass review and CI | D-031 / XB-012; enable protection and require the Stage 0 check | Director |
| A1 and A2 branches are not integrated; A3 is absent | High | Cross-agent gates are untested; merge conflicts are known | A4 merge order; A1 reviews A2 and confirms DMA; A3 publishes IF-008 | Director, A1, A3 |
| A1's "A3 review signed" is read as A3 acceptance | Medium | False cross-agent sign-off | A4 record: A3 has no branch; XB-001 stays Open | A4 |
| A1 evidence is inline, not raw logs | Medium | Claims are harder to audit | Copy A1 output to `evidence/a1-stage0/` before merge | A1, A4 |
| A2 response and evidence revisions differ | Medium | Provenance mismatch | Evidence is at `50b7853`; update the response header; XB-013 | A2 |
| A1's A2 review is at `f324b28`; A2 moved | Medium | A stale review is read as a sign-off | XB-014 re-review or no-change confirmation before XB-002 closes | A1, A2 |
| A3 has not published; audit refresh is pending | Medium | The audit is incomplete | XB-011; refresh immediately after A3 publishes | A3, A4 |
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
| Cross-agent verification | `evidence/stage0-review/cross-agent-verification-2026-11-23.md`, `...-r2.md` | Recorded at `84fe81a` and round 2 |
| Director review and A4 disposition | `evidence/stage0-review/director-review-2026-11-23.md` | Recorded |
| GitHub branch protection and checks | `evidence/stage0-review/github-settings.txt` | Recorded; protection not enabled |
| PR body | `evidence/stage0-review/PR_BODY.md` | Recorded |
| Stage 0 review PR | <https://github.com/Momegaii/Ozero/pull/3> | Open; not accepted |

## Agent status

| Agent | Current task | Status | Blocked by |
| --- | --- | --- | --- |
| A1 | Second-round response; PR #1 draft at `30ee4f0` | Submitted; not integrated | D-005, XB-001, XB-002, XB-014, XB-016 |
| A2 | Static-only PE package; PR #2 draft at `807c447`; evidence at `50b7853` | Submitted; not integrated | A1 review (XB-002/XB-014), XB-013 |
| A3 | — | No branch, commit or interface | D-001; must publish IF-008; XB-005, XB-011 |
| A4 | Stage 0 review follow-up; PR [#3](https://github.com/Momegaii/Ozero/pull/3); head `210653c` + documentation and audit commits | Submitted; awaiting review | Director review |

## Change log

| Date | Change | Commit |
| --- | --- | --- |
| 2026-11-23 | Stage 0 control plane, build harness and QEMU MBR harness test | `e12983e` (local source), imported as `5a50c0d` |
| 2026-11-23 | Stage 0 review follow-up: targets, decision register, DMA gate, CI safety, evidence | `4396155` (evidence in the next commit) |
| 2026-11-23 | Review evidence: audit, toolchain, CI, repro, clean clone, negative tests | `f990bf7` |
| 2026-11-23 | Cross-agent blocker register; refreshed audit | `596fc6f` |
| 2026-11-23 | A1/A2 branch verification; A3 absence; namespace and ownership findings | `84fe81a` |
| 2026-11-23 | A4 branch published to `origin`; upstream set | `84fe81a` |
| 2026-11-23 | Director round 2: namespace D-009, ownership D-010, settings D-031, cross-agent r2 | `19dac9d` |
| 2026-11-23 | A2 evidence re-verified at `807c447`; XB-009 closed, XB-013 opened | `076ed82` |
| 2026-11-23 | A4 Stage 0 review PR [#3](https://github.com/Momegaii/Ozero/pull/3) opened against `main` | `9ef13bd` |
| 2026-11-23 | A1 second-round response verified at `115a7f4`; namespace aligned to `include/ozero/`; D-009/D-010 merged; D-031 renumber | `210653c` |
| 2026-11-23 | A1 supplied exact `CODEOWNERS` overrides at `30ee4f0`; A4 applied both owners | this review commit |
| 2026-11-23 | Repository audit refreshed after A1's second-round commits | this audit commit |
