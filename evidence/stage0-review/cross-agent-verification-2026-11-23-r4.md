# Cross-agent verification — proceed-brief refresh (r4)

| Field | Value |
| --- | --- |
| Date | 2026-11-23 |
| Author | A4 |
| Branch | `a4/stage0-review-followup` |
| A4 base | `3ae7aca` |
| Stage state | **NOT ACCEPTED**; D-006 remains Proposed |
| Raw commands | [`cross-agent-commands-2026-11-23-r4.log`](cross-agent-commands-2026-11-23-r4.log) |

## 1. Current refs

| Agent | Ref | Commit | State |
| --- | --- | --- | --- |
| A1 | `origin/a1/stage0-review` | `74b1471` | PR #1, draft; IF-008 ACCEPT; IF-009 ACCEPT with conditions |
| A2 | `origin/agent/a2-stage0` | `64b3458` | PR #2, draft; code/docs `6d305b8`; evidence-only child |
| A3 | `origin/a3/stage0-review-response` | `307b919` | published; integrated on A4 `3ae7aca`; no PR |
| A4 | `origin/a4/stage0-review-followup` | `3ae7aca` | PR #3, open, non-draft |
| `main` | `origin/main` | `147cc86` | unchanged |

The proceed brief named A2 `3f78477` / code `d02598a` and A3 `2dcc0fc`. Both advanced during
verification. A3's `307b919` merges A4 `3ae7aca`, moves the A3 package into `a3/`, and accepts A1's
IF-008 §6 conditions.

Ancestry checks: `origin/main` is an ancestor of all four branches. `30ee4f0` is an ancestor of A1
`74b1471`; `d02598a` is an ancestor of A2 `64b3458`; `2dcc0fc` is an ancestor of A3 `307b919`;
`245007d` and `3ae7aca` are ancestors of A4 and A3.

## 2. A1 — IF-008 ACCEPT and IF-009 review

A1 `docs/if008-v1.1-review.md` at `74b1471` records:

```
| Decision | **ACCEPT** |
| A3 interface commit | b9a69d855360df28ad7503f3ba748dcde0910471 |
| A3 latest head | 2dcc0fcc6371aa6167ca773321e76ca9d5a587c0 |
| A1 header commit | 30ee4f0da3c3d4263033680d39e7937fcb59f42f |
| A4 registry reviewed | 245007d2b96bbc12de1e831c5275587ef56ee81f; latest audit 3ae7acaf4d81d103063a33ca49c1739bdee316f8 |
```

A1 accepts IF-008 v1.1 at A3 `b9a69d8`, with eight conditions in §6. A1 states this is not hardware
validation, not a runtime DMA test and not Stage 0 acceptance. A3 `307b919` accepts all eight
conditions in `a3/docs/interfaces/IF-008-v1.1-acceptance.md` and updates its status lines to "A1
ACCEPT at `b9a69d8`; Director D-007 pending". XB-001 and XB-019 are Closed. D-007 and D-021 remain
Proposed, so the AHCI probe remains blocked.

A1 `docs/if009-review.md` at `74b1471` records:

```
| Decision | **ACCEPT (A1 side) with conditions** |
| A2 code revision | d02598afe42ea0036a130a69801b3041f4b4cf9f |
| A2 evidence head | 3f7847756f256761a4da0ed393d2762111af22db |
| IF-009 contract blob | 9be6c8b02d20ac1cbdd5c27e62d0ae94a264fc2f |
| A1 review commit | a17d126664ba9c54792612590268f97d7f439b7d |
```

A1 accepts the fixture and static-analysis contract, subject to the IF9-A1-1 kernel-primitive
condition. This is not a runtime claim and not a Windows compatibility claim.

## 3. A2 — XB-002, IF-009 and provenance

A1 answered `A1-REV-01…12` in `docs/a2-contract-review-response.md` at `30ee4f0` / `d9cdec1`:
4 Accept, 6 Change required, 2 Defer. A2 `docs/12-a1-contract-review.md` and
`docs/14-review-ledger.md` at `6d305b8` record that A2 accepts all 12 and answers A1's seven open
items. A4 records XB-002 as **Signed**. This is not an ABI freeze; D-011 remains Proposed.

A2 published IF-009 v1.0 at `d02598a` with evidence at `3f78477`. A1 **ACCEPT**ed with conditions at
`a17d126`; A3 **ACCEPT**ed the hardware boundary at `307b919`. A2's current code/docs head is
`6d305b8` with evidence at `64b3458`. A2's response table and review ledger still mark both IF-009
reviews as pending and still name `d02598a` / `3f78477`. A2 must record both signatures and the
current evidence pair. XB-003 and XB-013 remain Open until it does.

The code/evidence pair itself is consistent: `git diff 6d305b8..64b3458 -- ':!a2/docs/evidence'` is
empty, and the evidence headers name `6d305b8`.

## 4. A3 — integration and condition acceptance

A3 `307b919`:

