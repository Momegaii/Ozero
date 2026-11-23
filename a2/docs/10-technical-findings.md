# Technical findings with hashes, offsets and raw bytes

- Status: draft ready (static evidence for the review; nothing executed)
- Evidence: [docs/evidence/technical-findings.txt](evidence/technical-findings.txt) (raw output of `tools/finding_report.py`), [docs/evidence/negative-tests.txt](evidence/negative-tests.txt) (raw output of `tools/negative_report.py`), [fixtures/build-manifest.json](../fixtures/build-manifest.json)
- Decisions needed: none for the findings themselves; the schema used for API set resolution is a test schema and must be replaced by a real one before M1 (Q4, Q8)
- Risks: static evidence is still not loader evidence; API set schema drift; unwind and load-config behaviour depends on Windows semantics, not only on bytes
- Next step: use these offsets as assertions in the M0 loader trace and fail the dry run when any of them disagrees

## What this document answers

The review asked for sample hashes, offsets, parser output and spec/header
references for the three findings (API sets, chained unwind, load config). It
also asked for explicit requirements and negative tests for API sets, TLS
callbacks, relocations, x64 unwind and load config. This document supplies the
first; the negative tests are in section F6 and in
`docs/evidence/negative-tests.txt`.

**Scope of the evidence.** Every value below is read from the file whose SHA-256
is printed next to it. That proves the bytes are as described and that the parser
reads the documented layout. It does not prove that a loader would behave like
Windows. That distinction is restated in section F7.

## F1 — API set contracts are imported by ordinary fixtures

Reference: <https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets>

Fixture `build/console_hello.exe`, SHA-256
`1e604cb5e651c27b4cc154225223217e98b3131e24602ed291bee2c7113e2f7c`:

| Import | Name RVA | File offset | Raw name bytes (first 24) |
| --- | --- | --- | --- |
| `api-ms-win-crt-heap-l1-1-0.dll` | 0x12020 | 0x11020 | `61 70 69 2d 6d 73 2d 77 69 6e 2d 63 72 74 2d 68 65 61 70 2d 6c 31 2d 31` |
| `api-ms-win-crt-locale-l1-1-0.dll` | 0x1203f | 0x1103f | `61 70 69 2d 6d 73 2d 77 69 6e 2d 63 72 74 2d 6c 6f 63 61 6c 65 2d 6c 31` |
| `api-ms-win-crt-runtime-l1-1-0.dll` | 0x12060 | 0x11060 | `61 70 69 2d 6d 73 2d 77 69 6e 2d 63 72 74 2d 72 75 6e 74 69 6d 65 2d 6c 31` |
| `api-ms-win-crt-stdio-l1-1-0.dll` | 0x12082 | 0x11082 | `61 70 69 2d 6d 73 2d 77 69 6e 2d 63 72 74 2d 73 74 64 69 6f 2d 6c 31 2d 31` |
| `api-ms-win-crt-environment-l1-1-0.dll` | 0x120af | 0x110af | `61 70 69 2d 6d 73 2d 77 69 6e 2d 63 72 74 2d 65 6e 76 69 72 6f 6e 6d 65 6e` |
| `api-ms-win-crt-math-l1-1-0.dll` | 0x120d5 | 0x110d5 | `61 70 69 2d 6d 73 2d 77 69 6e 2d 63 72 74 2d 6d 61 74 68 2d 6c 31 2d 31 2d` |

That is **6 contracts / 27 symbols** in the smallest fixture. The C++ fixture
adds `string` and `private`: **8 contracts / 34 symbols**
(`build/cpp_throw.exe`, SHA-256
`70403ee7002e94023f609e8d902f844f35f6bb7bcc47e1c8c9beca91eb653b9f`).
Across the five fixtures: **8 distinct contracts, 127 symbols**. Across all eight
analysed images: 16 DLL names, 360 imported symbols.

None of these names is a file. Resolution requires a schema. The repository now
contains `data/apiset-schema.json` and `tools/apiset.py`, which implement and test
the name rules:

