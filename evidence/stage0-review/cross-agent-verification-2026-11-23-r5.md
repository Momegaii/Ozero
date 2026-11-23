# Cross-agent verification — R2 audit refresh (r5)

| Field | Value |
| --- | --- |
| Date | 2026-11-23 |
| Author | A4 |
| Branch | `a4/stage0-review-followup` |
| A4 head | `b4232a0` |
| Stage state | **NOT ACCEPTED**; D-006 remains Proposed |
| Raw commands | [`cross-agent-commands-2026-11-23-r5.log`](cross-agent-commands-2026-11-23-r5.log) |
| Brief | `Ozero_A4_Proceed_Brief_2026-11-23_R2.pdf` |

This round corrects a material error in round 4: A3 is **not** integrated into the A4 candidate.
A3 `307b919` is based on A4 `3ae7aca` and adds `a3/` plus root `include/ozero/` on the A3 branch
only. A4 `b4232a0` and PR #3 contain neither path.

## 1. Current refs

| Agent | Ref | Commit | State |
| --- | --- | --- | --- |
| A1 | `origin/a1/stage0-review` | `74b1471` | PR #1, draft; IF-008 ACCEPT; IF-009 ACCEPT with conditions |
| A2 | `origin/agent/a2-stage0` | `64b3458` | PR #2, draft; code/docs `6d305b8`; evidence-only child |
| A3 | `origin/a3/stage0-review-response` | `307b919` | published; based on A4 `3ae7aca`; no PR; **not in A4** |
| A4 | `origin/a4/stage0-review-followup` | `b4232a0` | PR #3, open, non-draft |
| `main` | `origin/main` | `147cc86` | unchanged |

Ancestry: `main` is an ancestor of all four branches. `3ae7aca` is an ancestor of A3 `307b919`.
`307b919` is **not** an ancestor of A4 `b4232a0`, and A4 does not contain A3's commits.

## 2. Integration truth

```
$ git ls-tree -r --name-only origin/a4/stage0-review-followup | grep -E '^(a3/|include/ozero/)'
(none)

$ git ls-tree -r --name-only origin/a3/stage0-review-response | grep -c '^a3/'
42
$ git ls-tree -r --name-only origin/a3/stage0-review-response | grep -c '^include/ozero/'
12

$ gh pr diff 3 --name-only | wc -l
95
$ gh pr diff 3 --name-only | grep -E '^(a3/|include/ozero/)'
(none)
```

The 95-file count was measured at A4 `b4232a0`, before this audit commit. The audit commit adds
documentation and evidence only.

The A3 branch is based on A4 `3ae7aca`; its root `README.md`, `AGENTS.md`, `CODEOWNERS`, `docs/`,
`evidence/`, `scripts/` and `Makefile` match A4 at `3ae7aca`. It adds the A3 package under `a3/`
and 12 files under root `include/ozero/`.

That is evidence about the **A3 branch**, not evidence that A4 contains A3. The A4 candidate has no
`a3/` directory and no root `include/ozero/` directory. At `b4232a0`, PR #3 had 95 changed files and
no `a3/` or root `include/ozero/` paths.

A3 remains separate. XB-020 records the missing integration.

## 3. Interface decisions

A1 `docs/if008-v1.1-review.md` at `74b1471` records **ACCEPT** for IF-008 v1.1 at A3 `b9a69d8`.
A3 `a3/docs/interfaces/IF-008-v1.1-acceptance.md` at `307b919` accepts all eight §6 conditions.
XB-001 and XB-019 are Closed. D-007 and D-021 remain Proposed; the AHCI probe remains blocked.

A1 `docs/if009-review.md` at `74b1471` records **ACCEPT (A1 side) with conditions** for IF-009 v1.0
at A2 `d02598a` (decision commit `a17d126`). A3 `a3/docs/interfaces/IF-009-a3-review.md` at
`307b919` accepts the hardware boundary at `d02598a` / `3f78477`.

A2's response table and review ledger still name `d02598a` / `3f78477` and still mark the A1/A3
IF-009 reviews pending, while A2's code/docs are at `6d305b8` with evidence at `64b3458`. XB-003
therefore remains **Open** until A2 records both signatures and the current evidence pair.

XB-002 is **Signed**: A1 answered all 12 items and A2 accepts all 12 at `6d305b8`. No ABI freeze;
D-011 remains Proposed.

## 4. Blocker disposition

