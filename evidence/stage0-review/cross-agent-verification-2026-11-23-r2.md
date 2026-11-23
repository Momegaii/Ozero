# Cross-agent verification — round 2, 2026-11-23

This is the post-Director-review snapshot. It replaces the refs in the first verification note; the
first note stays as the earlier snapshot.

| Ref | Commit | State |
| --- | --- | --- |
| `origin/main` | `147cc86` | unchanged |
| `origin/a1/stage0-review` | `115a7f4` | PR #1, open, draft; second-round response |
| `origin/agent/a2-stage0` | `807c447` | PR #2, open, draft; evidence-only commit after code `50b7853` |
| A3 | none | no branch, commit or interface |
| `origin/a4/stage0-review-followup` | `9ef13bd` plus this audit commit | published; PR #3 open |

## Commit ancestry

Commands run in `/workspace/Ozero` after `git fetch origin --prune`:

```text
$ git merge-base --is-ancestor origin/main origin/a1/stage0-review
exit=0
$ git rev-list --count origin/main..origin/a1/stage0-review
14
$ git diff --stat origin/main..origin/a1/stage0-review | tail -1
 27 files changed, 3516 insertions(+)

$ git merge-base --is-ancestor origin/main origin/agent/a2-stage0
exit=0
$ git rev-list --count origin/main..origin/agent/a2-stage0
4
$ git diff --stat origin/main..origin/agent/a2-stage0 | tail -1
 48 files changed, 9967 insertions(+)

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
| A1 | No `evidence/` directory | Command output is inline in `docs/stage0-report.md`. It is readable but not a raw log tied to a commit (XB-016). A1 states no kernel, ISO or QEMU boot. |
| A2 | 10 files under `a2/docs/evidence/` | Evidence was regenerated at code revision `50b7853`; `807c447` adds only the evidence files. `git diff 50b7853..807c447 -- ':!a2/docs/evidence'` is empty. The response document `11-stage0-review-response.md` still cites the import revision `8bac650`, so that document's provenance is stale (XB-013). |
| A3 | None | No branch, so no evidence to verify. |
| A4 | `evidence/stage0-review/` | Review logs, audit, negative tests, CI safety, cross-agent notes and this file. |

A1's Limine pin (v12.11.1, base revision 6, header and binary hashes) is recorded in its report and
source index. A4 did not independently fetch or hash the upstream release; the claim is A1's, not an
A4 verification.

A2's evidence counts, read from the committed logs:

```text
unit-tests.txt:        Ran 40 tests ... OK
corpus-lint.txt:       559 checks, 0 failed
pe-structure-checks:   263 checks, 0 failed
negative-tests.txt:    8 negative cases produced the expected failure
reproducibility.txt:   manifest matches: 5 fixtures
```

All 17 A2 corpus entries remain `not-run`; no Windows application was executed.

A2 also posted an A1 review request on PR #1 under XB-002. The request lists A1-REV-01 to
A1-REV-12 and points at `a2/docs/12-a1-contract-review.md`. A1 answered with conditions in
`docs/a2-interface-review.md`, but reviewed A2 at `f324b28`, not the current `50b7853` / `807c447`.
XB-014 tracks the re-review; XB-002 remains Open until A1 confirms the current revision.

## A1 second-round response

A1 moved from `90b004f` to `115a7f4` during this round:

| Commit | Change |
| --- | --- |
| `03f8883` | Refactor to `include/ozero/` as the only public header root (A1 D-009) |
| `164360e` | Expose driver DMA callbacks and A2 thread primitives |
| `c50e385` | Answer the Director's second review; add A1 decisions, A2 review and A3 DMA contract |
| `115a7f4` | Record second-round evidence and hashes |

A1 answers its four Director actions on its branch:

- Namespace: `include/ozero/` only; `include/o0/` removed; `tools/check-headers.sh` rejects the old
  path. A4 aligned D-009 and its map to this choice.
- Tools ownership: A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`; A4 owns CI
  integration; A3 owns other `tools/`. A4 aligned D-010 and `CODEOWNERS`.
- A2 review: `docs/a2-interface-review.md` reviews 14 requirement groups with conditions. It is not
  an ABI freeze and is at the older A2 head (XB-014).
- A3 DMA contract: `docs/a3-dma-contract.md` fixes `struct o0_device_ops` and the Stage 1
  ring-0-only rule. Final IF-008 sign-off waits for A3's published `platform.h` and `block.h`
  (XB-001).

A1's D-009–D-030 block collided with A4's D-009–D-011. A4 merged D-009 and D-010 on the same
topics, kept A1's D-011 for the A2 requirements, recorded D-012–D-030 as Proposed, and renumbered
its own branch-protection row to D-031 (XB-015).

## Pull requests

| PR | Head | Base | Draft |
| --- | --- | --- | --- |
| #1 | `a1/stage0-review` at `115a7f4` | `main` | yes |
| #2 | `agent/a2-stage0` at `807c447` | `main` | yes |
| #3 | `a4/stage0-review-followup` at `9ef13bd` plus this audit commit | `main` | no |

## Namespace and ownership resolution

- D-009: the canonical public header namespace is `include/ozero/`. A1's `03f8883` refactor makes
  it the only public root; `tools/check-headers.sh` rejects `include/o0/`.
- D-010: A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`; A4 owns CI integration;
  A3 owns other `tools/` content. Specific `CODEOWNERS` entries override the `tools/` default.
- `CODEOWNERS`, `ownership.md`, `interfaces.md`, `repository-layout.md` and ADR-0001 now use
  `include/ozero/`.

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
