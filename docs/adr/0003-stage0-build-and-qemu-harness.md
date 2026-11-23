# ADR-0003: Stage 0 build and QEMU harness

| Field | Value |
| --- | --- |
| Status | Proposed |
| Date | 2026-11-23 |
| Owner | A4 |
| Approver | Project Director |
| Supersedes | — |
| Superseded by | — |
| Related | IF-002, IF-003, IF-004, IF-005 |

## Context

A4 must provide a reproducible Stage 0 build and a boot smoke test before A1 delivers a kernel. The
test has to fail clearly, keep artifacts, and not depend on an unapproved boot protocol. The
briefing lists Limine as a candidate only, not an approved decision, so the harness must not bake
Limine in.

## Decision

We will build and boot a 512-byte BIOS MBR fixture with GNU binutils and run it under QEMU with a
serial-marker contract:

- Toolchain pinned in `toolchain/toolchain.lock`; `make check-toolchain` enforces it.
- `make build` produces ELF with debug symbols, raw image, linker map and manifest.
- Deterministic flags plus `SOURCE_DATE_EPOCH`; `make verify-repro` proves bit-identical rebuilds.
- `make test` runs QEMU with no network, no display and no default devices, captures COM1, and
  requires every marker in `tests/qemu/expected-markers.txt` in order.
- Timeout is 20 s by default; failure keeps the serial log and QEMU debug log.
- `make verify-clean` proves a fresh clone builds and passes.

The fixture is a harness self-test, not the Ozero kernel and not a boot-protocol decision. When A1
delivers a real image, `make test` points at it through `IMAGE=`; the marker contract stays.

## Options considered

| Option | Pros | Cons | Verdict |
| --- | --- | --- | --- |
| Limine-based image now | Closer to a likely final boot path | Bakes in an unapproved decision; needs external download and ISO tooling | Rejected for Stage 0 |
| GRUB/multiboot2 fixture | Standard | Needs `grub-mkrescue` and ISO tooling; more moving parts | Rejected |
| 512-byte MBR fixture | No external deps, bit-reproducible, real BIOS boot path | Not the final kernel; very small | **Chosen** |
| No boot test until A1 lands | Less work now | Harness untested when it matters | Rejected |

## Consequences

- Positive: the harness is proven before domain code exists; failure modes are already exercised.
- Negative: the fixture is throwaway and must not be mistaken for the kernel.
- Follow-up: A1 decides the boot protocol; the harness switches to `IMAGE=` with the same markers.

## Risks

| Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- |
| Fixture mistaken for kernel | Medium | Wasted effort, wrong assumptions | Named `harness/fixture`, documented in README and ADR |
| QEMU version drift changes behaviour | Low | Flaky boot | Minimum version check; reference version recorded in evidence |
| Markers become a de facto ABI without review | Medium | Interface drift | Markers registered as IF-002 with an owner |
| Reproducibility breaks silently | Medium | Untrustworthy evidence | `make verify-repro` in CI, hash comparison is a hard failure |

## Evidence

Recorded in `evidence/stage0/`:

- `make ci` output
- `make verify-repro` output
- `make verify-clean` output
- serial log and `result.json`

## Approval

- Approved by:
- Date:
- Link:
