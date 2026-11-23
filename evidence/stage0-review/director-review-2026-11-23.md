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
| 2. Refresh audit after A3 is published | Pending A3 | A3 has no branch at this snapshot; XB-011 tracks the refresh |
| 3. Verify A1/A2/A3 evidence and ancestry | Done for A1/A2; A3 absent | `cross-agent-verification-2026-11-23-r2.md`; A1 head `115a7f4`, A2 evidence `50b7853`, head `807c447`; A1 reviewed A2 at older `f324b28` (XB-014) |
| 4. Resolve namespace and ownership | Done | D-009 (`include/ozero/` only, A1 `03f8883`), D-010 (tools), `CODEOWNERS`, `ownership.md` |
| 5. Keep MBR and kernel boot separate | Done | `tests/targets.tsv`, dashboard, PR body |
| 6. Confirm branch protection and checks | Done: not enabled | `github-settings.txt`, D-031 |