- Merges A4 `3ae7aca`; root `README.md`, `AGENTS.md`, `CODEOWNERS`, `docs/`, `evidence/`, `scripts/`
  and `Makefile` are A4's.
- Moves the A3 package into `a3/`; `a3/README.md` and `a3/AGENTS.md` are package-scoped.
- Adds root `include/ozero/` with A1's ten headers byte-identical to A1 `30ee4f0`, plus A3's
  `platform.h` and `block.h` at the reviewed blobs.
- Keeps a duplicate package snapshot at `a3/include/ozero/`; merge order must keep A1's root headers
  authoritative.
- `a3/docs/interfaces/IF-008-v1.1-acceptance.md` accepts all eight §6 conditions.
- `a3/docs/06-stage0-review-response.md` reviews against A4 `3ae7aca` and records A1 ACCEPT.
- `a3/docs/interfaces/IF-009-a3-review.md` accepts IF-009 v1.0 for the hardware boundary at
  `d02598a` / `3f78477`.

`make -C a3 check` exit 0: matrix PASS, deliverables PASS, 19 tests PASS.
`make -C a3 check-a1-headers A1_INCLUDE=<A1 74b1471>/include` exit 0.

XB-017, XB-018 and XB-019 are Closed. XB-005 remains Open because no device test ran.

## 5. Blocker disposition

| ID | Before | After | Basis |
| --- | --- | --- | --- |
| XB-001 | Open | **Closed** | A1 ACCEPT at `b9a69d8`, A1 `74b1471`; A3 accepts §6 |
| XB-002 | Open | **Signed** | A1 answered all 12; A2 accepts all 12 at `6d305b8` |
| XB-003 | Open | **Open** | A1/A3 accept IF-009 v1.0 at `d02598a`; A2 signature record pending |
| XB-004 | Open | **Open** | D-005 pending; MBR harness is not evidence |
| XB-005 | Open | **Open** | no QEMU driver, AHCI, DMA or hardware test |
| XB-012 | Open | **Open** | `protected:false`, rulesets `[]`, `CODEOWNERS` 404 |
| XB-013 | Open | **Open** | response/ledger lag the evidence pair and both signatures |
| XB-014 | Open | **Closed** | A1 re-reviewed `50b7853` / `807c447`; A2 accepts all 12 |
| XB-016 | Open | **Open** | A1 has no `evidence/a1-stage0/` tree |
| XB-017 | Open | **Closed** | A3 `307b919` integrates on A4; package in `a3/`; root docs preserved |
| XB-018 | Open | **Closed** | A3 `307b919` reviews against A4 `3ae7aca` |
| XB-019 | — | **Closed** | A3 accepts all eight §6 conditions at `307b919` |

Snapshot: **6 Open, 12 Closed, 1 Signed**. Open rows are XB-003, XB-004, XB-005, XB-012, XB-013 and
XB-016. Closed rows are XB-001, XB-006, XB-007, XB-008, XB-009, XB-010, XB-011, XB-014, XB-015,
XB-017, XB-018 and XB-019. Signed is XB-002.

## 6. Checks actually run

| Check | Command | Exit | Result | Artifact |
| --- | --- | --- | --- | --- |
| A1 headers | `bash tools/check-headers.sh` in A1 `74b1471` | 0 | `RESULT: PASS` | `a1-check-headers-74b1471.log` |
| A2 static | `make check-static` in `a2/` at A2 `64b3458` | 0 | 61 checks, 0 failed | `a2-static-check-64b3458.log` |
| A3 package | `make -C a3 check` at A3 `307b919` | 0 | matrix, deliverables, 19 tests PASS | `a3-check-307b919.log` |
| A3 against A1 headers | `make -C a3 check-a1-headers A1_INCLUDE=<A1 74b1471>/include` | 0 | compile PASS | `a3-a1-header-check-307b919.log` |

These are static/compile checks. They are not QEMU driver runs, AHCI probes, DMA transfers or
hardware tests. The A4 CI job named `build-and-boot` runs `make test-mbr-harness`; it is an MBR
fixture/harness smoke, not kernel boot.

## 7. Governance

The 2026-11-23 recheck returns:

- `branches/main`: `"protected": false`, `"protection.enabled": false`, required checks `[]`.
- `rulesets`: `[]`.
- `CODEOWNERS` on `main`: HTTP 404.
- The protection endpoint returns 403 for this fine-grained PAT (`Resource not accessible by
  personal access token`); that is a token limit, not evidence of protection.

D-031 / XB-012 remain Open. A4 requests the Director enable branch protection, require the Stage 0
check and land a base-branch `CODEOWNERS` before Stage 1.

## 8. Limits

- Stage 0 is **not accepted**. D-006 remains Proposed.
- A1's ACCEPT is an interface decision, not hardware or runtime validation.
- A2's IF-009 work is static-only; no PE loads.
- A3 has no driver, AHCI probe or DMA transfer.
- No branch is merged to `main`. A3 has no PR.
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
  `docs/decisions/decision-log.md` at `3ae7aca` plus this commit.
