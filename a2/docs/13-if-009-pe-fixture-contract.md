# IF-009: PE fixture contract (v1.0)

- Status: submitted for review (A3 accepted an earlier revision with comments; A1 pending; v1.0 confirmation pending; XB-003 Open)
- Evidence: `fixtures/build-manifest.json` and `docs/evidence/*.txt` at the code revision named in each evidence header
- Decisions needed: IF-009 v1.0 sign-off by A1 and A3
- Risks: a fixture change after sign-off silently invalidates A1/A3 review; an unlabelled fixture result is read as compatibility; see [05-risk-report.md](05-risk-report.md)
- Next step: A1 answers IF9-A1-1…3, A3 answers IF9-A3-1…2, A2 records both signatures with commit hashes

## Contract identity

| Field | Value |
| --- | --- |
| ID | IF-009 |
| Version | 1.0 |
| Owner | A2 |
| Reviewers | A1 (kernel interfaces), A3 (platform/device boundary) |
| Scope | fixture format, static validation, provenance and change control |
| Not in scope | loader, API set schema, import resolution against Windows DLLs, execution |
| Global dependency | XB-003 (A2 → A1/A3) |
| Compiler pin | zig 0.17.1, `python3 -m ziglang cc`, target `x86_64-windows-gnu` |

Fixtures and the manifest are platform-neutral; no `o0_*` hardware or A3 device type
appears in the loader contract. The fixtures use Windows APIs only, and the manifest
records relative paths and content hashes.

## Why this contract exists

A1 and A3 need a fixed input before they can review A2's loader requirements. Without a
contract, the fixtures, their hashes and the meaning of a passing check can change under
the review. IF-009 freezes those inputs and states what a pass does and does not mean.

## Fixture inventory (v1.0)

All fixtures are built from the committed sources by `tools/build_fixtures.py`; the
manifest records source and output SHA-256, size, command, target, compiler version and
the expected result. Paths in the manifest are relative to `a2/`.

| ID | Source | Kind | Subsystem | Sections | Imports (DLLs/symbols) | API sets | Relocs | TLS callbacks | `.pdata` | Size | Output SHA-256 (prefix) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `console_hello` | `fixtures/src/console_hello.c` | EXE | console (3) | 7 | 7 / 38 | 6 | 32 | 2 | 31 | 73 216 | `1e604cb5e651` |
| `fileproc` | `fixtures/src/fileproc.c` | EXE | console (3) | 7 | 7 / 49 | 6 | 32 | 2 | 31 | 74 240 | `2d6a6fbe6327` |
| `gui_messagebox` | `fixtures/src/gui_messagebox.c` | EXE | GUI (2) | 7 | 8 / 41 | 6 | 32 | 2 | 31 | 73 216 | `61e763139e67` |
| `tls_dll` | `fixtures/src/tls_dll.c` | DLL | GUI (2) | 7 | 4 / 20 | 3 | 26 | 3 | 23 | 70 656 | `13bf410e9522` |
| `cpp_throw` | `fixtures/src/cpp_throw.cpp` | EXE | console (3) | 7 | 9 / 52 | 8 | 1 410 | 2 | 470 | 225 280 | `70403ee7002e` |

The full 64-character hashes are in `fixtures/build-manifest.json`. A change to any
source, flag, target or compiler version changes this table and requires a version bump.

## Static validation matrix

| Check | Tool | Applies to | Evidence level |
| --- | --- | --- | --- |
| DOS/NT headers, optional header size, alignment | `pe_structs_check.py` | all five | L0 |
| Section table ordering, raw/virtual bounds | `pe_structs_check.py` | all five | L0 |
| Data directory sizes and terminators | `pe_structs_check.py` | all five | L0 |
| Import descriptors, names, thunks, terminator | `pe_structs_check.py` | all five | L0 |
| Relocation blocks, entry size, page bounds | `pe_structs_check.py` | all five | L0 |
| TLS directory, template, callback terminator | `pe_structs_check.py` | `console_hello`, `fileproc`, `gui_messagebox`, `tls_dll`, `cpp_throw` | L0 |
| `.pdata` entry size, order, unwind versions | `pe_structs_check.py` | all five | L0 |
| Load-config size and GuardFlags offset | `pe_structs_check.py` | `console_hello` | L0 |
| CLR header absent | `pe_structs_check.py` | all five | L0 |
| API set contract names and symbol counts | `pe_inspect.py`, `apiset.py` | all five | L0 |
| Eight deliberate corruptions must fail | `pe_mutate.py`, `negative_report.py` | `console_hello` | negative |

