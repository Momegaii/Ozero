# A2 review ledger — A4 reconciliation

| Field | Value |
| --- | --- |
| Purpose | Reconcile A2's ledger and evidence pair in the consolidated A4 candidate |
| A2 branch | `agent/a2-stage0` at `d83cfbe` (historical; not rewritten) |
| A2 code revision | `f6ed1d8` |
| A2 evidence head | `d83cfbe` |
| Signed IF-009 v1.0 contract | `d02598a` |
| Manifest blob | `f505cb4d9403b65d860c92d574bd736270db446a` |
| A1 signature | `74b1471` (decision `a17d126`) |
| A3 signature | `307b919` |
| A2 signature | `6d305b8` |
| Stage 0 | **NOT ACCEPTED** |
| Date | 2026-11-23 |

A2's original ledger at `d83cfbe` still names the old pair `6d305b8` / `64b3458` and marks
the A4 registry update as pending. That ledger is preserved as historical A2 work. This A4
table is the authoritative reconciliation for the consolidated candidate.

## Reconciled rows

| Item | Reviewer | Revision | Verdict | State in A4 candidate |
| --- | --- | --- | --- | --- |
| XB-002, A1-REV-01…12 | A1 | A1 `74b1471` (`a17d126`), reconciled to A2 `d02598a` / `3f78477` | 4 Accept, 6 Change required, 2 Defer | **Signed**; A2 accepts all 12 at `6d305b8`; current pair `f6ed1d8` / `d83cfbe` |
| XB-003, IF-009 v1.0 | A1 | `d02598a`, review `a17d126`, head `74b1471` | Accept with conditions | **Closed**; A1 signature recorded |
| XB-003, IF-009 v1.0 | A3 | `d02598a`, review `307b919` | Accept for the hardware boundary | **Closed**; A3 signature recorded |
| IF-009 v1.0 A2 signature | A2 | contract `d02598a`, signature commit `6d305b8` | Recorded | **Signed**; fixture manifest unchanged through `f6ed1d8` |
| XB-009, evidence | A4 | code `f6ed1d8`, evidence `d83cfbe` | Verified | **Closed**; all evidence headers name `f6ed1d8` |
| XB-013, ledger lag | A4 | code `f6ed1d8`, evidence `d83cfbe` | Reconciled here | **Closed in the A4 candidate**; A2 branch ledger remains historical |
| IF-009 v1.1 gaps | A1 | truncated headers; missing DLL name | Deferred to v1.1 | **Open** as XB-021 |
| D-006, Stage 0 acceptance | Director | — | Proposed | **Not approved** |
| D-011…D-030 | Director | A1 proposals | Proposed | **Not approved** |

## Evidence checks

```text
$ git diff --stat f6ed1d8 d83cfbe -- ':!a2/docs/evidence'
(empty)

$ git diff --stat d02598a f6ed1d8 -- a2/fixtures a2/tools/build_fixtures.py
(empty)

$ git rev-parse d02598a:a2/fixtures/build-manifest.json f6ed1d8:a2/fixtures/build-manifest.json
f505cb4d9403b65d860c92d574bd736270db446a
f505cb4d9403b65d860c92d574bd736270db446a
```

The full command log is in `evidence/stage0-review/cross-agent-commands-2026-11-23-r6.log`.

## Boundaries

- This reconciliation does not rewrite A2's branch or claim A2's work as A4's.
- It does not authorize PE execution, loader work or Windows compatibility claims.
- It does not approve IF-009 v1.1. The v1.1 gaps stay open as XB-021.
- It does not approve D-006 or any Director decision.
