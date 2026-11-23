# Cross-agent verification — round 2, 2026-11-23

This is the post-Director-review snapshot. It replaces the refs in the first verification note; the
first note stays as the earlier snapshot.

| Ref | Commit | State |
| --- | --- | --- |
| `origin/main` | `147cc86` | unchanged |
| `origin/a1/stage0-review` | `90b004f` | PR #1, open, draft |
| `origin/agent/a2-stage0` | `f324b28` | PR #2, open, draft |
| A3 | none | no branch, commit or interface |
| `origin/a4/stage0-review-followup` | `5642c16` at snapshot | published; PR opened after this snapshot |

## Commit ancestry

Commands run in `/workspace/Ozero` after `git fetch origin --prune`:

```text
$ git merge-base --is-ancestor origin/main origin/a1/stage0-review
exit=0
$ git rev-list --count origin/main..origin/a1/stage0-review
10
$ git diff --stat origin/main..origin/a1/stage0-review | tail -1
 24 files changed, 2983 insertions(+)

$ git merge-base --is-ancestor origin/main origin/agent/a2-stage0
exit=0
$ git rev-list --count origin/main..origin/agent/a2-stage0
2
$ git diff --stat origin/main..origin/agent/a2-stage0 | tail -1
 47 files changed, 9600 insertions(+)

$ git merge-base --is-ancestor origin/main origin/a4/stage0-review-followup
exit=0
$ git rev-list --count origin/main..origin/a4/stage0-review-followup
8
$ git diff --stat origin/main..origin/a4/stage0-review-followup | tail -1
 78 files changed, 3895 insertions(+), 2 deletions(-)

$ git ls-remote --heads origin 'refs/heads/a3/*' 'refs/heads/agent/a3*'
(no output)
```

A1's branch contains a merge of `main` (`9ddaf4a`), so the count includes the merge commit. A1 and
A2 are ancestors of `main`; neither rewrote the shared base. A3 cannot be checked because no ref
exists.

## Evidence check

| Agent | Evidence present | Finding |
| --- | --- | --- |
| A1 | No `evidence/` directory | Command output is inline in `docs/stage0-report.md`. It is readable but not a raw log tied to a commit. A1 states no kernel, ISO or QEMU boot. |
| A2 | 10 files under `a2/docs/evidence/` | Unit tests, corpus lint, PE structure checks, negative cases, findings, API set and reproducibility. Headers cite `8bac650`; branch head is `f324b28`. |
| A3 | None | No branch, so no evidence to verify. |
| A4 | `evidence/stage0-review/` | Review logs, audit, negative tests, CI safety, cross-agent notes and this file. |

A1's Limine pin (v12.11.1, base revision 6, header and binary hashes) is recorded in its report and
source index. A4 did not independently fetch or hash the upstream release; the claim is A1's, not an
A4 verification.

A2's evidence counts, read from the committed logs:

```text
unit-tests.txt:        Ran 35 tests ... OK
corpus-lint.txt:       558 checks, 0 failed
pe-structure-checks:   263 checks, 0 failed
negative-tests.txt:    8 negative cases produced the expected failure
reproducibility.txt:   manifest matches: 5 fixtures
```

All 17 A2 corpus entries remain `not-run`; no Windows application was executed.

## Pull requests

| PR | Head | Base | Draft |
| --- | --- | --- | --- |
| #1 | `a1/stage0-review` | `main` | yes |
| #2 | `agent/a2-stage0` | `main` | yes |
| A4 | `a4/stage0-review-followup` | `main` | opened after this snapshot |

## Namespace and ownership resolution

- D-009: the canonical public header namespace is `include/o0/`. A1's headers already use it.
- D-010: `tools/check-headers.sh` is A1-owned with A3 review; the specific `CODEOWNERS` entry
  overrides the A3-owned `tools/` default.
- `CODEOWNERS`, `ownership.md`, `interfaces.md`, `repository-layout.md` and ADR-0001 now use
  `include/o0/`.

## GitHub settings

`main` reports `"protected": false`; the repository rulesets endpoint returns an empty list. Required
status checks are therefore not enabled. The protection endpoints return 403 because the available
tokens lack Administration: read. See `github-settings.txt`.

## MBR harness versus kernel boot

The only executed boot test is `mbr-harness-smoke`: a 512-byte MBR fixture in QEMU with five serial
markers. It does not load Limine, does not enter an Ozero kernel and is not UEFI. The
`future-limine-kernel-boot` target remains not implemented.

## A3 audit refresh

A3 has not published. The repository audit is refreshed with A1, A2 and A4, but the Director's
request to refresh after A3 publication remains open as XB-011.
