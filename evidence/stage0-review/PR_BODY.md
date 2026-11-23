This PR was created by an AI agent (OpenHands) on behalf of A4.

## Summary

A4 is the sole active agent under the 23 November 2026 all-work takeover order. This PR is the
consolidated Stage 0 candidate. It recovers A1/A2/A3 work from their existing branches and PRs,
preserves authorship, integrates A3 PR #4 into the A4 branch, and records the remaining gaps.

**Stage 0 is not accepted.** This PR does not request a stage promotion. D-006 remains **Proposed**.
D-007, D-021 and D-031 also remain **Proposed**. No merge to `main` is performed.

## Live baseline

| Item | State |
| --- | --- |
| `main` | `147cc86` (unchanged) |
| A1 | `a1/stage0-review` at `2693a34`; PR #1 draft |
| A2 | `agent/a2-stage0` at `d83cfbe`; code `f6ed1d8`; PR #2 draft |
| A3 | `a3/stage0-review-response` at `48d434f`; PR #4 merged into A4 |
| A4 | `a4/stage0-review-followup`; PR #3 |
| A4 before takeover | `a3a67ab` |
| A3 integration merge | `44d566e` (parent `8b71be1`; A3 `48d434f`) |
| Baseline log | `evidence/stage0-review/takeover-baseline-2026-11-23.txt` |

## What changed

### A1 evidence recovered

- A1's raw log is copied verbatim to `evidence/a1-stage0/a1-raw-checks-2026-11-23-r2.txt`
  (source blob `24ce6a9a…`, SHA-256 `457235e5…`).
- A4 re-ran `./tools/check-headers.sh` in an A1 worktree at `2693a34`. The full output, tool
  versions and exit codes are in `evidence/a1-stage0/a4-verification-2026-11-23.txt`.
- Result: `RESULT: PASS`, `exit=0`. XB-016 is **Closed**. This is static header/ABI evidence, not
  kernel boot or runtime evidence.

### A2 ledger reconciled; IF-009 v1.1 disposition

- IF-009 v1.0 remains **Signed** by A1, A3 and A2; XB-003 is **Closed**.
- Current A2 pair: code `f6ed1d8`, evidence `d83cfbe`; signed contract `d02598a`; manifest blob
  `f505cb4d…` unchanged.
- A4 reconciliation: `docs/architecture/a2-review-ledger-reconciled.md`. A2's branch ledger remains
  historical and is not rewritten. XB-013 is **Closed** in the candidate.
- IF-009 v1.1 is **not signed and not implemented**. The truncated-header and missing-DLL-name
  cases are specified in `docs/architecture/if-009-v1.1-disposition.md` as six required negative
  cases with L0-only acceptance criteria. XB-021 is **Open**.

### A3 integrated into the A4 candidate

Merge commit `44d566e` adds 56 files: 44 under `a3/` and 12 under `include/ozero/`. No A4
control-plane file changed.

- A1's ten root headers are byte-identical to A1 `74b1471`.
- A3's `platform.h` and `block.h` match `a3/include/ozero/` by blob hash.
- `make -C a3 check`: 19 tests, `OK`, exit 0.
- `make -C a3 check-a1-headers A1_INCLUDE=include`: exit 0.
- `make clean && make ci`: `ci: ok`, 5 serial markers, image SHA-256 `37ad8227…`, exit 0.
- File-level map: `docs/architecture/a3-integration-map.md`.
- Evidence: `evidence/stage0-review/a3-integration-verification-2026-11-23.md` and
  `evidence/stage0-review/a3-integration-checks-2026-11-23.log`.
- XB-020 is **Closed**. PR #4 is merged into the A4 branch at `44d566e` and is **not** merged to `main`.

Integration is not a hardware result. A3 remains static-only: no AHCI probe, DMA transfer, GPU
execution or real-hardware test exists. XB-005 stays **Open**.

### Consolidated A4 master brief

One master brief is at `docs/A4_SOLE_AGENT_MASTER_BRIEF.md` with the rendered PDF
`Ozero_A4_Sole_Agent_Master_Brief_2026-11-23.pdf`. No separate A1, A2 or A3 brief was generated.

## Blocker snapshot

**4 Open, 16 Closed, 1 Signed.**

- Open: XB-004 (D-005 boot protocol), XB-005 (A3 hardware evidence), XB-012 (branch protection /
  D-031), XB-021 (IF-009 v1.1 gaps).
- Signed: XB-002 (A1/A2 contracts; D-011 Proposed; no ABI freeze).
- Closed this round: XB-013, XB-016, XB-020.

## Checks at this head

```text
$ make -C a3 check
Ran 19 tests in 0.004s
OK
exit=0

$ make -C a3 check-a1-headers A1_INCLUDE=include
cc -std=c11 -Wall -Wextra -Werror -Iinclude -Iinclude -fsyntax-only tests/header_smoke.c tests/a1_header_smoke.c
exit=0

$ make clean && make ci
...
test: ok (5 markers in order)
ci: ok
exit=0
```

The CI job named `build-and-boot` runs the MBR fixture/harness smoke test. It is not UEFI, Limine
or Ozero kernel boot. The 512-byte MBR fixture is not the kernel.

## What this does not authorize

- No Stage 0 acceptance. D-006 remains **Proposed**.
- No AHCI, DMA, GPU or hardware work. D-007 and D-021 remain **Proposed**.
- No merge to `main`, no approval, and no branch-protection change. D-031 remains **Proposed**.
- No PE load, import resolution, TLS callback, exception dispatch or Windows compatibility claim.
- No claim that IF-009 v1.1 gaps are covered.
- No relabeling of A1/A2/A3 work as A4 work. Authorship and source commits are recorded.

## Next measurable milestone

The next milestone is Director disposition of D-006, D-007, D-021 and D-031, followed by either:

1. A1/A2 integration into the A4 candidate with the same file-level proof used for A3; or
2. implementation of the six IF-009 v1.1 negative cases with L0 negative reports and A1/A3 re-sign.

Completion test: the A4 tree contains the agreed change, the named checks pass with raw output, and
the relevant blocker row moves only when its evidence condition is met.
