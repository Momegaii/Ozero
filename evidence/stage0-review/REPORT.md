# A4 Stage 0 review — report back

| Field | Value |
| --- | --- |
| Date | 2026-11-23 |
| Author | A4 |
| Review basis | Director-supplied Stage 0 review, 23 Nov 2026 |
| Repository | `Momegaii/Ozero` |
| Branch | `a4/stage0-review-followup` |
| Commit under test | `43961551442009aac8189e5edca3f5c7af43a2cd` |
| Parent | `5a50c0d28f323372cad5126baf6645df07407975` (A4 control plane import) |
| Base | `147cc86c027cef7cec42525024d26dcccf66d7db` (`origin/main`, "Initial commit") |
| Evidence commit | child of `4396155` on the same branch |
| Stage state | Submitted for review. Not approved. |

## 1. Exact commits, branch and files

Commit graph:

```
* 4396155 (HEAD -> a4/stage0-review-followup) docs: implement Stage 0 review actions
* 5a50c0d docs: import A4 Stage 0 control plane
* 147cc86 (origin/main, origin/HEAD, main) Initial commit
```

`git fetch origin --prune` succeeded. `origin/main` is still `147cc86`. No A1, A2 or A3 branch,
tag or commit exists in the remote or in the local clone. The full audit is in `repo-audit.txt`.

This is the `4396155` snapshot. The current cross-agent refs are in §9 and in
[`cross-agent-verification-2026-11-23-r3.md`](cross-agent-verification-2026-11-23-r3.md).

Files changed in `4396155` (26 files, +474/−107):

```
.github/workflows/stage0.yml
AGENTS.md
CODEOWNERS
Makefile
README.md
docs/README.md
docs/adr/0002-branch-isolation-and-review-gate.md
docs/adr/README.md
docs/architecture/interfaces.md
docs/architecture/ownership.md
docs/dashboard/director-dashboard.md
docs/decisions/decision-log.md
docs/process/branching-and-review.md
docs/process/build-and-test.md
docs/process/ci-safety.md
docs/process/integration-checklist.md
docs/process/reporting.md
docs/process/test-targets.md
docs/process/trust-boundaries.md
docs/templates/decision-log.md
docs/templates/interface.md
evidence/stage0/README.md
scripts/audit-repos.sh
scripts/check-ci-safety.sh
scripts/list-targets.sh
tests/targets.tsv
```

The A4 control plane itself was imported in `5a50c0d` from the local Stage 0 work. That local
repository is `/workspace/project` on branch `master` at `d6b7af0`; its commits are `e12983e` and
`d6b7af0`. It has no remote and is not independently fetchable.

## 2. Commands and raw output

Every command below was run at `4396155` on the reference environment. Raw output is in the named
file; the lines quoted are verbatim.

| Command | Exit | Raw output |
| --- | --- | --- |
| `git fetch origin --prune` | 0 | `repo-audit.txt` |
| `scripts/audit-repos.sh /workspace/project` | 0 | `repo-audit.txt` |
| `scripts/check-toolchain.sh --strict` | 0 | `toolchain.txt` |
| `make targets` | 0 | `targets.txt` |
| `make check-ci-safety` | 0 | `ci-safety.log` |
| `make clean && make ci` | 0 | `ci.log` |
| `make verify-repro` | 0 | `repro.log` |
| `make verify-clean` | 0 | `clean-checkout.log` |
| missing-marker negative | 1 | `negative-missing-marker.log` |
| corrupt-image negative | 2 | `negative-corrupt-image.log` |
| zero-timeout negative | 1 | `negative-timeout.log` |
| CI-safety negative | 1 | `ci-safety.log` |

Key raw lines:

