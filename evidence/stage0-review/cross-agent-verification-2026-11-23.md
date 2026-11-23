# Cross-agent repository verification — 2026-11-23

Snapshot taken with `git fetch origin --prune` and the GitHub pull-request API. The remote can
advance after this snapshot; the refs below are the ones verified.

| Ref | Commit | State |
| --- | --- | --- |
| `origin/main` | `147cc86` | unchanged |
| `origin/a1/stage0-review` | `90b004f` | PR #1, open, draft |
| `origin/agent/a2-stage0` | `f324b28` | PR #2, open, draft |
| A3 branch | none | not started |
| `a4/stage0-review-followup` | `596fc6f` at snapshot | local only; push denied |

The full ref graph and clean-tree state are in `repo-audit.txt`.

## A1 — `a1/stage0-review`

Verified: 9 commits, 24 files, 2,981 insertions against `main`. The branch adds the boot contract,
CPU baseline, interface draft, kernel architecture, source pins, Stage 1 plan, `include/o0/`
headers, one ABI test and `tools/check-headers.sh`.

What the report claims, and what the tree supports:

| Claim | Verification |
| --- | --- |
| Limine pinned to v12.11.1, base revision 6, header and binary hashes | Recorded in `docs/sources.md` and `docs/boot-contract.md`; report quotes `sha256sum`, `SUPPORTED_BASE_REVISION 6` and `diff` output |
| Header/ABI checks passed | Raw output is inline in `docs/stage0-report.md`; there is no `evidence/` directory or log file |
| No kernel, no QEMU boot, no ISO | Stated in report §7 and consistent with the tree; no kernel or image files exist |
| A3 interface review signed with one blocker | **Not accepted as A3 sign-off.** A3 has no branch and has published no interface. A1 reviewed its own draft in `docs/interface-draft.md` §11. XB-001 stays Open until A3 publishes a version and A1 signs that version |

A1 integration findings:

- Namespace mismatch: A1 uses `include/o0/`; the A4 ownership map and `CODEOWNERS` name
  `include/ozero/`. One must change before merge (XB-007).
- Ownership conflict: A1 adds `tools/check-headers.sh`, but `tools/` is A3-owned in the current
  map. Move it to `scripts/` or obtain A3 review (XB-008).
- A1 adds `tests/abi_static_asserts.c` under A4-owned `/tests/`; A4 review and a `tests/targets.tsv`
  row are required.
- `AGENTS.md`, `README.md` and `docs/README.md` overlap with the A4 control plane. Integration must
  keep the A4 versions and merge A1's content into them, not replace them.
- A1's evidence is a Markdown report, not raw logs tied to a commit. It is readable but weaker than
  the A4 evidence standard. No technical claim is accepted by A4 on the strength of the report alone.

## A2 — `agent/a2-stage0`

Verified: 2 commits, 47 files, all under `a2/`. The package is isolated and does not touch the A4
control plane.

| Claim | Verification |
| --- | --- |
| Static-only; no Windows execution | 17 corpus entries are `not-run`; evidence README and review response state L0 only |
| 35 unit tests | `a2/docs/evidence/unit-tests.txt` ends with `Ran 35 tests ... OK` |
| 558 corpus lint checks | `a2/docs/evidence/corpus-lint.txt` ends with `558 checks, 0 failed` |
| 263 structure checks | `a2/docs/evidence/pe-structure-checks.txt` reports `263 checks, 0 failed` |
| 8 negative cases | `a2/docs/evidence/negative-tests.txt` reports `8 negative cases produced the expected failure` |
| Fixture rebuild matches manifest | `a2/docs/evidence/reproducibility.txt` reports `manifest matches: 5 fixtures` |

A2 integration findings:

- Evidence headers cite `git: 8bac650`, while the branch head is `f324b28` and
  `a2/tools/build_fixtures.py` changes in `f324b28`. Regenerate the evidence at the final branch
  head before merge so the logs and the generating tool match (XB-009).
- A2 asks A1 to review `a2/docs/03-a1-interface-requirements.md`. No A1 review is recorded, and A1's
  branch does not reference it. XB-002 stays Open.
- `a2/` is low-conflict. After the A4 control plane lands, add a root `test-pe-static` target that
  runs `make -C a2 check-static`; do not merge A2's `a2/Makefile` into the root Makefile.

## A3

No branch, commit, pull request or interface version exists. XB-001 and XB-005 stay Open. A1's
"one blocker" note is not a substitute for A3's interface.

## A4

The branch was published at `84fe81a` and now tracks `origin/a4/stage0-review-followup`. No Stage 0
pull request is open; XB-006 is Closed and XB-010 tracks the PR. The branch includes the CI workflow,
so the push needed the token's `workflow` scope.

## Recommended merge order

1. A4 control plane first: it defines `CODEOWNERS`, `Makefile`, `tests/targets.tsv`, `scripts/` and
   the process documents.
2. A1 rebased onto that base: resolve the three shared docs, the `include/o0/` namespace, the
   `tools/` ownership and the ABI test registration.
3. A2 rebased onto that base: keep `a2/` intact and add the root `test-pe-static` target.
4. A3 only after it publishes IF-008 and A1 signs it.

No row in this note is an acceptance. The Director still owns D-001 to D-008.
