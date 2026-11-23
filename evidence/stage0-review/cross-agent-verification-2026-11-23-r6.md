# Cross-agent verification — R3 audit (2026-11-23)

| Field | Value |
| --- | --- |
| Branch | `a4/stage0-review-followup` |
| A4 head at audit start | `eaca9219dec5fdb1cf8ad5400c7e1fa4e42dc39b` |
| `main` | `147cc86c027cef7cec42525024d26dcccf66d7db` (unchanged) |
| A1 | `2693a34a058506e4755948989fb420c9a20a91ea` (PR #1, draft) |
| A2 | `d83cfbe9f1f4095d0df53f0dfca20162f255fbf1` (PR #2, draft); code `f6ed1d8`, evidence `d83cfbe` |
| A3 | PR #4 `48d434f961ff0639a38ee50ba456e99d8ebb72da` against `a4/stage0-review-followup` |
| A4 PR | [#3](https://github.com/Momegaii/Ozero/pull/3), head `eaca921` plus this audit |
| Stage 0 | **NOT ACCEPTED** |
| D-006 | **Proposed** |
| Blocker snapshot | **5 Open, 14 Closed, 1 Signed** |

Raw commands and outputs are in
[`cross-agent-commands-2026-11-23-r6.log`](cross-agent-commands-2026-11-23-r6.log).

## 1. A3 PR #4 review

PR #4 is open, non-draft, base `a4/stage0-review-followup`, head `a3/stage0-review-response` at
`48d434f`. GitHub reports `MERGEABLE`, 56 changed files, +7358/−0. The final diff from `b4232a0`
contains only:

- 44 files under `a3/`: docs, matrix, tests, tools, Makefile, evidence and PDFs.
- 12 files under `include/ozero/`: A1's ten public headers plus A3's `platform.h` and `block.h`.

No `README.md`, `AGENTS.md`, root `Makefile`, `docs/`, `scripts/`, `tests/`, `harness/`,
`toolchain/`, `evidence/` or `CODEOWNERS` file is changed. There are no symlinks or executable
mode changes outside A3's two shell tools.

Header check:

- `include/ozero/{boot,cpu,driver,ipc,irq,mm,o0,status,thread,types}.h` are byte-identical to A1
  `74b1471` and to A1 PR #1 `2693a34`.
- `include/ozero/platform.h` and `block.h` match `a3/include/ozero/` in the same PR.
- A3's root `platform.h` and `block.h` are the A1-approved IF-008 v1.1 files.

Merge test: from A4 `eaca921`, `git merge --no-commit --no-ff origin/pr-4` reports
`Automatic merge went well; stopped before committing as requested`; the staged change is the same
56 additions. A4 did **not** commit the merge.

Checks:

- PR #4 CI `build-and-boot`: success, 1m25s, run 45185176782.
- `make -C a3 check` at `48d434f`: 19 tests, `OK`; matrix and deliverables pass.
- `make -C a3 check-a1-headers A1_INCLUDE=/tmp/a1include/include` at `48d434f`: exit 0 against A1
  `74b1471`.

Ownership: `CODEOWNERS` now lists `/a3/` as `@ozero/a3-userland @ozero/a4-integration`. Root
`include/ozero/` remains A1/A4, with `platform.h` and `block.h` A3/A1. This is a documentation and
review-map change; branch protection is not enabled on `main`.

Decision: **not merged.** D-007 and D-021 remain Proposed. The integration checklist requires both
decisions and Director authorization before an A3 merge. A clean merge test and green CI are
necessary, not sufficient. XB-020 stays Open until the A4 tree proves integration.

## 2. A2 IF-009 contract and evidence pair

A2 PR #2 advanced to `d83cfbe`, with code `f6ed1d8` and evidence `d83cfbe`. The change from
`64b3458` is docs, evidence and `a2/tests/test_tools.py`; the fixture manifest and fixture sources
are unchanged.

- `git diff --stat f6ed1d8 d83cfbe -- ':!a2/docs/evidence'` is empty.
- Every `a2/docs/evidence/*.txt` header names `f6ed1d8`.
- `git diff --stat d02598a f6ed1d8 -- a2/fixtures a2/tools/build_fixtures.py` is empty.
- The manifest blob is `f505cb4d9403b65d860c92d574bd736270db446a` at both revisions.
- `make -C a2 check-static` at `d83cfbe`: 228 docs checks and 61 structure checks, 0 failed, exit 0.

Contract status at `d83cfbe`:

- A1 **ACCEPT** with conditions at `74b1471` (`a17d126`).
- A3 **ACCEPT** for the hardware boundary at `307b919`.
- A2 signature recorded at `6d305b8`; signed contract `d02598a`.
- A4 records IF-009 v1.0 as **Signed**; XB-003 **Closed**.

The A2 ledger still names `6d305b8` / `64b3458`, not the current `f6ed1d8` / `d83cfbe`; the
response revision history also stops at the old pair. XB-013 remains **Open** for that pair lag.
The signature is not loader approval and does not authorize a PE run; no loader exists.

## 3. A1 raw evidence

A1 PR #1 is at `2693a34`. The raw log is committed at
`docs/evidence/a1-raw-checks-2026-11-23-r2.txt`, added at `9cda79c`. It records the commands and raw
output for:

- immutable refs and `git diff --check`;
- A1 header/ABI checks, ending `RESULT: PASS`;
- A2 `6d305b8` / `64b3458` contract and evidence reads;
- A3 `307b919` IF-008 acceptance and A4 `b4232a0` registry reads.

The path differs from the earlier suggested `evidence/a1-stage0/`, but the committed raw log meets
the evidence rule. XB-016 is **Closed**.

## 4. Governance and decisions

Live GitHub state:

- `branches/main` reports `protected:false`.
- Repository rulesets: `[]`.
- `CODEOWNERS` on `main`: HTTP 404.
- The branch-protection endpoint returns HTTP 403 for the supplied PAT, so A4 cannot independently
  read the protection object; the branch metadata and ruleset list are the available evidence.
- No required status checks are visible on `main`.

XB-012 / D-031 remain **Open/Proposed**. D-006, D-007, D-021 and D-031 remain **Proposed**. D-008,
D-009 and D-010 remain **Approved**.

## 5. Blocker snapshot

| State | Count |
| --- | --- |
| Open | 5 |
| Closed | 14 |
| Signed | 1 |
| Total | 20 |

Open: XB-004, XB-005, XB-012, XB-013, XB-020. Signed: XB-002. XB-003 and XB-016 moved to Closed in
this audit.

## 6. What this audit does not prove

- No merge of A3 PR #4 into A4, and no merge to `main`.
- No Stage 0 acceptance and no change to D-006.
- No AHCI probe, DMA transfer, kernel boot, UEFI, Limine, PE load or Windows compatibility.
- No hardware validation. A3's checks are static/compile-time only.
- No enforcement of branch protection or required checks.

## Sources

- A3 PR #4: <https://github.com/Momegaii/Ozero/pull/4>
- A2 PR #2: <https://github.com/Momegaii/Ozero/pull/2>
- A1 PR #1: <https://github.com/Momegaii/Ozero/pull/1>
- A4 PR #3: <https://github.com/Momegaii/Ozero/pull/3>
- A2 `a2/docs/13-if-009-pe-fixture-contract.md`, `a2/docs/14-review-ledger.md` at `d83cfbe`.
- A1 `docs/evidence/a1-raw-checks-2026-11-23-r2.txt` at `2693a34`.
- `docs/process/cross-agent-blockers.md`, `docs/process/integration-checklist.md`.
