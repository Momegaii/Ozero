# Stage 0 review evidence

| Field | Value |
| --- | --- |
| Stage | 0 review follow-up |
| Repository | `Momegaii/Ozero` |
| Branch | `a4/stage0-review-followup` (local, unpushed) |
| Commit under test | `43961551442009aac8189e5edca3f5c7af43a2cd` |
| Import commit | `5a50c0d28f323372cad5126baf6645df07407975` |
| Base commit | `147cc86c027cef7cec42525024d26dcccf66d7db` (`origin/main`) |
| Date | 2026-11-23 |
| Environment | Debian 13, binutils 2.44, GNU make 4.4.1, bash 5.2.37, QEMU 10.0.13 |
| Overall | All checks passed; negative tests failed as required |

The report required by the review is [`REPORT.md`](REPORT.md). This file indexes the raw output.

## Results

| Check | Command | Exit | Result | Artifact |
| --- | --- | --- | --- | --- |
| Toolchain | `scripts/check-toolchain.sh --strict` | 0 | exact reference versions | `toolchain.txt` |
| Targets | `make targets` | 0 | five targets, one implemented | `targets.txt` |
| CI safety | `make check-ci-safety` | 0 | 8 files scanned, no findings | `ci-safety.log` |
| Build and boot | `make clean && make ci` | 0 | 5/5 markers, 224 ms | `ci.log`, `qemu/` |
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
- A1, A2 and A3 have no commits, so no cross-agent interface or driver test has run.
- The branch is unpushed; an independent reviewer cannot fetch it until D-008 is approved.
