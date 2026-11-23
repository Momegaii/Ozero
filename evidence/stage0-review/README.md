# Stage 0 review evidence

| Field | Value |
| --- | --- |
| Stage | 0 review follow-up |
| Repository | `Momegaii/Ozero` |
| Branch | `a4/stage0-review-followup` (published) |
| Commit under test | `43961551442009aac8189e5edca3f5c7af43a2cd` (harness baseline) |
| Current head | `HEAD` of `a4/stage0-review-followup`; directive baseline `7208a28` |
| A1 / A2 / A3 refs | `2693a34` / `d83cfbe` (`f6ed1d8` code, `d83cfbe` evidence), integrated at `f0c64a9` / PR #4 `48d434f`, integrated at `44d566e` |
| Import commit | `5a50c0d28f323372cad5126baf6645df07407975` |
| Base commit | `147cc86c027cef7cec42525024d26dcccf66d7db` (`origin/main`) |
| Date | 2026-11-23 |
| Environment | Debian 13, binutils 2.44, GNU make 4.4.1, bash 5.2.37, QEMU 10.0.13 |
| Overall | All checks passed; negative tests failed as required; A2 integrated at `f0c64a9`, A3 at `44d566e`; IF-009 v1.1 implemented, evidence `37e9bf0` |

The report required by the review is [`REPORT.md`](REPORT.md). This file indexes the raw output.

## Results

| Check | Command | Exit | Result | Artifact |
| --- | --- | --- | --- | --- |
| Toolchain | `scripts/check-toolchain.sh --strict` | 0 | exact reference versions | `toolchain.txt` |
| Targets | `make targets` | 0 | five targets, one implemented | `targets.txt` |
| CI safety | `make check-ci-safety` | 0 | 8 files scanned, no findings | `ci-safety.log` |
| Build and MBR harness smoke | `make clean && make ci` | 0 | 5/5 markers, 224 ms on the integrated tree; not kernel boot | `a3-integration-checks-2026-11-23.log`, `ci.log`, `qemu/` |
| A3 package checks | `make -C a3 check` | 0 | 19 tests, `OK`; matrix and deliverables pass | `a3-integration-checks-2026-11-23.log` |
| A3 A1-header check | `make -C a3 check-a1-headers A1_INCLUDE=include` | 0 | A1 headers compile against A3 | `a3-integration-checks-2026-11-23.log` |
| A2 IF-009 v1.1 negative cases | `make -C a2 negative` | 0 | 12 expected failures, 2 sample skips, 0 problems; six v1.1 cases pass | `../../a2/docs/evidence/if-009-v1.1-negative-cases.txt` |
| A2 unit tests | `python3 -m unittest discover -s tests -v` | 0 | 45 tests `OK`, 1 skipped | `../../a2/docs/evidence/unit-tests.txt` |
| GitHub settings audit | `gh api .../branches/main`, `.../rulesets`, `.../contents/CODEOWNERS` | 0 | `protected:false`, rulesets `[]`, `CODEOWNERS` 404 on `main`; no setting changed | `github-settings-a4-2026-11-23.txt` |
| A1 re-verification | `./tools/check-headers.sh` at A1 `2693a34` | 0 | `RESULT: PASS`; static only | `../a1-stage0/a4-verification-2026-11-23.txt` |
| GitHub Actions | `stage0 / build-and-boot` at `bc3f566` | 0 | success in 4m55s; run `45195798408` | `ci-run-bc3f566.txt` |
| Reproducibility | `make verify-repro` | 0 | image, ELF and manifest identical | `repro.log` |
| Clean checkout | `make verify-clean` | 0 | fresh clone at `4396155` passed | `clean-checkout.log` |
| Missing marker | `scripts/test-qemu.sh` with an extra marker | 1 | failed as required | `negative-missing-marker.log` |
| Corrupt image | `scripts/test-qemu.sh` on a 513-byte image | 2 | failed as required | `negative-corrupt-image.log` |
| Timeout | `TIMEOUT=0 scripts/test-qemu.sh` | 1 | failed as required | `negative-timeout.log` |
| CI safety negative | deliberate `--privileged` script | 1 | rejected as required | `ci-safety.log` |

## Recorded values

| Item | Value |
| --- | --- |
| Image | 512 bytes, `55 AA` at offset 510 |
| Image SHA-256 | `37ad8227720cd7f40b1393a8ad554a78f76d08b09ff97be68b1d2579f5e15319` |
| ELF SHA-256 | `4e2b74c41b38888430f649eb95548ae2c5ebe29e63db97ff19e43ed087825665` |
| Manifest SHA-256 | `6a6be1bdd7ed43520843fbe48252a8e7c4305eac266cecb1649d486e39e477d7` |
| Boot duration | 224 ms (timeout 20 s) |
| QEMU | 10.0.13, TCG, no network, no display, no default devices |

The manifest hash is identical to the Stage 0 run because `git archive` omits `.git`, so the
manifest records `commit: unknown` inside both reproducibility copies.

## Artifacts