```
$ scripts/check-toolchain.sh --strict
check-toolchain: ok
exit=0

$ make clean && make ci
status:   pass
image:    build/ozero-stage0.img
sha256:   37ad8227720cd7f40b1393a8ad554a78f76d08b09ff97be68b1d2579f5e15319
qemu:     QEMU emulator version 10.0.13 (Debian 1:10.0.13+ds-0+deb13u1)
timeout:  20s
duration: 224ms
test: ok (5 markers in order)
ci: ok
exit=0

$ make verify-repro
ok    ozero-stage0.img   37ad8227720cd7f40b1393a8ad554a78f76d08b09ff97be68b1d2579f5e15319
ok    ozero-stage0.elf   4e2b74c41b38888430f649eb95548ae2c5ebe29e63db97ff19e43ed087825665
ok    manifest.json      6a6be1bdd7ed43520843fbe48252a8e7c4305eac266cecb1649d486e39e477d7
verify-repro: ok at 43961551442009aac8189e5edca3f5c7af43a2cd
exit=0

$ make verify-clean
ci: ok
verify-clean: ok at 43961551442009aac8189e5edca3f5c7af43a2cd
exit=0
```

Negative tests:

```
missing:  OZERO-STAGE0-NEVER-PRINTED
test: FAIL (timeout)
exit=1

test: harness error: image size 513 is not a multiple of 512
exit=2

status:   timeout
missing:  OZERO-STAGE0-BOOT
test: FAIL (timeout)
exit=1

FAIL  privileged container
/workspace/Ozero/scripts/tmp-ci-safety-violation.sh:2:docker run --privileged alpine true
exit=1
```

## 3. Review actions

| # | Action | Closed | Evidence |
| --- | --- | --- | --- |
| 1 | Verify A1/A2/A3/A4 commits; record graph and clean tree | Yes at `4396155`; current refs in §9 | `repo-audit.txt` |
| 2 | Rerun checks in a clean environment | Yes, at `4396155` | `ci.log`, `repro.log`, `clean-checkout.log` |
| 3 | Separate target labels | Yes | `tests/targets.tsv`, `docs/process/test-targets.md` |
| 4 | Decision register statuses and approver | Yes; all Proposed at this snapshot | `docs/decisions/decision-log.md` |
| 5 | A1 review of A3 DMA/platform interfaces as a merge gate | Gate defined; A1 approval of IF-008 v1.1 pending | IF-008, `CODEOWNERS`, integration checklist |
| 6 | Safe CI | Yes, enforced | `docs/process/ci-safety.md`, `scripts/check-ci-safety.sh` |

## 4. Decisions needed

All are **Proposed**. None is approved.

| ID | Decision | Owner | Director approver |
| --- | --- | --- | --- |
| D-001 | Confirm the A1/A2/A3 domain split and team slugs | A4 | Project Director |
| D-002 | Approve branch isolation and the review gate | A4 | Project Director |
| D-003 | Approve the Stage 0 build and QEMU MBR harness approach | A4 | Project Director |
| D-004 | Approve agent trust boundaries | A4 | Project Director |
| D-005 | Choose the boot protocol and handoff; Limine is a candidate only | A1 | Project Director |
| D-006 | Accept Stage 0 | A4 | Project Director |
| D-007 | Approve IF-008 and the A1 sign-off gate for A3 DMA/platform | A1 + A3 | Project Director |
| D-008 | Approve publishing the A4 review branch | A4 | Project Director |

## 5. Unresolved risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| A4 branch is unpushed | Independent review cannot fetch the commit | D-008; push after approval |
| A1/A2/A3 have no commits | Cross-agent gates are defined but untested | Start A1–A3 after D-001 |
| MBR fixture may be read as kernel boot | False boot claim | Naming rule and explicit not-tested list |
| CI safety check is static | A novel bypass is possible | Review still required; no opt-in jobs yet |
| QEMU is not hardware | Compatibility claims would be wrong | Hardware matrix remains a separate target |
| Reproducibility depends on binutils 2.44 | Other versions may differ | Version is pinned and recorded |

## 6. Not tested

- UEFI boot.
- Limine loading an Ozero kernel.
- Ozero kernel entry, memory setup or any kernel subsystem.
- Real-hardware boot or any device matrix.
- PE parsing, loading or Windows compatibility.
- Public header ABI.
- Any A1, A2 or A3 code, because no such code exists in this repository.
- Branch protection and CODEOWNERS enforcement, because they are repository settings and the team
  slugs are placeholders.

## 7. Definition of done

| Item | Met | Evidence |
| --- | --- | --- |
| Clean clone reproduces A4 results | Yes | `clean-checkout.log` |
| Evidence links exact commits and raw outputs | Yes | this file and `README.md` |
| MBR harness never described as kernel boot | Yes | naming rule and wording audit |
| Director decisions pending until approved | Yes | register, all Proposed |
| Cross-agent API sign-off precedes dependent implementation | Yes, as a gate | integration checklist and PR gate |

