# A3 response to the 23 November 2026 proceed brief — Revision 2

| Field | Value |
|---|---|
| Brief | `Ozero_A3_Proceed_Brief_2026-11-23_R2.pdf` |
| Branch | `a3/stage0-review-response` |
| A3 package | `a3/` |
| A3 acceptance commit | `307b919` |
| A3 current head | this commit (HEAD) |
| A4 candidate | `a4/stage0-review-followup` @ `b4232a0`; A4 PR #3 open non-draft |
| Other PRs | A4 PR #1/#2 draft; all unmerged; no Director approval |
| A3 integration PR | [#4](https://github.com/Momegaii/Ozero/pull/4), base `a4/stage0-review-followup` |
| A1 review head | `74b1471`; IF-008 decision commit `85df005` |
| A2 IF-009 v1.0 | contract `d02598a`; latest evidence `64b3458` (code `6d305b8`) |
| Stage 0 | **NOT ACCEPTED** |
| D-006 / D-007 / D-021 / D-031 | **Proposed** |
| AHCI probe | **Blocked** |
| Runtime evidence | **None**; static and compile-time checks only |

Revision 2 holds merges and confirms that A3's acceptance exists on its branch
but is not yet present in the A4 candidate. This response records the gap and
the reviewable integration path.

## R2-1 — Bring the A3 package into the A4 candidate

A3 opened review PR [#4](https://github.com/Momegaii/Ozero/pull/4) from
`a3/stage0-review-response` into `a4/stage0-review-followup`. It adds:

- the complete `a3/` package;
- root `include/ozero/` with A1's ten public headers plus A3's two
  A1-approved IF-008 headers.

The PR is review-only. It does not merge to `main`, does not approve Stage 0,
and does not change D-006, D-007, D-021 or D-031.

## R2-2 — A4 candidate gap verified

A4 PR #3 at `b4232a0` has **zero** `a3/` files and **zero** root
`include/ozero/` files in its diff. The A4 candidate tree also has no `a3/`
directory and no `include/ozero/` directory. A4's XB-017 text says it is closed,
but the candidate tree does not demonstrate the integration.

Until A4 merges PR #4, A3 is a **separate branch**, not integrated into the A4
candidate. A3 removes any claim that the A4 candidate already contains its
package.

A4 has already added file-level `CODEOWNERS` entries for
`include/ozero/platform.h` and `include/ozero/block.h`; A3 verified them at
`b4232a0`.

## R2-3 — Keep acceptance tied to exact commits

| Item | Exact anchor |
|---|---|
| IF-008 v1.1 interface | A3 `b9a69d8` |
| A1 IF-008 decision | A1 `74b1471`; decision commit `85df005` |
| A3 acceptance of A1's eight §6 conditions | A3 `307b919`; `a3/docs/interfaces/IF-008-v1.1-acceptance.md` |
| IF-009 v1.0 contract | A2 `d02598a` |
| A1 IF-009 decision | A1 `a17d126`; **ACCEPT (A1 side) with conditions** |
| A3 IF-009 hardware-boundary acceptance | A3 `307b919`; `a3/docs/interfaces/IF-009-a3-review.md` |
| A2 latest IF-009 evidence | `64b3458` (code `6d305b8`); A2 signature record pending |

A3's IF-009 acceptance is hardware-boundary only. It is not PE loader approval
and not a Windows compatibility claim.

## R2-4 — Keep AHCI blocked

D-007 and D-021 remain Proposed. A3 has not started the AHCI probe. No AHCI,
NVMe, USB, GPU, DMA transfer, QEMU driver or real-hardware test is evidenced.

## R2-5 — Keep tests labelled static

`make -C a3 check` runs the matrix validator, deliverable checker, 19 static
tests, header smoke compile and inventory syntax check. `make -C a3
check-a1-headers` is a compile-time check against A1's headers. Neither is a
driver or hardware test.

## Definition-of-done status for Revision 2

| Criterion | Status |
|---|---|
| A3 package present in A4 candidate | **Pending A4 merge of PR #4** |
| A4 PR #3 diff includes `a3/` and root `include/ozero/` | **Pending**; currently 0 / 0 |
| A3 labelled as separate branch until integration | **Done** in this response |
| IF-008 §6 acceptance tied to exact commits | **Done**: A1 `85df005`, A3 `307b919`, interface `b9a69d8` |
| IF-009 hardware-boundary acceptance tied to exact commits | **Done**: A2 `d02598a`, A3 `307b919` |
| AHCI blocked until Director D-007/D-021 | **Held** |
| Tests labelled static | **Done** |

## Handoff

A4 should review and merge PR #4 into `a4/stage0-review-followup`, then verify
that A4 PR #3's diff contains `a3/` and the exact root `include/ozero/` headers.
A1 should review the A3 IF-009 v1.0 confirmation. The Director still owns
D-006, D-007, D-021 and D-031. No merge to `main` is requested.

## References

- A3 integration PR: <https://github.com/Momegaii/Ozero/pull/4>
- A4 PR #3: <https://github.com/Momegaii/Ozero/pull/3>
- A1 IF-008 decision:
  <https://github.com/Momegaii/Ozero/blob/74b1471/docs/if008-v1.1-review.md>
- A1 IF-009 decision:
  <https://github.com/Momegaii/Ozero/blob/a17d126/docs/if009-review.md>
- A2 IF-009 v1.0:
  <https://github.com/Momegaii/Ozero/blob/d02598a/a2/docs/13-if-009-pe-fixture-contract.md>
- A4 interface registry:
  <https://github.com/Momegaii/Ozero/blob/b4232a0/docs/architecture/interfaces.md>
