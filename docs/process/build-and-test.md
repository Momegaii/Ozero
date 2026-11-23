# Stage 0 build and test plan

| Field | Value |
| --- | --- |
| Status | **Tested** on the reference toolchain (see `evidence/stage0/`) |
| Owner | A4 |
| Approver | Project Director for stage acceptance |

## Goal

A fresh checkout must build and boot with only the documented tools, produce the same bytes twice,
and fail loudly when the boot contract is broken.

## Pinned toolchain

The lock file is [`toolchain/toolchain.lock`](../../toolchain/toolchain.lock). `make check-toolchain`
compares the local tools against it.

| Tool | Reference version | Minimum | Why it matters |
| --- | --- | --- | --- |
| GNU as / ld / objcopy | 2.44 | 2.42 | Bit-identical output requires the same binutils |
| GNU make | 4.4.1 | 4.3 | `$(file ...)` and `.ONESHELL` behaviour |
| bash | 5.2 | 5.1 | `set -euo pipefail`, `[[ ]]`, arrays |
| qemu-system-x86_64 | 10.0.13 | 9.0 | Serial chardev and `isa-debug-exit` |
| OS | Debian 13 | any Linux | Reference environment only |

No cross-compiler, libc, GRUB, Limine or Python is required for Stage 0.

For a container build:

```sh
docker build -t ozero-stage0 toolchain/
docker run --rm -v "$PWD:/work" -w /work ozero-stage0 make ci
```

## Build

```sh
make build
```

Steps:

1. `as --32 -g` assembles the boot fixture with debug symbols.
2. `ld -m elf_i386 --build-id=none -T harness/fixture/boot.ld` links at `0x7C00` and emits a map.
3. `objcopy -O binary` produces the raw image.
4. The build asserts the image is exactly 512 bytes and ends with `55 AA`.
5. `build/manifest.json` records commit, tool versions, sizes, hashes and `SOURCE_DATE_EPOCH`.

Artifacts:

| Path | Required | Notes |
| --- | --- | --- |
| `build/ozero-stage0.elf` | yes | Debug symbols retained. Use `objdump -d`, `addr2line`. |
| `build/ozero-stage0.img` | yes | Raw 512-byte MBR image. |
| `build/ozero-stage0.map` | yes | Linker map for address questions. |
| `build/manifest.json` | yes | Machine-readable build record. |

## Determinism

`scripts/build.sh` sets `LC_ALL=C`, `TZ=UTC`, `SOURCE_DATE_EPOCH` from the lock file, `umask 022`
and `--build-id=none`. It uses repo-relative paths so the build directory does not leak into output.

`make verify-repro` exports the current commit twice with `git archive`, builds each in a separate
directory and compares SHA-256 of the ELF and the image. Different bytes are a failure, not a
warning.

## Clean-checkout validation

`make verify-clean` clones `HEAD` into a temporary directory, then runs `make ci` there. This
catches untracked files, local state and undocumented steps. The acceptance criterion is not "it
builds on my machine", it is "it builds on an empty machine from git alone".

## QEMU boot smoke test

```sh
make test
```

The harness:

1. Validates the image exists, is a multiple of 512 bytes and carries the `55 AA` signature.
2. Starts QEMU with `-machine pc -m 128M -nodefaults -display none -nic none`.
3. Captures COM1 to `build/qemu/serial.log` and guest errors to `build/qemu/qemu-debug.log`.
4. Polls the serial log for every expected marker in `tests/qemu/expected-markers.txt`, in order.
5. Stops as soon as the completion marker `OZERO-STAGE0-OK` appears.
6. On timeout or missing markers, exits non-zero, records the missing markers, and keeps the serial
   log and QEMU debug log as failure artifacts.
7. Writes `build/qemu/result.json` with status, duration, image hash, QEMU version and the exact
   command line.

Timeouts: default 20 s, configurable with `TIMEOUT`. A boot that needs longer than 20 s in Stage 0
is a regression, not a tuning problem.

Expected markers:

```
OZERO-STAGE0-BOOT
OZERO-STAGE0-HARNESS=fixture
OZERO-STAGE0-MODE=real16
OZERO-STAGE0-SERIAL=ok
OZERO-STAGE0-OK
```

Markers are a contract (IF-002). A1 and A2 may replace the values, but the harness must keep
failing on a missing marker, and the replacement must be recorded in the interface registry.

## CI

`.github/workflows/stage0.yml` runs on every push and pull request:

1. `make check-toolchain` (minimum versions; CI records exact versions).
2. `make build`.
3. `make test`.
4. `make verify-repro`.
5. Upload `build/qemu/` and `build/manifest.json` as artifacts.

CI is a gate, not evidence of record. Recorded evidence is a run on the reference toolchain with the
commit hash, copied into `evidence/`.

## Failure policy

- A red build blocks merge. There is no "merge and fix later" on `main`.
- A flaky test is quarantined only with an owner, an expiry date and a linked issue.
- A test may be deleted only with a replacement or an ADR explaining why the property no longer
  matters.
- Any regression in a previously passing test is treated as a blocker, even if the new change is
  unrelated.

## Manual reproduction

```sh
make clean
make ci
qemu-system-x86_64 -machine pc -m 128M -nodefaults -display none \
  -serial stdio -monitor none -nic none -no-reboot \
  -drive file=build/ozero-stage0.img,format=raw,if=ide
```

Press `Ctrl-A X` to leave QEMU. The fixture halts after its markers, so QEMU will not exit on its
own.