- case-insensitive;
- versioned form (`...-l1-1-0`) and alias form (`...-l1`);
- optional group suffix after `~`;
- unknown contract is an error, not a silent fallback;
- a host must be a plain DLL name, never a path.

```
$ python3 tools/apiset.py --check-image build/console_hello.exe
api-ms-win-crt-heap-l1-1-0.dll               -> ucrtbase.dll [versioned]
...
KERNEL32.dll                                 -> KERNEL32.dll [literal]
```

**Important caveat:** this schema is a test fixture with illustrative host names.
It is not Microsoft's schema and must not be treated as a compatibility claim.
What it proves is that the loader needs a schema and that the name rules can be
implemented and tested independently of Windows.

## F2 — TLS callbacks are real, ordered and terminated

Reference: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-tls-directory>

`build/console_hello.exe` (SHA-256 above):

| Item | Value |
| --- | --- |
| TLS directory RVA | 0xE0C0 |
| TLS directory file offset | 0xD0C0 |
| Directory size | 40 (`IMAGE_TLS_DIRECTORY64`) |
| Raw data template | VA 0x140016000..0x140016008, 8 bytes |
| AddressOfCallBacks | VA 0x1400119F0 |
| Callback array file offset | 0x109F0 |
| Array bytes | `90 14 00 40 01 00 00 00` / `10 15 00 40 01 00 00 00` / `00 00 00 00 00 00 00 00` |
| Decoded callbacks | 0x140001490, 0x140001510, then null |
| Terminated | yes |

Requirement for the loader: the template must be copied per thread, the callback
array must be walked to the null terminator, and the callbacks must run **before**
the entry point, in array order, with `DLL_PROCESS_ATTACH`. The pointer fields are
VAs and must be relocated like any other address.

## F3 — Relocations are numerous and block-structured

Reference: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-relocation-section>

`.cache/7zr.exe`, SHA-256
`256feca8e274e5da655e2a284fabafd9f554365eb164862089dacd4e8276d282`:

| Item | Value |
| --- | --- |
| Format | PE32 i386 |
| Blocks | 135 |
| Entries | 9 546 |
| Types | HIGHLOW 9 484, ABSOLUTE 62 |
| Truncated | no |
| Block[0] | file 0x8D800, PageRVA 0x1000, BlockSize 120 (56 entries) |
| Block[1] | file 0x8D878, PageRVA 0x2000, BlockSize 48 (20 entries) |
| Block[2] | file 0x8D8A8, PageRVA 0x3000, BlockSize 64 (28 entries) |

Block[0] raw bytes: `00 10 00 00 78 00 00 00 ad 30 70 31 b8 31 19 32 ...`, i.e.
PageRVA 0x1000, BlockSize 0x78, then entries such as 0x30AD (HIGHLOW at offset
0x0AD). This is the concrete argument against "32-bit is just a flag": the same
format has a different relocation type and a different image base size.

## F4 — Chained unwind info occurs in real release binaries

Reference: <https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64>

`.cache/_speedups.cp311-win_amd64.pyd`, SHA-256
`79d6891d23e7bb5acfae0ab87b2c8d59431450724999e9cfc3deb8877e1f4cb9`:

| Item | Value |
| --- | --- |
| `.pdata` RVA / file offset | 0x5000 / 0x2800 |
| Entries | 40, remainder 0, sorted by BeginAddress |
| `UNWIND_INFO` versions | 1 × 40 |
| Flags | none 28, CHAININFO 8, UHANDLER 2, EHANDLER 2 |
| Chained entries | 8 |
| Chained-flag violations | 0 |

First chained entry, index 1:

```
file 0x280c: 3b 10 00 00  68 10 00 00  d8 35 00 00
             Begin=0x103b   End=0x1068    UnwindInfo=0x35d8 -> file 0x1fd8
file 0x1fd8: 21 24 0c 00
             first_byte=0x21 -> version=1, flags=4 (CHAININFO)
```

Requirement: chained entries must be followed to the primary `RUNTIME_FUNCTION`,
and a `CHAININFO` entry must not also claim `EHANDLER` or `UHANDLER` (flags 1 and
2 must be clear). The mutation in F6 sets `EHANDLER` on this entry and the checker
rejects it.