The negative suite patches one invariant per case: section order, relocation block size,
import terminator, `.pdata` entry size, `.pdata` order, TLS callback terminator, chained
unwind flags and load-config size. A fixture build that does not reproduce these failures
is not IF-009 compliant.

## What a passing check means

A pass means: the parser read the bytes, the structure matched the documented layout, and
the expected static facts (counts, offsets, terminators) were observed. It is evidence
level **L0**.

A pass does **not** mean: the image loads, imports resolve, TLS callbacks run, exceptions
dispatch, the API set schema is real, or any Windows application is compatible. Those are
L1+ results and none exists.

## A1 interface assumptions

IF-009 does not freeze a kernel ABI. It assumes only that a future loader can:

1. map a file-backed image at a preferred or relocated base and protect its sections;
2. keep the mapped image and its `.pdata` readable for the process lifetime;
3. create a suspended process and first thread before transferring control.

Questions for A1:

- **IF9-A1-1** — do the A1 memory primitives (`o0_as_create`, map, protect) support
  mapping a file-backed image at a chosen base, or is a section object required first?
- **IF9-A1-2** — is the mapped image guaranteed to stay resident and readable, including
  `.pdata`, until the process exits?
- **IF9-A1-3** — does A1 accept these five fixtures as M0 inputs without implying an ABI
  freeze or a Windows loader commitment?

These overlap A1-REV-04/05/08/09 in [12-a1-contract-review.md](12-a1-contract-review.md);
A1 may answer them together.

## A3 interface assumptions

PE parsing and these fixtures have no device, DMA, IRQ, block or graphics dependency.
They are regular files read by user-mode tools.

Questions for A3:

- **IF9-A3-1** — confirm that IF-009 requires no platform, DMA or block interface from A3.
- **IF9-A3-2** — confirm that A3 will not cite an IF-009 pass as a driver, AHCI or
  graphics result.

A3 answered both in its review of `807c447` (branch `a3/stage0-review-response`,
commit `2dcc0fc`): no hardware-platform leakage, and A3 does not review loader runtime.
A3 also asked for the platform-neutrality sentence above and for relative manifest paths;
both are in v1.0. A3 confirmation of the v1.0 wording is pending.

## Change control

- Any change to a fixture source, flag, target or compiler version bumps IF-009 to 1.1,
  1.2, and so on.
- The manifest is regenerated; `make fixtures-check` must report no drift.
- Evidence is regenerated at the new code revision.
- A1 and A3 re-sign the new version; an old signature does not carry forward.
- Adding a corpus entry that is not a fixture does not change IF-009, but it must stay
  `not-run` until the execution gate allows a run.

## Review record

| Reviewer | Revision reviewed | Verdict | Disposition |
| --- | --- | --- | --- |
| A3 | `807c447` | Accept with comments: no hardware-platform leakage; normalize absolute manifest paths; add a platform-neutrality sentence | Both comments applied in v1.0 (`0522f11`): paths are relative, sentence added. A3 confirmation of v1.0 pending |
| A1 | — | Pending | A1-REV-04/05/08/09 and IF9-A1-1…3 remain open |

## Sign-off

| Reviewer | Decision (Accept / Change / Defer) | Commit reviewed | Date |
| --- | --- | --- | --- |
| A1 | pending | — | — |
| A3 | Accept with comments (earlier revision); v1.0 confirmation pending | `807c447` | 2026-11-23 |

A signature names the IF-009 version, the A2 commit and the reviewer's commit. A general
approval comment does not close XB-003.

## Sources

- `fixtures/build-manifest.json` and `tools/build_fixtures.py`.
- [04-test-corpus.md](04-test-corpus.md), [10-technical-findings.md](10-technical-findings.md).
- Microsoft, PE format: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, x64 exception handling: <https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64>
