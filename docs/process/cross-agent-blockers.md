# Cross-agent blocker register

One row per dependency between agents. A dependent implementation does not merge until the owner
signs the interface and the evidence is linked here. Status words are **Open**, **Signed** or
**Closed**; only the named owner can move a row to Signed.

Snapshot 2026-11-23 after the Director execution directive: **3 Open, 17 Closed, 1 Signed.** Raw
checks are in [`evidence/stage0-review/a3-integration-verification-2026-11-23.md`](../../evidence/stage0-review/a3-integration-verification-2026-11-23.md)
and [`a3-integration-checks-2026-11-23.log`](../../evidence/stage0-review/a3-integration-checks-2026-11-23.log).
A1 is at `2693a34`; A2 is at `d83cfbe` (code `f6ed1d8`, evidence `d83cfbe`) and is integrated into
the A4 candidate at `f0c64a9`; A3 PR #4 is at `48d434f`; A4 merged A3 into the candidate at
`44d566e`. The A4 tree now has `a2/`, `a3/` and root `include/ozero/`; XB-020 is Closed. PR #4 is
merged into the A4 branch at `44d566e`. `main` remains `147cc86`. The IF-009 v1.1 six negative cases
are implemented at `31e6825`, with evidence at `37e9bf0`; XB-021 is Closed for implementation.

