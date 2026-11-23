# IF-009: PE fixture contract (v1.0)

- Status: A1 accepts v1.0 with conditions (`a17d126`, head `74b1471`); A3 accepts the hardware boundary (`307b919`); A2 signature recorded; A4 registry update pending; XB-003 Open until A4 records
- Evidence: `fixtures/build-manifest.json` and `docs/evidence/*.txt` at the code revision named in each evidence header
- Decisions needed: A4 records both v1.0 signatures and updates the IF-009 registry row
- Risks: a fixture change after sign-off silently invalidates A1/A3 review; an unlabelled fixture result is read as compatibility; see [05-risk-report.md](05-risk-report.md)
- Next step: A4 updates the IF-009 row to v1.0 and records both signatures; A2 tracks the v1.1 negative-case gaps

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

## Contract references

| Side | Reference | Commit |
| --- | --- | --- |
| A1 | `include/ozero/` public headers; `o0_as_create`, `o0_as_map`, `o0_as_protect`, `o0_as_unmap`; `o0_thread_create`, user pointer and GS/FS base; `o0_handle_open/dup/wait`; neutral exception record later | reviewed `30ee4f0`, head `74b1471`, review `a17d126` |
| A2 | This contract v1.0; PE structures in [01](01-pe-loader-requirements.md) and [10](10-technical-findings.md); loader stages covered: L0 parse only | signed pair `d02598a`/`3f78477`; current pair in the evidence headers |
| A3 | Block/storage delivery for any future runtime path (IF-008); no PE parsing | review `307b919` |

The A1 commit is the header set A1 reviewed; the current A1 head is `4716e09`. A later
A1 commit does not silently change this contract; a header change that affects the
primitives above requires a new IF-009 review.

## Delivery path

Stage 0 tools read fixture files from the repository or the harness filesystem. No block
device, DMA transfer or kernel PE parser is involved.

A future runtime loader receives image bytes through A3's block interface (IF-008). A1
provides memory and thread primitives, A3 provides storage, and A2 parses PE in user mode.
Neither A1 nor A3 parses PE, and IF-009 does not require A3 to implement anything in
Stage 0.

## Negative coverage against A1's categories

A1's review asks for six negative categories. Current coverage is explicit, including the
gaps:

| A1 category | v1.0 case | State |
| --- | --- | --- |
| Malformed PE | `sections_swap`, `pdata_remainder`, `loadconfig_size` | Covered |
| Truncated headers | — | **Gap**; candidate for v1.1 |
| Bad relocations | `reloc_block_size` | Covered |
| Missing imports | `import_terminator` | Partial; a missing DLL name is a **gap** for v1.1 |
| Invalid TLS | `tls_callbacks_unmapped` | Covered |
| Expected error/evidence | Each case names the check it must trigger | Covered |

A1's review of v1.0 will decide whether the two gaps block acceptance or can be deferred
to v1.1.

## A1 interface assumptions

IF-009 does not freeze a kernel ABI. It assumes only that a future loader can:

1. map a file-backed image at a preferred or relocated base and protect its sections;
2. keep the mapped image and its `.pdata` readable for the process lifetime;
3. create a suspended process and first thread before transferring control.

A1 answered at `a17d126` (head `74b1471`):

| ID | A1 answer | Consequence |
| --- | --- | --- |
| IF9-A1-1 | Change required at the kernel-primitive level: `o0_as_create` + `o0_as_map` can map physical pages at a chosen virtual address and `o0_as_protect` sets protections; there is no file-backed section object at `30ee4f0` | For M0, A2 reads the file into A1-provided memory and maps it. A true section object is A1-REV-04 at Stage 7. Not an IF-009 defect |
| IF9-A1-2 | Accept with condition: A1 does not evict or unmap behind the address-space owner; the mapping persists until unmap, destroy or exit | A2 must keep `.pdata` mapped while unwind data is needed |
| IF9-A1-3 | Accept: the five fixtures are static M0 inputs; no ABI freeze, no Windows loader commitment, no runtime claim | A2 owns PE parsing and loader policy |

These overlap A1-REV-04/05/09 in [12-a1-contract-review.md](12-a1-contract-review.md).

## A3 interface assumptions

PE parsing and these fixtures have no device, DMA, IRQ, block or graphics dependency.
They are regular files read by user-mode tools.

Questions for A3:

- **IF9-A3-1** — confirm that IF-009 requires no platform, DMA or block interface from A3.
- **IF9-A3-2** — confirm that A3 will not cite an IF-009 pass as a driver, AHCI or
  graphics result.

A3 answered both at `307b919`:

- **IF9-A3-1** — No. IF-009 requires no A3 block, DMA, IRQ or GPU interface. A future
  loader may use IF-008 only as the byte-delivery path.
- **IF9-A3-2** — No. An IF-009 pass is L0 static evidence; A3 will not cite it as an
  AHCI, DMA, driver, GPU or runtime result.

A3 accepts IF-009 v1.0 at `d02598a` for the hardware boundary with no blocking comments.
This is not A1's kernel-interface approval and not loader approval.

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
| A1 | `d02598a` (contract blob `9be6c8b0…`), review `a17d126`, head `74b1471` | **Accept with conditions** | Static-only L0; version discipline; review-record corrections (resolved at `6d305b8`); A3 v1.0 confirmation (received `307b919`); A4 registry update; no file-backed section claim |
| A3 | `d02598a`, review `307b919` | **Accept for the hardware boundary**, no blocking comments | IF9-A3-1/2 answered; not loader approval and not A1's kernel approval |

## Sign-off

| Reviewer | Decision (Accept / Change / Defer) | A2 commit reviewed | Reviewer commit | Date |
| --- | --- | --- | --- | --- |
| A1 | Accept with conditions (IF9-A1-1 kernel primitive; review-record corrections; A3/A4 records) | `d02598a` | `a17d126` | 2026-11-23 |
| A3 | Accept for the hardware boundary | `d02598a` | `307b919` | 2026-11-23 |

A signature names the IF-009 version, the A2 commit and the reviewer's commit. A general
approval comment does not close XB-003.

## Sources

- `fixtures/build-manifest.json` and `tools/build_fixtures.py`.
- A1 `docs/if009-review.md` and `docs/a2-contract-review-response.md` at `74b1471` (`a17d126`).
- A3 `a3/docs/interfaces/IF-009-a3-review.md` at `307b919`.
- [04-test-corpus.md](04-test-corpus.md), [10-technical-findings.md](10-technical-findings.md).
- Microsoft, PE format: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, x64 exception handling: <https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64>