| Path | Contents |
| --- | --- |
| `REPORT.md` | Required report: commits, files, commands, decisions, risks, not-tested |
| `cross-agent-verification-2026-11-23.md` | A1/A2 branch verification, A3 absence, merge order |
| `cross-agent-verification-2026-11-23-r2.md` | Post-Director-review ancestry, evidence and settings check |
| `cross-agent-verification-2026-11-23-r3.md` | A3 publication, A2 provenance, governance and round-3 blocker state |
| `cross-agent-verification-2026-11-23-r4.md` | Round-4 record; corrected by r5 because it wrongly called A3 integrated on A4 |
| `cross-agent-verification-2026-11-23-r5.md` | R2 audit: A3/A4 path and ancestry proof, corrected XB-003, blocker snapshot, ownership and governance |
| `cross-agent-verification-2026-11-23-r6.md` | R3 audit: PR #4 review, header hashes, clean merge test, A2 pair, A1 raw log, blocker snapshot |
| `takeover-baseline-2026-11-23.txt` | Sole-agent takeover live baseline: refs, PRs, runs and local state |
| `a3-integration-verification-2026-11-23.md` | Sole-agent takeover: A3 merge `44d566e`, tree/header proof and checks |
| `a3-integration-checks-2026-11-23.log` | Raw `make -C a3 check`, `check-a1-headers` and `make ci` output on the integrated tree |
| `final-takeover-checks-2026-11-23.log` | Final `make check-ci-safety` and `make ci` at the takeover tree |
| `ci-run-bc3f566.txt` | GitHub Actions `build-and-boot` success at `bc3f566` (run `45195798408`) |
| `../../docs/architecture/a3-integration-map.md` | File-level ownership map for all 56 A3 files |
| `../../docs/architecture/a2-review-ledger-reconciled.md` | A2 ledger reconciliation at `f6ed1d8` / `d83cfbe` |
| `../../docs/architecture/if-009-v1.1-disposition.md` | IF-009 v1.1 negative-case disposition (XB-021) |
| `../../evidence/a1-stage0/` | A1 raw log copy and A4 re-verification at `2693a34` |
| `../../docs/A4_SOLE_AGENT_MASTER_BRIEF.md` | One consolidated A4 master brief source |
| `../../Ozero_A4_Sole_Agent_Master_Brief_2026-11-23.pdf` | One consolidated A4 master brief PDF; no separate A1/A2/A3 briefs generated |
| `cross-agent-commands-2026-11-23-r3.log` | Raw commands for round-3 heads, stale references and GitHub settings |
| `cross-agent-commands-2026-11-23-r4.log` | Raw commands for round-4 heads, ancestry, checks and governance |
| `cross-agent-commands-2026-11-23-r5.log` | Raw commands for the R2 audit: A4 tree, A3 tree, PR #3 file list, checks and API state |
| `cross-agent-commands-2026-11-23-r6.log` | Raw commands for the R3 audit: PR #4 scope, merge test, header hashes, A2 pair, A1 log, checks and API state |
| `a1-check-headers-74b1471.log`, `a1-abi-check.log` | A1 header check at `74b1471`; ABI check at `30ee4f0` |
| `a2-static-check-64b3458.log` | A2 static checks at `64b3458` / code `6d305b8` |
| `a3-check-307b919.log`, `a3-a1-header-check-307b919.log` | A3 package checks at `307b919`; A1 headers at `74b1471` |
| `director-review-2026-11-23.md` | Director integration review and A4 disposition |
| `github-settings.txt` | Branch protection, rulesets and required-check state (earlier capture) |
| `github-settings-a4-2026-11-23.txt` | Live A4 audit: branch metadata, rulesets, `CODEOWNERS`, owner type, collaborators, PR #3 check |
| `directive-checks-2026-11-23.log` | A2 check, A3 check, CI safety and full CI at `6a934a0`; all exit 0 |
| `../../docs/decisions/D-005-boot-protocol-package.md` | D-005 alternatives, recommendation, compatibility and approvals |
| `../../docs/process/branch-protection-audit.md` | XB-012 audit and exact recommended protection payloads |
| `../../docs/architecture/xb-005-a3-hardware-evidence-plan.md` | XB-005 gap analysis and gated QEMU/L2 plan |
| `../../a2/docs/15-if-009-v1.1-negative-cases.md` | IF-009 v1.1 case table and L0 results |
| `../../a2/docs/evidence/if-009-v1.1-negative-cases.txt` | Raw IF-009 v1.1 negative report at `37e9bf0` |
| `PR_BODY.md` | Submitted pull request body |
| `repo-audit.txt` | Fetch plus commit graph, branches, tags and clean-tree state |
| `toolchain.txt` | Strict toolchain check |
| `targets.txt` | Test-target registry |
| `ci.log` | `make clean && make ci` |
| `repro.log` | Two clean exports and their hashes |
| `clean-checkout.log` | Fresh clone at the review commit |
| `ci-safety.log` | Policy check and deliberate violation |
| `manifest.json` | Build manifest for the review commit |
| `qemu/` | Serial log, result JSON, summary and debug log |
| `negative-*.log` | Failure-path tests |

## Reproduce

```sh
git checkout 43961551442009aac8189e5edca3f5c7af43a2cd
make check-toolchain
make check-ci-safety
make clean && make ci
make verify-repro
make verify-clean
```

## Limitations

- QEMU TCG is not real hardware.
- The image is the A4 MBR harness fixture, not the Ozero kernel.
- A2 is integrated into the A4 candidate at `f0c64a9` and A3 at `44d566e`, but both packages are
  static-only; no AHCI probe, DMA transfer, GPU execution or device test ran. A1 is not integrated.
- PR [#4](https://github.com/Momegaii/Ozero/pull/4) at `48d434f` is integrated into the A4 branch,
  not merged to `main`. D-007 and D-021 remain Proposed; XB-020 is Closed.
- IF-009 v1.0 is Signed and XB-003 is Closed. A4 reconciled A2's ledger at `f6ed1d8` / `d83cfbe`
  (XB-013 Closed). IF-009 v1.1 is implemented at `31e6825` with evidence at `37e9bf0`; XB-021 is
  Closed for implementation, but v1.1 is not signed and v1.0 remains accepted.
- The branch is published and Stage 0 PR [#3](https://github.com/Momegaii/Ozero/pull/3) is open.
  Stage 0 is still not accepted. D-006 remains Proposed.