## F5 — Load config: declared size and GuardFlags

References: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-load-configuration-structure>
and `winnt.h`.

`.cache/_speedups.cp311-win_amd64.pyd` (SHA-256 above):

| Item | Value |
| --- | --- |
| Load config RVA / file offset | 0x3190 / 0x1B90 |
| Data directory size | 320 |
| Declared `Size` | 320 |
| Header bytes | `40 01 00 00` (320), then zeros |
| Security cookie | 0x180004000 |
| GuardFlags | 0x100 (`IMAGE_GUARD_CF_INSTRUMENTED`) |
| GuardFlags file offset | 0x1C20 (offset 144 in the x64 structure) |
| GuardFlags bytes | `00 01 00 00` |
| Guard CF table / count | 0 / 0 |

Requirement: read the structure according to its declared `Size`, never past it;
do not assume the data directory size and the declared size agree (here they do,
but the check tolerates up to 16 bytes of padding); and treat CFG as a policy
decision rather than a parsing detail.

**Specification discrepancy.** The load-config table in the PE specification does
not match `winnt.h`: it starts at `Characteristics` and omits `Size`, which shifts
every field. This tool follows `winnt.h`, and the value at offset 144 is the
GuardFlags that the specification's table would place elsewhere. This is recorded
as gap G2 in [01-pe-loader-requirements.md](01-pe-loader-requirements.md).

## F6 — Negative tests for each invariant

Each row patches one documented invariant in a copy of a real image and requires a
specific check to fail. Raw output is in
[docs/evidence/negative-tests.txt](evidence/negative-tests.txt).

| Mutation | File offset | Original → patched | Check that must fail | Observed detail |
| --- | --- | --- | --- | --- |
| `sections_swap` | 0x18C | `00100000` → `00e00000` | `sections_ascending` | 7 sections, order broken |
| `reloc_block_size` | 0x11C04 | `40000000` → `ffffff00` | `relocation_blocks_consistent` | blocks=0, truncated |
| `import_terminator` | 0x10AC4 | `00000000` → `01000000` | `import_descriptors_terminated` | 107 descriptors, no terminator |
| `pdata_remainder` | 0x11C | `74010000` → `75010000` | `exception_table_entry_size` | entries=31, remainder=1 |
| `pdata_swap` | 0x11800 | first two entries swapped | `exception_table_sorted` | BeginAddress decreases |
| `tls_callbacks_unmapped` | 0xD0D8 | `f019014001000000` → `0000004f01000000` | `tls_callbacks_terminated` | 0 callbacks, array unmapped |
| `chain_flags` | 0x1FD8 | `21` → `29` | `chained_unwind_flags_clear` | chained=8, violations=1 |
| `loadconfig_size` | 0x1B90 | `40010000` → `ffff0000` | `load_config_size_sane` | declared=65535, directory=320 |

The mutations are produced by `tools/pe_mutate.py` and verified by
`tools/negative_report.py`; the same eight cases also run in
`tests/test_tools.py`.

## F7 — What this evidence proves and what it does not

| Proved by static analysis | Not proved |
| --- | --- |
| the documented structure layout matches real images | that the Windows loader would accept the image |
| imports, relocations, TLS, `.pdata` and load config can be located and decoded | that imports would resolve to the right implementations |
| malformed values in those structures are detected | that a loader recovers from malformed input the way Windows does |
| API set names follow a grammar and need a schema | that the real schema maps them the way Windows does |
| unwind flags and chains are internally consistent | that exception dispatch and unwinding behave correctly |

The review's point stands: these are L0 facts. The loader requirements that
depend on Windows semantics — search order, forwarders, loader lock, exception
dispatch, APC delivery — are tested only when M0/M1 exist.

## Sources

- Microsoft, PE format specification: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, The TLS directory: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-tls-directory>
- Microsoft, The relocation section: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-relocation-section>
- Microsoft, The load configuration structure: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-load-configuration-structure>
- Microsoft, Unwind data structures for x64: <https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64>
- Microsoft, Windows API sets: <https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets>