## 8. Next actions

1. Director decides D-001 to D-008.
2. On D-008 approval, push `a4/stage0-review-followup` and open a PR against `main`.
3. A1 records the Limine handoff in IF-001 and answers D-005.
4. A2 records the PE fixture contract in IF-009 before any PE test.
5. A3 records the DMA/platform contract in IF-008 and requests A1 sign-off.

## 9. Post-review addendum — 2026-11-23

The Director completed a Stage 0 integration review and kept Stage 0 **NOT ACCEPTED**. A4 continued
from the existing branch; it did not restart Stage 0 and did not merge into `main`.

### Current refs

| Ref | Commit | State |
| --- | --- | --- |
| `origin/main` | `147cc86` | unchanged |
| `origin/a1/stage0-review` | `30ee4f0` | PR #1, open, draft; second-round response |
| `origin/agent/a2-stage0` | `807c447` | PR #2, open, draft; evidence at `50b7853` |
| `origin/a3/stage0-review-response` | `15cd9ce` | published; no PR; IF-008 v1.1 at `b9a69d8` |
| `origin/a4/stage0-review-followup` | `245007d` plus documentation and audit commits | published; PR #3 open |

### Director A4 actions

1. **Open the A4 Stage 0 review PR** — done: [#3](https://github.com/Momegaii/Ozero/pull/3).
2. **Refresh the audit after A3 is published** — done: A3 `a3/stage0-review-response` at `15cd9ce`
   is included; XB-011 is Closed. A3 has no PR, and none is implied.
3. **Verify A1/A2/A3 evidence and ancestry** — A1 at `30ee4f0`, A2 at `807c447` with evidence at
   `50b7853`, A3 at `15cd9ce` with IF-008 v1.1 at `b9a69d8`. A1 reviewed A2 at the older `f324b28`
   (XB-014) and keeps its command output inline rather than in `evidence/` (XB-016). A1 has not
   reviewed A3's v1.1 (XB-001) or answered `A1-REV-01…12` (XB-002). A2's response still cites
   `8bac650` and its contract review cites A1 `90b004f` / `include/o0/` (XB-013). A3 cites a stale
   A4 head `076ed82` and a stale D-009 `include/o0/` (XB-018); its branch rewrites the shared root
   `README.md` / `AGENTS.md` and adds `platform.h` / `block.h` under A1-owned `include/ozero/`
   (XB-017). See
   [`cross-agent-verification-2026-11-23-r3.md`](cross-agent-verification-2026-11-23-r3.md).
4. **Resolve namespace and ownership** — D-009 adopts `include/ozero/`; A1's `03f8883` refactor
   makes it the only public root. D-010 assigns `tools/check-headers.sh` and
   `tests/abi_static_asserts.c` to A1, with A4 owning CI integration and A3 owning other `tools/`
   content. `CODEOWNERS` and the ownership map are updated.
5. **Keep MBR harness and kernel boot separate** — the only executed boot test is
   `mbr-harness-smoke`; the Limine/kernel target remains not implemented.
6. **Confirm branch protection and required checks** — `main` reports `"protected": false` and
   `"protection.enabled": false`; the rulesets list is empty; `CODEOWNERS` is 404 on `main`.
   Required checks are not enabled (D-031 / XB-012). See
   [`github-settings.txt`](github-settings.txt).

### Decision status

D-008, D-009 and D-010 are **Approved** by the Director's 2026-11-23 instruction. D-001–D-007 and
D-011–D-031 remain **Proposed**. D-006 (accept Stage 0) is explicitly **not** approved.

### Tests at the published head

`make ci` passes at the published head: 5 serial markers in order, QEMU 10.0.13, no network, no
display, no default devices. The later round-2 commits are documentation and evidence only.

### Still not tested

UEFI, Limine loading an Ozero kernel, Ozero kernel entry, real hardware, PE loading, Windows
compatibility, A3 driver code or DMA, and branch-protection enforcement. A3's `make check` and
A1-header compile pass, but they are static checks; no AHCI probe or device test ran.