| ID | From → to | Dependency | State | Evidence or next action |
| --- | --- | --- | --- | --- |
| XB-001 | A1 → A3 | A1 reviews the A3 DMA/platform/block interface (IF-008) before any AHCI probe work | **Closed** | A1 recorded **ACCEPT** for IF-008 v1.1 at A3 `b9a69d8` in `docs/if008-v1.1-review.md` at A1 `74b1471` (decision commit `d9cdec1`). A3 accepted A1's §6 conditions in `a3/docs/interfaces/IF-008-v1.1-acceptance.md` at `307b919`. D-007/D-021 remain Proposed |
| XB-002 | A1 → A2 | Process, address-space, handle and syscall contracts match A1's kernel contracts | **Signed** | A1 answered `A1-REV-01…12` (4 Accept, 6 Change required, 2 Defer) at `30ee4f0` / `d9cdec1`. A2 accepts all 12 and answers A1's seven open items at `6d305b8`; current pair `f6ed1d8` / `d83cfbe`. No ABI freeze; D-011 remains Proposed |
| XB-003 | A2 → A1/A3 | PE fixture contract (IF-009) is fixed before any PE test | **Closed** | IF-009 v1.0 is Signed: A1 **ACCEPT** with conditions at A1 `74b1471` (decision commit `a17d126`); A3 **ACCEPT** for the hardware boundary at `307b919`; A2 signature recorded at `6d305b8` (signed contract `d02598a`). A2's contract at `d83cfbe` records all three. Fixture manifest is unchanged `d02598a`..`f6ed1d8`; current A2 pair is `f6ed1d8` / `d83cfbe`. A4 updated the IF-009 registry to Signed. Sign-off is not loader approval; no PE test may run without a loader and a separate gate |
| XB-004 | A1 → A4 | Limine pin, boot protocol and kernel handoff (IF-001, D-005) | **Open** | A1 pinned v12.11.1, base revision 6, with hashes; Director D-005 is still pending. A4 prepared the alternatives, compatibility and approval package in [`D-005-boot-protocol-package.md`](../decisions/D-005-boot-protocol-package.md). The MBR harness is not evidence |
| XB-005 | A3 → A4 | Device-specific tests and hardware requirements are named with evidence | **Open** | A3 `make check` (19 tests) and A1-header compile pass on the integrated tree at `44d566e`, but no QEMU driver run, AHCI probe, DMA transfer or real hardware exists. The exact gap and gated test plan are in [`xb-005-a3-hardware-evidence-plan.md`](../architecture/xb-005-a3-hardware-evidence-plan.md). Static checks do not close this row |
| XB-006 | A4 → Director | Publish the A4 branch (D-008) | **Closed** | Published at `84fe81a`; upstream set to `origin/a4/stage0-review-followup` |
| XB-007 | A1 → A4 | Public header namespace conflict | **Closed** | D-009: canonical namespace is `include/ozero/`; A1's `03f8883` refactor makes it the only root; `tools/check-headers.sh` rejects `include/o0/`; A4 map updated |
| XB-008 | A1 → A3 | A1's `tools/check-headers.sh` sits in the A3-owned `tools/` tree | **Closed** | D-010: A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`; A4 owns CI integration; A3 owns other `tools/`; `CODEOWNERS` lists A1 and A4 on both files |
| XB-009 | A2 → A4 | A2 evidence headers cited an older revision than the branch head | **Closed** | A2 regenerated evidence at code revision `f6ed1d8`; evidence commit `d83cfbe`; all evidence headers name `f6ed1d8` |
| XB-010 | A4 → Director | Open the A4 Stage 0 review pull request | **Closed** | Opened as [#3](https://github.com/Momegaii/Ozero/pull/3) at `076ed82` |
| XB-011 | A3 → A4 | Publish A3 so A4 can refresh the repository audit | **Closed** | A3 branch `a3/stage0-review-response` is published; PR #4 head `48d434f` was merged into the A4 branch at `44d566e`. The r4/r5/r6 audits include it. This row is audit access only, not merge approval |
| XB-012 | A4 → Director | Enable `main` branch protection and required CI checks | **Open** | Rechecked: `branches/main` reports `protected:false`; rulesets are `[]`; `CODEOWNERS` is 404 on `main`; the protection endpoint returns HTTP 403 for the supplied PAT, so the protection object is not readable; no required checks are visible (D-031). The exact payloads and the user-vs-organization `CODEOWNERS` problem are in [`branch-protection-audit.md`](../process/branch-protection-audit.md) and [`github-settings-a4-2026-11-23.txt`](../../evidence/stage0-review/github-settings-a4-2026-11-23.txt) |
| XB-013 | A2 → A4 | A2's review response and ledger lag the evidence revision | **Closed** | A4 reconciled the A2 ledger in the consolidated candidate: `docs/architecture/a2-review-ledger-reconciled.md` records code `f6ed1d8` / evidence `d83cfbe`, the `d02598a` signed contract, the unchanged manifest blob `f505cb4d…`, and all three signatures. A2's branch ledger at `d83cfbe` remains historical and is not rewritten. XB-003 stays Closed for IF-009 v1.0 |
| XB-014 | A1 → A2 | A1 reviewed A2 at `f324b28`; A2 has since moved | **Closed** | A1 re-reviewed the added requirements at A2 `50b7853` / evidence `807c447` in `docs/a2-contract-review-response.md`; A2 accepted all 12 at `6d305b8`. Later A2 changes are docs, evidence and test-token updates, not new A1-REV requirements |
| XB-015 | A4 → Director | A1's D-009–D-030 block collided with A4's D-009–D-011 | **Closed** | D-009 and D-010 merged on the same topics; A1's D-011 retained; A4 branch protection renumbered D-031; D-012–D-030 recorded as Proposed |
| XB-016 | A1 → A4 | A1's command output is inline in its report, not a raw log | **Closed** | A1 committed `docs/evidence/a1-raw-checks-2026-11-23-r2.txt` at `9cda79c` (A1 head `2693a34`). A4 copied it verbatim to `evidence/a1-stage0/a1-raw-checks-2026-11-23-r2.txt` (blob `24ce6a9a…`) and re-ran `./tools/check-headers.sh` at A1 `2693a34`; `evidence/a1-stage0/a4-verification-2026-11-23.txt` records the full output, tool versions and `RESULT: PASS` / `exit=0` |
| XB-017 | A3 → A4 | A3's branch rewrites shared root files and adds interface headers under an A1-owned path | **Closed** | A3 `307b919` is based on A4 `3ae7aca`, moves its package into `a3/`, and preserves the root `README.md` / `AGENTS.md`. Root `include/ozero/` has A1's exact ten headers plus A3's two A1-approved IF-008 files. A4 integrated the package at `44d566e` without overwriting A1's headers; XB-020 is Closed |
| XB-018 | A3 → A4 | A3's review basis cites a stale A4 head | **Closed** | A3 `307b919` reviews against A4 `3ae7aca` and records D-009 as `include/ozero/` with XB-007 Closed. The `245007d` basis is gone |
| XB-019 | A3 → A1/A4 | A3 has not recorded acceptance of A1's IF-008 §6 conditions or updated its status lines | **Closed** | A3 `307b919` adds `a3/docs/interfaces/IF-008-v1.1-acceptance.md`, accepts all eight §6 conditions, and updates `a3/docs/06`, `a3/AGENTS.md`, `a3/README.md` and `a3/EVIDENCE.md` to "A1 ACCEPT at `b9a69d8`; Director D-007 pending" |
| XB-020 | A3 → A4 | A3's published `a3/` package and root `include/ozero/` headers are not in the A4 candidate | **Closed** | A4 merged PR #4 `48d434f` into the candidate as merge commit `44d566e` (parent `8b71be1`). The actual tree now has 44 files under `a3/` and 12 under `include/ozero/`; no A4 control-plane file changed. A1's ten headers are byte-identical to `74b1471`; `platform.h`/`block.h` match `a3/include/ozero/`. Proof: `docs/architecture/a3-integration-map.md` and `evidence/stage0-review/a3-integration-verification-2026-11-23.md`. PR #4 is merged into the A4 branch at `44d566e` and is not merged to `main` |
| XB-021 | A2 → A1/A3 | IF-009 v1.1 negative cases for truncated headers and missing DLL-name imports are not implemented | **Closed** | Implemented at `31e6825`; evidence regenerated at `37e9bf0`. Raw run: `a2/docs/evidence/if-009-v1.1-negative-cases.txt`; case table: `a2/docs/15-if-009-v1.1-negative-cases.md`. Four truncated-header cases and two import-Name-RVA cases all fail for the named reason; 12 expected failures, 2 sample skips, 0 problems; unit tests 45 `OK`, 1 skipped. This closes the implementation blocker only. IF-009 v1.1 is **not signed**; A1 and A3 must re-sign, and v1.0 remains the accepted version |

## Rules

- The MBR harness target (`mbr-harness-smoke`) never satisfies XB-004. It is BIOS-only and does not
  load Limine or an Ozero kernel.
- A2's PE findings remain static-only. A runtime corpus is an opt-in, sandboxed job and is not
  normal CI.
- A2's `a2/` package is integrated into the A4 candidate at `f0c64a9` from A2 `d83cfbe`. The
  integration adds `a2/` only and does not rewrite A2's branch or evidence. The six IF-009 v1.1
  cases are implemented at `31e6825`; evidence is `37e9bf0`.
- A1 accepted IF-008 v1.1 at `b9a69d8`; A3 accepted A1's §6 conditions at `307b919`. XB-001 and
  XB-019 are Closed. A3 still does not start AHCI probe work: the Director must decide D-007 and
  D-021. A3's `make check` and A1-header compile pass at `307b919`, but they are static checks, not a
  driver test.
- A3 has PR #4 against the A4 branch, not against `main`. A4 integrated PR #4 `48d434f` into the
  candidate as merge commit `44d566e`; the tree proof is in `docs/architecture/a3-integration-map.md`.
  PR #4 is merged into the A4 branch at `44d566e` and is not merged to `main`. The integration does not decide D-007
  or D-021, and it does not authorize AHCI, DMA, GPU or hardware work.
- A3 remains static-only. `make -C a3 check` and the A1-header compile pass, but no QEMU driver run,
  AHCI probe, DMA transfer or hardware test exists. XB-005 stays Open.
- XB-002 is Signed, not Closed: A1 answered and A2 accepted, but D-011 remains Proposed and no ABI is
  frozen. XB-003 is Closed because IF-009 v1.0 is signed by A1, A3 and A2. XB-013 is Closed by the A4
  reconciliation in `docs/architecture/a2-review-ledger-reconciled.md`; the A2 branch ledger remains
  historical. XB-021 is Closed for implementation; IF-009 v1.1 is not signed and the A1/A3 re-sign
  gate remains.
- A1 committed its raw log at `docs/evidence/a1-raw-checks-2026-11-23-r2.txt` (A1 `2693a34`), so
  XB-016 is Closed. A report alone would not satisfy the evidence rule.
- A2's `a2/` package is the isolation model: one agent tree, no control-plane edits.
- A status change needs a commit, a command and raw output. A link to a branch alone is not evidence.
- The Director owns D-031 and the review under XB-010. A4 implemented D-009 and D-010 under the
  Director's 2026-11-23 instruction to resolve the namespace and ownership conflicts. A1's
  D-011–D-030 proposals are recorded in the canonical register; A1's D-009 and D-010 were merged
  into A4's rows on the same topics.
