# Test targets

| Field | Value |
| --- | --- |
| Status | **Implemented** (registry), targets **not started** unless marked |
| Owner | A4 |
| Approver | Project Director for target definitions that change scope |

## Why separate names

A test name is a claim. `make test` must never mean "the kernel boots". Each target below names one
property, one owner and one dependency. The machine-readable source is
[`tests/targets.tsv`](../../tests/targets.tsv); `make targets` prints the short form.

| Target | Status | Owner | Command | Proves |
| --- | --- | --- | --- | --- |
| `mbr-harness-smoke` | Implemented | A4 | `make test-mbr-harness` | A 512-byte BIOS MBR fixture boots in QEMU, emits serial markers in order, and the harness fails on missing markers, corrupt images and timeouts. |
| `future-limine-kernel-boot` | Not implemented | A1 | `make test-limine` (future) | Nothing yet. Will prove Limine loads the Ozero kernel and reaches the documented handoff state. |
| `header-contract` | Not started | A1 | `make test-headers` (future) | Nothing yet. Will prove the public headers compile for every consumer and match the registry. |
| `pe-static-analysis` | Not started | A2 | `make test-pe-static` (future) | Nothing yet. Will inspect PE fixtures statically. It must not execute them in normal CI. |
| `hardware-matrix-check` | Not started | A4 / A3 | `make test-hardware` (future) | Nothing yet. Will record boot and device results on named physical machines. |

## Naming rules

- The target name states the property, not the tool: `mbr-harness-smoke`, not `qemu`.
- A target that has not run is **Not started**, even if the command exists.
- A target that only compiles code proves compilation, not behaviour. Say so.
- `mbr-harness-smoke` proves the harness. It does not prove UEFI, Limine, kernel entry, Ozero boot
  or hardware compatibility.
- The MBR fixture is never described as kernel boot in any report, dashboard or PR.

## Current not-tested list

The following have not been tested at all:

- UEFI boot
- Limine loading an Ozero kernel
- Ozero kernel entry, memory setup or any kernel subsystem
- Real-hardware boot
- PE parsing, loading or Windows compatibility
- Public header ABI
- Any A1, A2 or A3 code, because no such code exists in this repository

## Adding a target

1. Add a row to `tests/targets.tsv` with owner, dependency, command and the exact property proved.
2. Add the row to the table above.
3. Add the Makefile target only when the command exists; otherwise keep the status **Not started**.
4. A target that requires hardware, privileged access or Windows binaries must be opt-in and must
   not run in normal CI.