| ID | Before | After | Basis |
| --- | --- | --- | --- |
| XB-001 | Open | **Closed** | A1 ACCEPT at `b9a69d8`; A3 accepts §6 at `307b919` |
| XB-002 | Open | **Signed** | A1 answered all 12; A2 accepts all 12 at `6d305b8` |
| XB-003 | Open | **Open** | A1/A3 accept IF-009 v1.0; A2 signature record still stale |
| XB-004 | Open | **Open** | D-005 pending; MBR harness is not evidence |
| XB-005 | Open | **Open** | no QEMU driver, AHCI, DMA or hardware test |
| XB-012 | Open | **Open** | `protected:false`, rulesets `[]`, `CODEOWNERS` 404 |
| XB-013 | Open | **Open** | A2 response/ledger lag the evidence pair and both signatures |
| XB-014 | Open | **Closed** | A1 re-reviewed A2; A2 accepts all 12 |
| XB-016 | Open | **Open** | A1 has no `evidence/a1-stage0/` tree |
| XB-017 | Open | **Closed** | A3 `307b919` preserves root docs; package in `a3/`; **A3 branch only** |
| XB-018 | Open | **Closed** | A3 `307b919` reviews against A4 `3ae7aca` |
| XB-019 | — | **Closed** | A3 accepts all eight §6 conditions at `307b919` |
| XB-020 | — | **Open** | A3 `a3/` and root `include/ozero/` are not in A4 `b4232a0` or PR #3 |

Snapshot: **7 Open, 12 Closed, 1 Signed**. Open rows are XB-003, XB-004, XB-005, XB-012, XB-013,
XB-016 and XB-020. Signed is XB-002.

## 5. Checks actually run

| Check | Command | Exit | Result | Artifact |
| --- | --- | --- | --- | --- |
| A1 headers | `bash tools/check-headers.sh` in A1 `74b1471` | 0 | PASS | `a1-check-headers-74b1471.log` |
| A2 static | `make check-static` in `a2/` at A2 `64b3458` | 0 | 61 checks, 0 failed | `a2-static-check-64b3458.log` |
| A3 package | `make -C a3 check` at A3 `307b919` | 0 | 19 tests PASS | `a3-check-307b919.log` |
| A3 against A1 headers | `make -C a3 check-a1-headers A1_INCLUDE=<A1 74b1471>/include` | 0 | compile PASS | `a3-a1-header-check-307b919.log` |
| A4 CI | GitHub Actions `build-and-boot` on `b4232a0` | success | 4m55s | run 45180803273 |
| A4 local CI | `make ci` | 0 | 5 markers in order | `/tmp/a4-ci.log` (not committed) |

These are static/compile checks plus the MBR harness smoke. They are not QEMU driver runs, AHCI
probes, DMA transfers, kernel boot or hardware tests.

## 6. Ownership

`CODEOWNERS` on A4 has no `/a3/` rule. It has `/include/ozero/` owned by A1 and A4, with
`/include/ozero/platform.h` and `/include/ozero/block.h` owned by A3 and A1. Those root header rules
would apply only if A3's root headers land in A4. Since A3 is not in A4, no A3 path is currently
owned by this candidate. XB-020 covers the missing integration and ownership decision.

## 7. Governance

The 2026-11-23 recheck returns:

- `branches/main`: `"protected": false`, `"protection.enabled": false`, required checks `[]`.
- `rulesets`: `[]`.
- `CODEOWNERS` on `main`: HTTP 404.
- The protection endpoint returns 403 for this fine-grained PAT; that is a token limit, not
  evidence of protection.

D-031 / XB-012 remain Open. D-006, D-007, D-021 and D-031 remain Proposed.

## 8. Limits

- Stage 0 is **not accepted**. D-006 remains Proposed.
- No PR is merged. A3 has no PR. A3 is not in the A4 candidate.
- A1's ACCEPT is an interface decision, not hardware or runtime validation.
- A2's IF-009 work is static-only; no PE loads.
- A3 has no driver, AHCI probe or DMA transfer.
- The MBR fixture is not the Ozero kernel.

## 9. Sources

- A1: `docs/if008-v1.1-review.md`, `docs/if009-review.md`,
  `docs/a2-contract-review-response.md` at `74b1471`.
- A2: `docs/12-a1-contract-review.md`, `docs/13-if-009-pe-fixture-contract.md`,
  `docs/14-review-ledger.md`, `docs/evidence/*` at `64b3458` / `6d305b8`.
- A3: `a3/docs/06-stage0-review-response.md`,
  `a3/docs/interfaces/IF-008-v1.1-acceptance.md`,
  `a3/docs/interfaces/IF-009-a3-review.md` at `307b919`.
- A4: `docs/process/cross-agent-blockers.md`, `docs/architecture/interfaces.md`,
  `docs/decisions/decision-log.md` at `b4232a0` plus this audit commit.
- Brief: `Ozero_A4_Proceed_Brief_2026-11-23_R2.pdf`, supplied by the Director on 2026-11-23. The PDF
  is coordination input; it is not committed to this repository.
