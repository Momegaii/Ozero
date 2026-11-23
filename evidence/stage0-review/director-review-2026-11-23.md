# Director Stage 0 integration review — 2026-11-23

Status: **Stage 0 remains NOT ACCEPTED.** Continue from existing work; do not restart Stage 0 and do
not merge directly into `main`.

## A1

1. Resolve the `include/o0/` versus `include/ozero/` repository namespace conflict.
2. Resolve ownership of `tools/check-headers.sh`.
3. Review A2's process, address-space, handle and syscall requirements.
4. Confirm the exact DMA interface that A3 must implement.

## A2

1. Regenerate evidence at the final branch HEAD.
2. Request A1's review of the process/address-space/handle/syscall contract.
3. Keep all unexecuted Windows corpus entries classified as not-run.
4. Keep M0 and M1 unimplemented until their prerequisites are approved.

## A3

1. Resolve the push-permission problem.
2. Reconcile `platform.h` and `block.h` with A1's signed headers.
3. Remove unsupported DMA-domain and reset assumptions.
4. Preserve the trusted ring-0-only Stage 1 restriction.
5. Do not start AHCI probe work until the interface is reconciled and approved.

## A4

1. Open the A4 Stage 0 review PR.
2. Refresh the repository audit after A3 is published.
3. Verify A1/A2/A3 evidence and commit ancestry.
4. Resolve the shared namespace and ownership conflicts.
5. Keep MBR harness results separate from actual Limine/kernel boot results.
6. Confirm whether branch protection and required CI checks are actually enabled in GitHub settings.

## All agents

Use global decision IDs, report exact commit hashes, distinguish tests run from tests not run, and
document cross-agent dependencies.

Submit updated evidence and PRs for director review. No Stage 0 acceptance or implementation-stage
promotion is implied by this instruction.

## A4 disposition

| Action | Disposition | Evidence |
| --- | --- | --- |
| 1. Open the A4 Stage 0 review PR | Done | [#3](https://github.com/Momegaii/Ozero/pull/3) |
| 2. Refresh audit after A3 is published | Done | A3 PR #4 `48d434f` is reviewed; it adds 56 files on top of A4 `b4232a0`; no A4 control-plane changes; XB-011 Closed; XB-020 Open because PR #4 is not merged |
| 3. Verify A1/A2/A3 evidence and ancestry | Done | `cross-agent-verification-2026-11-23-r6.md`; A1 `2693a34`, A2 `d83cfbe` / `f6ed1d8`, A3 PR #4 `48d434f`, A4 `eaca921`; IF-009 v1.0 Signed and XB-003 Closed; A2 ledger pair lag XB-013 Open; XB-016 Closed |
| 4. Resolve namespace and ownership | Done | D-009 (`include/ozero/` only, A1 `03f8883`), D-010 (tools), `CODEOWNERS`, `ownership.md` |
| 5. Keep MBR and kernel boot separate | Done | `tests/targets.tsv`, dashboard, PR body |
| 6. Confirm branch protection and checks | Done: not enabled | `github-settings.txt`; `protected:false`, rulesets `[]`, `CODEOWNERS` 404 on `main`; D-031 |

## R3 audit disposition

The R3 proceed brief added no new Director decision. A4 reviewed A3 PR #4 and did **not** merge it:
D-007 and D-021 remain Proposed and the integration checklist requires Director authorization.
IF-009 v1.0 is Signed and XB-003 is Closed, but Stage 0 remains **NOT ACCEPTED** and D-006 remains
**Proposed**. A2's ledger pair lag is XB-013; A1's raw log closes XB-016. The current snapshot is
5 Open / 14 Closed / 1 Signed.
