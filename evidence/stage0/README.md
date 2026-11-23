# Stage 0 evidence

| Field | Value |
| --- | --- |
| Stage | 0 |
| Commit under test | `e12983e29c07cc987e813f605f178ce76d1dce85` |
| Date | 2026-11-23 |
| Recorded by | A4 |
| Environment | Debian 13, binutils 2.44, GNU make 4.4.1, bash 5.2.37, QEMU 10.0.13 |
| Overall | Build, boot, reproducibility and clean-checkout checks passed |

This directory is the evidence for the Stage 0 claims in
`docs/process/build-and-test.md`. It is a snapshot; the live result is always
`build/qemu/result.json` after `make ci`.

## Results

| Check | Command | Result | Artifact |
| --- | --- | --- | --- |
| Toolchain | `scripts/check-toolchain.sh --strict` | pass | `toolchain.txt` |
| Build and boot | `make clean && make ci` | pass, 5/5 markers | `ci.log` |
| Reproducibility | `make verify-repro` | pass, identical image, ELF and manifest | `repro.log` |
| Clean checkout | `make verify-clean` | pass in a fresh clone | `clean-checkout.log` |
| Missing marker | `scripts/test-qemu.sh` with an extra marker | failed as required, exit 1 | `negative-missing-marker.log` |
| Corrupt image | `scripts/test-qemu.sh` on a 513-byte image | failed as required, exit 2 | `negative-corrupt-image.log` |
| Timeout | `TIMEOUT=0 scripts/test-qemu.sh` | timed out as required, exit 1 | `negative-timeout.log` |

## Recorded values

| Item | Value |
| --- | --- |
| Image size | 512 bytes, signature `55 AA` at offset 510 |
| Image SHA-256 | `37ad8227720cd7f40b1393a8ad554a78f76d08b09ff97be68b1d2579f5e15319` |
| ELF SHA-256 | `4e2b74c41b38888430f649eb95548ae2c5ebe29e63db97ff19e43ed087825665` |
| Manifest SHA-256 | `6a6be1bdd7ed43520843fbe48252a8e7c4305eac266cecb1649d486e39e477d7` |
| Boot duration | 225 ms (timeout 20 s) |
| QEMU | 10.0.13, TCG, no network, no display, no default devices |

The SHA-256 values above come from `repro.log`. `manifest.json` records the same hashes and the
commit.

## Artifacts

| Path | Contents |
| --- | --- |
| `ci.log` | `make clean && make ci`: toolchain, build, boot test |
| `repro.log` | Two `git archive` builds and their hash comparison |
| `clean-checkout.log` | Fresh clone, `make ci`, scrubbed temporary path |
| `toolchain.txt` | Strict toolchain check |
| `manifest.json` | Build manifest from the tested commit |
| `qemu/serial.log` | Raw COM1 output from the passing run |
| `qemu/result.json` | Machine-readable result, portable command line |
| `qemu/summary.txt` | Human-readable result |
| `negative-*.log` | Failure-path tests required by the integration checklist |

## Reproduce

```sh
git checkout e12983e29c07cc987e813f605f178ce76d1dce85
make check-toolchain
make clean && make ci
make verify-repro
make verify-clean
```

## Limitations

- QEMU TCG is not real hardware. A passing boot says the image and the harness work together; it
  does not prove hardware compatibility.
- The boot image is the A4 harness fixture, not the Ozero kernel. It makes no boot-protocol
  decision.
- Reproducibility is bit-identical only with the reference binutils version recorded in
  `toolchain/toolchain.lock`.
