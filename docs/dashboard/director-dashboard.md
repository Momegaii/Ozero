# Director dashboard — Ozero Stage 0

| Field | Value |
| --- | --- |
| Updated | 2026-11-23 |
| Prepared by | A4 |
| Base commit | `5a50c0d` (A4 control plane imported into `Momegaii/Ozero`) |
| Current head | `44d566e` (A3 integration merge) plus this takeover commit |
| Review branch | `a4/stage0-review-followup` (published) |
| Review round | Sole-agent takeover, 2026-11-23 |
| Stage | 0 — architecture, integration and QA control plane |
| Stage state | **Submitted for review. Not accepted.** |

## At a glance

- A4 is the sole active agent under the 23 Nov takeover order. It recovered A1/A2/A3 work from
  their branches and PRs, preserved authorship, and consolidated the candidate on
  `a4/stage0-review-followup`.
- Current refs: A1 `2693a34` (PR #1, draft), A2 `d83cfbe` with code `f6ed1d8` and evidence
  `d83cfbe` (PR #2, draft), A3 PR [#4](https://github.com/Momegaii/Ozero/pull/4) at `48d434f`,
  A4 `44d566e` (PR #3, open, non-draft), `main` `147cc86`.
- A3 PR #4 is **integrated into the A4 candidate** as merge commit `44d566e`. The actual tree has
  44 files under `a3/` and 12 under `include/ozero/`; no A4 control-plane file changed. A1's ten
  headers are byte-identical to `74b1471`; A3's two IF-008 headers match its package. PR #4 is
  still open on GitHub and is **not** merged to `main`. XB-020 is **Closed**.
- A1 recorded **ACCEPT** for IF-008 v1.1 at A3 `b9a69d8` (A1 `74b1471`). A3 accepted all eight §6
  conditions at `307b919`. D-007 and D-021 remain Proposed; the AHCI probe stays blocked.
- A1 answered `A1-REV-01…12` (4 Accept, 6 Change required, 2 Defer). A2 accepts all 12 at `6d305b8`;
  XB-002 is **Signed**, but D-011 remains Proposed and no ABI is frozen.
- IF-009 v1.0 is **Signed**: A1 **ACCEPT**ed with conditions at `74b1471` (`a17d126`), A3
  **ACCEPT**ed the hardware boundary at `307b919`, and A2 recorded its signature at `6d305b8`
  (contract `d02598a`). XB-003 is **Closed**. A4 reconciled the A2 ledger at code `f6ed1d8` /
  evidence `d83cfbe` in `docs/architecture/a2-review-ledger-reconciled.md`; XB-013 is **Closed**.
- IF-009 v1.1 is **not signed and not implemented**. Truncated-header and missing-DLL-name negative
  cases are carried in `docs/architecture/if-009-v1.1-disposition.md`; XB-021 is **Open**.
- A1 raw logs are committed under `evidence/a1-stage0/` and independently re-verified at A1
  `2693a34`; `./tools/check-headers.sh` ends `RESULT: PASS`, exit 0. XB-016 is **Closed**.
- Blocker snapshot: **4 Open, 16 Closed, 1 Signed**. Open: XB-004, XB-005, XB-012, XB-021.
  Signed: XB-002.
- The GitHub job named `build-and-boot` runs `make test-mbr-harness`: an MBR fixture/harness smoke,
  not kernel boot. The 512-byte MBR fixture is not the Ozero kernel.
- D-008, D-009 and D-010 are **Approved**. D-006, D-007, D-021 and D-031 are **Proposed**. Stage 0
  is **not accepted** (D-006).
- The A4 Stage 0 PR is open: [#3](https://github.com/Momegaii/Ozero/pull/3), CI `build-and-boot`
  successful. `main` is **not protected**, has **no required status checks**, and has **no
  `CODEOWNERS`** (XB-012 / D-031).

## Stage 0 review actions

| # | Action | Status | Where |
| --- | --- | --- | --- |
| 1 | Verify A1/A2/A3/A4 commits; record graph and clean-tree state | Done. A1 `2693a34`, A2 `d83cfbe` / code `f6ed1d8`, A3 PR #4 `48d434f`, A4 `44d566e`; ancestry to `main` verified; A3 integrated into A4. | `evidence/stage0-review/a3-integration-verification-2026-11-23.md` |
| 2 | Rerun checks in a clean environment; keep logs, versions, exit codes | Done at the review commit | `evidence/stage0-review/` |
| 3 | Separate target labels | Done | `tests/targets.tsv`, `docs/process/test-targets.md`, `make targets` |
| 4 | Decision register with Proposed/Approved/Rejected/Deferred and approver | Done; D-008/D-009/D-010 Approved; rest Proposed | `docs/decisions/decision-log.md` |
| 5 | A1 review of A3 DMA/platform interfaces as a merge gate | A1 **ACCEPT** at `b9a69d8`; A3 accepted §6 at `307b919`; D-007/D-021 Proposed; AHCI blocked | IF-008, `CODEOWNERS`, `docs/process/integration-checklist.md` |
| 6 | Safe CI: no privileges, raw disks or Windows binaries | Done and enforced | `docs/process/ci-safety.md`, `scripts/check-ci-safety.sh` |

## R2 proceed brief — A4 actions (historical)

| # | Action | Status at R2 | Evidence |
| --- | --- | --- | --- |
| 1 | Refresh branch/PR refs; record A1 `74b1471`, A2 `64b3458` (brief `3f78477`), A3 `307b919`, A4 `b4232a0`, `main` `147cc86` | Done | `cross-agent-verification-2026-11-23-r5.md`, `cross-agent-commands-2026-11-23-r5.log` |
| 2 | Correct the A3 integration claim: A3 is based on A4 `3ae7aca`, not in A4 `b4232a0` or PR #3 | Done; XB-020 Open | A4 tree and PR #3 file list have no `a3/` or root `include/ozero/` |
| 3 | Re-evaluate XB-001/002/003/005/012/013/014/016/017/018 and add XB-020 | Done | `docs/process/cross-agent-blockers.md`; snapshot 7 Open, 12 Closed, 1 Signed |
| 4 | Correct XB-003: A1 accepts IF-009 at `74b1471`; A3 accepts hardware boundary at `307b919`; A2 stale | Done; XB-003 stays Open | A1 `docs/if009-review.md`; A3 `IF-009-a3-review.md`; A2 `14-review-ledger.md` |
| 5 | Recompute A4 CI and PR state at `b4232a0` | Done: `build-and-boot` pass, 4m55s | run 45180803273 |
| 6 | Request Director enforcement of `main` protection and required checks | Requested; not enabled | `protected:false`, rulesets `[]`, `CODEOWNERS` 404; XB-012 / D-031 |
| 7 | Preserve labels: CI `build-and-boot` runs `make test-mbr-harness`; static checks are not CI-verified tests | Done | This dashboard; `tests/targets.tsv`; `stage0.yml` |

## R3 proceed brief — A4 actions (historical)

| # | Action | Status at R3 | Evidence |
| --- | --- | --- | --- |
| 1 | Review PR #4 as an integration PR; verify path scope, ownership and no control-plane overwrite | Done: 56 files, 44 `a3/` + 12 `include/ozero/`; no A4 control-plane changes; clean merge test from `eaca921` | `cross-agent-verification-2026-11-23-r6.md`; `cross-agent-commands-2026-11-23-r6.log` |
| 2 | Integrate PR #4 only if safe and authorized by the branch process | **Not merged at R3**: D-007/D-021 Proposed; no Director authorization | XB-020; integration checklist |
| 3 | Refresh blockers, dashboard, PR #3 body, verification report and evidence at the new A4 head | Done at R3; snapshot **5 Open, 14 Closed, 1 Signed** | `cross-agent-verification-2026-11-23-r6.md`; this dashboard |
| 4 | Reconcile A2 IF-009 contract/ledger; verify current code/evidence pair | IF-009 v1.0 **Signed**; XB-003 **Closed**; ledger still named `6d305b8`/`64b3458`; XB-013 **Open at R3** | A2 `d83cfbe`; `a2/docs/evidence/*` |
| 5 | Resolve XB-016 only after A1 raw logs are committed and verified | **Closed**: `docs/evidence/a1-raw-checks-2026-11-23-r2.txt` at A1 `2693a34` | A1 `9cda79c`, `2693a34` |
| 6 | Keep XB-012 open until `main` protection, rulesets, required checks and `CODEOWNERS` are configured | Open; `protected:false`, rulesets `[]`, `CODEOWNERS` 404 | `github-settings.txt`; D-031 |
| 7 | Keep D-006, D-007, D-021 and D-031 Proposed; do not treat MBR CI as kernel boot | Done | Decision log; test-target labels |

## Sole-agent takeover — A4 actions

| # | Action | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Establish live baseline for `main`, PR #1–#4, branch heads and workflow runs | Done: `main` `147cc86`; A1 `2693a34`; A2 `d83cfbe`; A3 `48d434f`; A4 `a3a67ab` before takeover | `evidence/stage0-review/repo-audit.txt`; this dashboard |
| 2 | Recover A1 raw evidence and commit it under `evidence/a1-stage0/` | Done: raw log copied verbatim; A4 re-ran `./tools/check-headers.sh` at `2693a34`; `RESULT: PASS`, exit 0 | `evidence/a1-stage0/` |
| 3 | Reconcile A2 IF-009 contract, ledger, evidence pair and signatures | Done in the candidate: code `f6ed1d8`, evidence `d83cfbe`, contract `d02598a`, manifest `f505cb4d…`; XB-013 **Closed** | `docs/architecture/a2-review-ledger-reconciled.md` |
| 4 | Carry truncated-header and missing-DLL-name cases into IF-009 v1.1 | Done: six required negative cases, L0-only, A1/A3 re-sign; XB-021 **Open** | `docs/architecture/if-009-v1.1-disposition.md` |
| 5 | Integrate A3 PR #4 into the A4 candidate and produce a file-level ownership map | Done: merge `44d566e`; 44 `a3/` + 12 `include/ozero/`; no A4 control-plane change; XB-020 **Closed** | `docs/architecture/a3-integration-map.md`; `a3-integration-verification-2026-11-23.md` |
| 6 | Re-run A3 and A4 checks on the integrated tree | Done: `make -C a3 check` 19 tests OK; `check-a1-headers` exit 0; `make ci` `ci: ok` | `a3-integration-checks-2026-11-23.log` |
| 7 | Keep Stage 0, D-006, D-007, D-021 and D-031 unapproved; no merge to `main` | Done: all remain **Proposed**; `main` unchanged | Decision log; this dashboard |

## Director integration review — A4 actions

| # | Action | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Open the A4 Stage 0 review PR | Done: [#3](https://github.com/Momegaii/Ozero/pull/3) | A4 PR against `main` |
| 2 | Refresh the repository audit after A3 is published | Done: A3 PR #4 `48d434f` reviewed and integrated at `44d566e`; XB-011 and XB-020 Closed | `a3-integration-verification-2026-11-23.md`, `repo-audit.txt` |
| 3 | Verify A1/A2/A3 evidence and commit ancestry | Done: A1 `2693a34`, A2 `d83cfbe` / `f6ed1d8`, A3 PR #4 `48d434f`, A4 `44d566e`; all descend from `main`; A3 integrated into A4 | `a3-integration-verification-2026-11-23.md` |
| 4 | Resolve the shared namespace and ownership conflicts | Done | D-009, D-010, file-level `CODEOWNERS`, `ownership.md` |
| 5 | Keep MBR harness results separate from kernel boot | Done: CI `build-and-boot` runs `make test-mbr-harness`; this is not kernel boot | `tests/targets.tsv`, this dashboard, PR body |
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
- Any A1 runtime: no kernel, ISO, QEMU boot, AP takeover or DMA execution. A1's IF-008 ACCEPT is an interface decision only.
- Any A2 Windows execution: PE evidence is static-only, and all 17 corpus entries are `not-run`. A2 `make check-static` passes 228 docs checks and 61 structure checks at `d83cfbe`, but no PE loads.
- Any A3 driver, AHCI probe, DMA transfer, GPU execution or real hardware test: A3 is static-only;
  `make -C a3 check` passes 19 tests on the integrated tree, but no device test ran.
- A3 integration is proven only by file scope, header identity and static checks. It is not a
  hardware or runtime validation.
- IF-009 v1.1 negative cases: no truncated-header or missing-DLL-name case is implemented.

## Blockers

| # | Blocker | Owner | Impact | Needed by |
| --- | --- | --- | --- | --- |
| 1 | ADR-0001 to ADR-0004 are unapproved | Project Director | Stage 1 cannot start with agreed ownership or review rules | Before Stage 1 |
| 2 | GitHub team slugs in `CODEOWNERS` are placeholders | Project Director | Review enforcement is not active | Before Stage 1 |
| 3 | Boot protocol is proposed as Limine v12.11.1, base revision 6; Director D-005 is open | A1 → Director | A1 cannot finalise the boot path; the MBR harness is protocol-neutral | Stage 1 design |
| 4 | Branch protection and required CI checks are not enabled | Project Director (XB-012 / D-031) | The merge gate is advisory until enabled | Before Stage 1 |
| 5 | A4 Stage 0 PR [#3](https://github.com/Momegaii/Ozero/pull/3) is open; Director review pending | Project Director (XB-010) | Review is in progress; no acceptance is implied | Stage 0 review |
| 6 | A1 and A2 are not integrated; A3 is integrated at `44d566e` | A1, A2, A4 | A1/A2 cross-agent gates are still untested in the candidate; A3 is static-only | Stage 1 |
| 7 | Four cross-agent dependencies are Open; one is Signed | A1, A2, A3, A4 | Dependent work cannot merge without owner sign-off | Stage 1 |
| 8 | D-007 and D-021 are Proposed | Project Director | The AHCI probe stays blocked even though A1 and A3 accepted the interface and its conditions | Before A3 DMA work |
| 9 | IF-009 v1.1 negative cases are not implemented | A2 → A1/A3 (XB-021) | Truncated headers and missing DLL names remain unverified; v1.0 is still signed | Before a complete IF-009 claim |
| 10 | A3 is static-only; no AHCI, DMA, GPU or hardware test exists | A3 → A4 (XB-005) | Integration is proven, but hardware behavior is not | Before A3 hardware work |

## Cross-agent dependencies

The live register is [`docs/process/cross-agent-blockers.md`](../process/cross-agent-blockers.md).
Four rows are **Open** (XB-004, XB-005, XB-012, XB-021), sixteen are **Closed**, and XB-002 is
**Signed**.

| ID | Dependency | Owner | State |
| --- | --- | --- | --- |
| XB-001 | A1 reviews the A3 DMA/platform/block interface before AHCI probe | A1 → A3 | Closed; A1 ACCEPT for IF-008 v1.1 at `b9a69d8` in `74b1471`; A3 accepted §6 |
| XB-002 | A2 contracts match A1 process/address-space/handle/syscall | A1 → A2 | Signed; A1 answered all 12; A2 accepts all 12 at `6d305b8`; current pair `f6ed1d8` / `d83cfbe`; no ABI freeze |
| XB-003 | A2 PE fixture contract (IF-009) before any PE test | A2 → A1/A3 | Closed; A1 ACCEPT with conditions at `74b1471` (`a17d126`); A3 ACCEPT at `307b919`; A2 signature at `6d305b8`; IF-009 v1.0 Signed |
| XB-004 | A1 pins Limine and the kernel handoff | A1 → Director | Open; v12.11.1 / base 6 pinned, D-005 pending |
| XB-005 | A3 names device tests and hardware requirements with evidence | A3 → A4 | Open; `make check` passes 19 tests on the integrated tree `44d566e`, but no QEMU/hardware driver test |
| XB-006 | A4 publishes the review branch | A4 → Director | Closed at `84fe81a` |
| XB-007 | Public header namespace conflict | A1 → A4 | Closed by D-009: canonical `include/ozero/` |
| XB-008 | A1's `tools/check-headers.sh` is in A3-owned `tools/` | A1 → A3 | Closed by D-010: A1 owns script and ABI asserts; A4 CI; A3 other tools |
| XB-009 | A2 evidence revision mismatch | A2 → A4 | Closed: evidence regenerated; current headers at `f6ed1d8` / evidence `d83cfbe` |
| XB-010 | A4 opens the Stage 0 review PR | A4 → Director | Closed: [#3](https://github.com/Momegaii/Ozero/pull/3) |
| XB-011 | A3 publishes so A4 can refresh the audit | A3 → A4 | Closed: A3 PR #4 `48d434f`; audit refreshed; no merge implied |
| XB-012 | Enable `main` protection and required checks | A4 → Director | Open; `protected:false`, rulesets `[]`, `CODEOWNERS` 404 on `main` |
| XB-013 | A2 response and ledger lag the evidence revision | A2 → A4 | Closed by A4 reconciliation; `docs/architecture/a2-review-ledger-reconciled.md` records `f6ed1d8` / `d83cfbe` and all signatures; A2 branch ledger remains historical |
| XB-014 | A1 review of A2 is at `f324b28` | A1 → A2 | Closed; A1 re-reviewed `50b7853` / `807c447`; A2 accepts all 12 |
| XB-015 | A1 decision block collided with A4 IDs | A4 → Director | Closed: D-009/D-010 merged; A1 D-011 kept; A4 D-031 |
| XB-016 | A1 command output is inline, not a raw log | A1 → A4 | Closed; raw log copied to `evidence/a1-stage0/` and re-verified at A1 `2693a34` (`RESULT: PASS`, exit 0) |
| XB-017 | A3 rewrites shared root files and adds interface headers under `include/ozero/` | A3 → A4 | Closed; A3 preserves A4 root docs and puts its package in `a3/`; root `include/ozero/` has A1's exact headers plus A3's two IF-008 headers |
| XB-018 | A3 cites A4 `245007d` rather than the current A4 head | A3 → A4 | Closed; A3 `307b919` reviews against A4 `3ae7aca` |
| XB-019 | A3 has not accepted A1's IF-008 §6 conditions or updated its status lines | A3 → A1/A4 | Closed; A3 accepted all eight conditions at `307b919` and updated its status lines |
| XB-020 | A3's `a3/` package and root `include/ozero/` headers are not in A4 `eaca921` | A3 → A4 | Closed; merge `44d566e` adds 44 `a3/` files and 12 `include/ozero/` headers; no A4 control-plane change; PR #4 merged into A4 at `44d566e`; not merged to `main` |
| XB-021 | IF-009 v1.1 truncated-header and missing-DLL-name cases are not implemented | A2 → A1/A3 | Open; v1.0 remains Signed; disposition has six required cases and L0-only acceptance criteria |

## Decisions needed

| ID | Question | Status | Approver | Recommendation |
| --- | --- | --- | --- | --- |
| D-001 | Confirm the A1/A2/A3 domain split and team slugs | **Proposed** | Project Director | Accept the proposed split, then supply real slugs |
| D-002 | Approve branch isolation and the review gate | **Proposed** | Project Director | Approve ADR-0002 and enable protection on `main` |
| D-003 | Approve the Stage 0 build and QEMU MBR harness approach | **Proposed** | Project Director | Approve ADR-0003 |
| D-004 | Approve agent trust boundaries | **Proposed** | Project Director | Approve ADR-0004 |
| D-005 | Choose the boot protocol | **Proposed** | Project Director | A1 to propose; Limine is a candidate only |
| D-006 | Accept Stage 0 | **Proposed** | Project Director | Decide after D-001 to D-004 and D-008 |
| D-007 | Approve the A3 DMA/platform interface and A1 sign-off gate | **Proposed** | Project Director | A1 **ACCEPT** for IF-008 v1.1 at `b9a69d8`; A3 accepted §6 at `307b919`; A3 is integrated into the candidate at `44d566e`, but AHCI/DMA work stays blocked on this decision and D-021 |
| D-008 | Approve publishing the A4 review branch | **Approved** | Project Director | Published; PR [#3](https://github.com/Momegaii/Ozero/pull/3) open |
| D-009 | Adopt `include/ozero/` as the canonical header namespace | **Approved** | Project Director | Implemented by A1 `03f8883`; `include/o0/` rejected by the check; A3 corrected its citation at `307b919` |
| D-010 | Assign `tools/check-headers.sh` and `tests/abi_static_asserts.c` to A1; A4 owns CI integration; A3 owns other `tools/` | **Approved** | Project Director | Implemented in `CODEOWNERS`; not enforced on `main` until it lands there |
| D-011 | Accept A2's process, address-space, handle and syscall requirements as Stage 7 input | **Proposed** | Project Director | A1 answered all 12; A2 accepts all 12 at `6d305b8`; XB-002 Signed; no ABI freeze |
| D-031 | Enable `main` branch protection and required CI checks | **Proposed** | Project Director | Not enabled: `protected:false`, rulesets `[]`, `CODEOWNERS` 404 on `main` |
| D-012–D-030 | A1 architecture and A2 contract proposals | **Proposed** | Project Director | Recorded in the decision log; no approval implied |

## Risks

| Risk | Likelihood | Impact | Mitigation | Owner |
| --- | --- | --- | --- | --- |
| `main` is unprotected and has no required checks | High | Any merge can bypass review and CI | D-031 / XB-012; enable protection and require the Stage 0 check | Director |
| A1 and A2 are not integrated; A3 is integrated but static-only | High | A1/A2 gates are untested; A3 hardware behavior is unknown | A1/A2 integration remains future work; A3 hardware stays behind XB-005 and D-007/D-021 | Director, A1, A2, A4 |
| A1's IF-008 ACCEPT is read as hardware or Stage 0 approval | Medium | False validation claim | A4 record: ACCEPT is an interface decision; A3 accepted the conditions; D-007/D-021 remain; Stage 0 not accepted | A4 |
| A1 raw evidence is not found | Low | Claims are harder to audit | Raw log copied to `evidence/a1-stage0/` and re-verified at A1 `2693a34`; XB-016 Closed | A1, A4 |
| A2's branch ledger remains historical and differs from the candidate | Low | Provenance mismatch | A4 reconciliation is authoritative for the candidate; A2 branch is not rewritten; XB-013 Closed | A4 |
| A3's root header snapshot conflicts with A1's tree | Low | Duplicate or stale headers at merge | XB-017 and XB-020 Closed; merge `44d566e` keeps A1's ten root headers byte-identical and A3's package under `a3/` | A3, A4 |
| A2 records IF-009 signatures against the wrong revision | Low | A stale signature is read as v1.0 approval | XB-003 Closed; A1 `a17d126` at `d02598a`, A3 `307b919`, A2 `6d305b8`; manifest unchanged through `f6ed1d8`; v1.1 gaps tracked as XB-021 | A2, A4 |
| IF-009 v1.1 gaps are mistaken for covered tests | Medium | False confidence in header/import validation | XB-021 Open; disposition lists the six missing negative cases and keeps L0 scope | A4 |
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
| Cross-agent verification | `evidence/stage0-review/cross-agent-verification-2026-11-23.md`, `...-r2.md`, `...-r3.md`, `...-r4.md`, `...-r5.md`, `...-r6.md` | Recorded at `84fe81a`, rounds 2–6; r6 reviews PR #4 and reconciles A2/A1 evidence |
| Cross-agent raw commands | `evidence/stage0-review/cross-agent-commands-2026-11-23-r3.log`, `...-r4.log`, `...-r5.log`, `...-r6.log` | Recorded; current heads, PR #4 file scope, header hashes, A2 pair, A1 raw log and governance |
| A1 header and ABI checks | `evidence/a1-stage0/` (raw log copy, A4 verification); `evidence/stage0-review/a1-check-headers-74b1471.log` | PASS at A1 `2693a34`; `RESULT: PASS`, exit 0; raw log blob `24ce6a9a…` |
| A2 static checks | `evidence/stage0-review/a2-static-check-64b3458.log`; r6 run at `d83cfbe` | PASS at `d83cfbe` / code `f6ed1d8`; 228 docs checks and 61 structure checks, 0 failed |
| A2 reconciliation | `docs/architecture/a2-review-ledger-reconciled.md` | Current pair `f6ed1d8` / `d83cfbe`; all signatures; XB-013 Closed |
| IF-009 v1.1 disposition | `docs/architecture/if-009-v1.1-disposition.md` | Not implemented; XB-021 Open; L0 scope preserved |
| A3 package checks | `evidence/stage0-review/a3-integration-checks-2026-11-23.log` | PASS on integrated tree; 19 tests; `check-a1-headers` exit 0 |
| Final takeover checks | `evidence/stage0-review/final-takeover-checks-2026-11-23.log` | `check-ci-safety: ok`; `ci: ok`; 5 markers; exit 0 |
| CI at takeover commit | `evidence/stage0-review/ci-run-bc3f566.txt` | `build-and-boot` success, run `45195798408`, 4m55s |
| A3 integration proof | `docs/architecture/a3-integration-map.md`, `evidence/stage0-review/a3-integration-verification-2026-11-23.md` | Merge `44d566e`; 44 `a3/` + 12 `include/ozero/`; XB-020 Closed |
| Takeover baseline | `evidence/stage0-review/takeover-baseline-2026-11-23.txt` | Live refs, PRs, runs and local state at takeover |
| Director review and A4 disposition | `evidence/stage0-review/director-review-2026-11-23.md` | Recorded |
| GitHub branch protection and checks | `evidence/stage0-review/github-settings.txt` | Recorded; protection not enabled |
| PR body | `evidence/stage0-review/PR_BODY.md` | Recorded; takeover revision |
| Consolidated A4 master brief | `docs/A4_SOLE_AGENT_MASTER_BRIEF.md`, `Ozero_A4_Sole_Agent_Master_Brief_2026-11-23.pdf` | One A4 master brief; no separate A1/A2/A3 briefs generated |
| Stage 0 review PR | <https://github.com/Momegaii/Ozero/pull/3> | Open; not accepted |

## Agent status

| Agent | Current task | Status | Blocked by |
| --- | --- | --- | --- |
| A1 | Third-round response; PR #1 draft at `2693a34` | Submitted; not integrated | D-005; IF-008 accepted, IF-009 signed with conditions, XB-002 Signed; raw log committed (XB-016 Closed) |
| A2 | Static-only PE package; PR #2 draft at `d83cfbe` / code `f6ed1d8` | Submitted; not integrated; ledger reconciled in A4 | IF-009 v1.1 gaps (XB-021); v1.0 signed and XB-003 Closed |
| A3 | Static hardware package; PR [#4](https://github.com/Momegaii/Ozero/pull/4) at `48d434f`; IF-008 v1.1 at `b9a69d8` | Integrated into A4 at `44d566e`; PR #4 merged into A4, not `main` | Hardware evidence (XB-005); D-007/D-021 |
| A4 | Sole active agent; consolidated candidate at `44d566e`; PR [#3](https://github.com/Momegaii/Ozero/pull/3) | Submitted; awaiting review | Director review; D-031 protection |

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
| 2026-11-23 | A1 supplied exact `CODEOWNERS` overrides at `30ee4f0`; A4 applied both owners | `56bcff0` |
| 2026-11-23 | Repository audit refreshed after A1's second-round commits | `245007d` |
| 2026-11-23 | A3 published `a3/stage0-review-response` at `15cd9ce`; IF-008 v1.1 at `b9a69d8`; XB-011 closed; A3 `make check` and A1-header checks pass | `15cd9ce` (A3), this review commit |
| 2026-11-23 | A2 provenance rechecked: `8bac650` in response, `50b7853` evidence, A1 `90b004f` / `include/o0/` in contract review; IF-009 contradiction recorded | this review commit |
| 2026-11-23 | Governance rechecked: `main` `protected:false`, rulesets `[]`, `CODEOWNERS` 404; D-031 not enabled | this review commit |
| 2026-11-23 | Round-3 cross-agent verification and raw command log; XB-017/XB-018 opened | this review commit |
| 2026-11-23 | A3 published `307b919`, based on A4 `3ae7aca`; accepted IF-008 §6; XB-017/XB-018/XB-019 closed for the A3 branch | A3 `307b919` |
| 2026-11-23 | A1 accepted IF-009 v1.0 with conditions at `74b1471` and confirmed A3's IF-008 packet | A1 `74b1471` |
| 2026-11-23 | R2 audit: corrected the A3 integration claim; A4 `b4232a0` and PR #3 have no `a3/` or root `include/ozero/`; XB-020 opened; A2/A3/A4 refs refreshed | `eaca921` |
| 2026-11-23 | R3 audit: reviewed A3 PR #4 (56 files, clean merge, CI pass) but did not merge; IF-009 v1.0 Signed and XB-003 Closed; XB-016 Closed; A2 ledger pair lag XB-013 Open; blockers 5/14/1 | `a3a67ab` |
| 2026-11-23 | Sole-agent takeover: A1 raw evidence recovered; A2 ledger reconciled; IF-009 v1.1 gaps recorded; A3 PR #4 integrated at `44d566e`; XB-013/XB-016/XB-020 Closed, XB-021 Open; blockers 4/16/1 | this takeover commit |
