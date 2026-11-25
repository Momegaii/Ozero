# ADR 0006 — Verification matrix: firmware × CPU, plus sanitizer host tests

- Status: accepted, implemented in `.github/workflows/quality.yml` (2026-11-24)
- Scope: CI workflow, `tools/qemu-smoke.sh`, `Makefile`

## Context

Until this ADR the workflow had one job that built the kernel and ran two QEMU boots (BIOS,
`qemu64` and `Haswell-v1`). That left the UEFI boot path unexercised, so a kernel change that
broke OVMF boot would pass CI. It also ran host tests only in their normal build, so an
out-of-bounds or undefined-behaviour defect that happens to pass could go unnoticed.

Two conformance axes matter for the current kernel:

- **Firmware:** Limine's BIOS and UEFI entry paths differ in the memory map shape, the EFI
  system table availability and firmware hand-off, and the boot contract is claimed as a
  hybrid BIOS/UEFI ISO.
- **CPU model:** `qemu64` is the conservative generic x86-64 baseline; `Haswell-v1` carries
  the feature set the Stage 5 backend targets and changes CPUID results.

The Wave 1 investigation also showed that the QEMU **version** changes observable interrupt
delivery behaviour (see ADR 0005), so the matrix result must name the emulator version it ran
on rather than assuming "QEMU" is uniform.

## Alternatives considered

| Alternative | Why it was not chosen |
|---|---|
| Keep one job with two BIOS boots | Cheapest, but UEFI and the sanitizer build stay unverified; this is the debt the wave exists to close. |
| Full matrix of firmware × CPU × QEMU versions | Best coverage, but pinning and installing multiple QEMU versions in CI is slow and fragile; the version is recorded in the smoke-test output instead, and the developer runs can cover a second version (as done for Wave 1: 8.2.2 and 10.1.0). |
| Self-hosted physical hardware runner | The only source of real hardware evidence, but it is not available and physical validation remains a separate, explicitly unclaimed category. |
| Sanitizer flags added to the normal `make test` | Would slow every local run and make the default build depend on sanitizer runtimes. A separate `make test-sanitize` target plus a dedicated CI job keeps both usable. |

## Decision

1. `make test-sanitize` builds and runs the full host suite with
   `-fsanitize=address,undefined -fno-omit-frame-pointer -g`, leak detection enabled and
   `UBSAN_OPTIONS=halt_on_error=1`. `make test` gains `HOST_EXTRA_CFLAGS`/`HOST_RUN_ENV`
   hooks so one recipe set serves both.
2. `tools/qemu-smoke.sh` takes `QEMU_FIRMWARE=bios|uefi`. UEFI uses OVMF 4 MiB code plus a
   per-run copy of the variable store (`QEMU_OVMF_CODE`/`QEMU_OVMF_VARS` override the
   `/usr/share/OVMF/...` defaults) and requires `firmware=UEFI` in the self-describing boot
   header; BIOS requires `firmware=BIOS`. Both require `OZERO: interrupt delivery=`. UEFI
   logs are matched after stripping ANSI CSI/OSC sequences, and scheduler worker lines are
   checked as an unordered set.
3. The workflow has two jobs: `host-sanitizers` (no emulator needed) and a four-cell
   `kernel-and-boot` matrix over `{bios, uefi} × {qemu64, Haswell-v1}`, each uploading its
   serial and QEMU diagnostic logs.
4. The smoke test prints the emulator version and the firmware/CPU it validated in its PASS
   line, so copied evidence cannot be mistaken for another cell.
5. `make check` runs `tools/framebuffer-check.py` when QEMU is installed, so every
   `kernel-and-boot` cell exercises it. The tool boots the same ISO under QMP, takes a raw
   `screendump`, validates the eight bar centres at three heights and both edges, and compares
   an FNV checksum with the kernel's `framebuffer checksum=` marker. This is a separate CI
   assertion from the serial markers. Its serial log matches the existing
   `build/qemu-serial-*.log` artifact glob; the PPM is left in the job workspace.

## Consequences

- A failing cell is attributable by firmware and CPU without reading logs first, and the
  artifacts are named per cell.
- CI installs `ovmf`; a developer without OVMF gets a clear "install the ovmf package" error
  rather than a silent fallback to BIOS.
- Boot time roughly quadruples for the boot matrix. Each cell builds the ISO independently;
  caching or a build-once/artifact-pass design can be added later without changing what is
  asserted.
- The matrix is still emulator evidence. It cannot mark a physical-hardware or GPU claim as
  verified.
- GitHub Actions results for a branch still have to be observed in the repository UI/API; the
  REST API returned 403 from the Wave 1 sandbox, so run data was read from the server-rendered
  Actions page. With that method, run #132 on `kilo/floral-eagle-2hb` (commit `d6626b4`)
  completed SUCCESS with all four boot cells and `host-sanitizers` present.
