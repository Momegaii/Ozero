# Stage 0 review response

- Status: draft ready (review answered; no Windows application executed)
- Evidence: repository `Momegaii/Ozero`, branch `agent/a2-stage0`, package root `a2/`, code revision `8bac650`; the regenerated `a2/docs/evidence/*.txt` are in the commit immediately after it
- Decisions needed: Q1–Q4 (Windows baseline, 32-bit timing, syscall strategy, own `ntdll`) and the acceptance of M0 as a milestone distinct from running
- Risks: static evidence is still L0; the API set schema is a test fixture; loader semantics remain unproven until M0/M1; see [05-risk-report.md](05-risk-report.md)
- Next step: A1 reviews [03-a1-interface-requirements.md](03-a1-interface-requirements.md), then A2 implements the M0 trace and compares it with the offsets in [10-technical-findings.md](10-technical-findings.md)

## Basis of this response

The review was a desk review of a summary, not an inspection of the repository.
This response is written against the actual tree, so every statement can be
checked with the commands shown.

| Item | Value |
| --- | --- |
| Repository | <https://github.com/Momegaii/Ozero> |
| Branch | `agent/a2-stage0` |
| Package root | `a2/` |
| Code revision | `8bac650` |
| Parent | `147cc86` (`main`) |

The import commit `8bac650` adds 38 files (+8 273) under `a2/`:

```
a2/README.md  a2/AGENTS.md  a2/Makefile  a2/.gitignore
a2/docs/00..11 and a2/docs/evidence/README.md
a2/data/{pe32plus-structures,test-corpus,apiset-schema}.json
a2/tools/*.py  a2/tools/evidence_header.sh
a2/tests/test_tools.py
a2/fixtures/src/*.c(pp)  a2/fixtures/build-manifest.json
```

The director's briefing PDF and third-party binaries are not redistributed. This
document and the regenerated evidence files are in the commit that follows
`8bac650` on the same branch.

## Point-by-point response

| Review point | Action taken | Where to verify |
| --- | --- | --- |
| Static checks are L0, not runtime compatibility | Every corpus entry now carries an explicit `evidence_level`; the linter forces `L0` for static-only entries and rejects a higher level without execution evidence. Docs 01, 04, 10 state the limit in prose. | `data/test-corpus.json`, `tools/corpus_lint.py`, [04](04-test-corpus.md) |
| Microsoft docs describe structures; loader behaviour needs Windows semantics and real binaries | Accepted and recorded: section "Static evidence and its limits" plus a proved/not-proved table. No loader claim is made. | [01](01-pe-loader-requirements.md), [10](10-technical-findings.md) F7 |
| Findings need hashes, offsets, parser output and spec/header references | Added `docs/10-technical-findings.md` and `tools/finding_report.py`, which prints the file SHA-256, file offsets, raw bytes and the reference URL for each finding. | `docs/evidence/technical-findings.txt` |
| Keep corpus entries not-run until environment, command, date, exit code and artifacts exist | Linter rejects `pass`/`fail`/`blocked` without `environment`, `command`, `date` and `artifacts`; all 17 entries remain `not-run`. | `tools/corpus_lint.py`, test `test_rejects_pass_without_evidence` |
| Preserve fixture source, compiler/linker version, exact command, hashes and expected behaviour | `tools/build_fixtures.py` writes `fixtures/build-manifest.json` with source hash, command line, compiler version (`0.17.1`), target, output hash, size and expected stdout/exit code. `--check` detects drift; the current rebuild is bit-identical to the committed manifest. | `fixtures/build-manifest.json`, `docs/evidence/reproducibility.txt` |
| Third-party provenance, version, licence and SHA-256; run only in a disposable sandbox | Non-fixture entries now require a `provenance` block (retrieval date, method, `redistribute: false`, `verify: sha256`) and `sandbox_required: true`; the linter rejects an entry that would be executed without it. | `data/test-corpus.json`, `tools/corpus_lint.py` |
| Make API-set, TLS, relocations, x64 unwind/chained unwind and load config explicit requirements with negative tests | Each is a named check, and each has a mutation that must make that check fail: `api-ms-win-*` (resolver grammar + 6/8 contracts per fixture), `tls_callbacks_terminated`, `relocation_blocks_consistent`, `exception_table_sorted`, `chained_unwind_flags_clear`, `load_config_size_sane`/`_matches_directory`. | `docs/evidence/negative-tests.txt`, [10](10-technical-findings.md) F6 |
| Make 32-bit x86 a deliberate scope decision; do not infer support from one sample | Added an explicit non-goal in doc 00, an anti-goal in doc 07 and a "32-bit scope" section in doc 04: the single `7zr.exe` entry is analysis-only. Q2 owns the timing decision. | [00](00-compatibility-scope.md), [04](04-test-corpus.md), [07](07-stage7-milestone.md) |
| Prioritise M0 map/relocate/import dry run, then M1 console fixture with exact stdout and exit code | M0/M1 were already the recommendation; M0 now explicitly forbids entry-point execution, TLS callback invocation and stdout, and its pass criteria are the offsets in doc 10. | [07](07-stage7-milestone.md) |

## Definition of done, checked against the tree

| Criterion | Status | Evidence |
| --- | --- | --- |
| All samples have provenance/hash and an explicit evidence level | Met for the 8 analysed images; the other 9 entries are `none` with `hash_pending` where applicable | `data/test-corpus.json`, 558 lint checks |
| No pass without environment, command, date and artifacts | Enforced by the linter and covered by a negative test | `tools/corpus_lint.py`, `tests/test_tools.py` |
| M0 does not execute the entry point | Stated as a pass condition: no entry point, no CRT, no TLS callbacks, no stdout | [07](07-stage7-milestone.md) |
| M1 has deterministic stdout and exit code | Specified: exact string `a2 console fixture: compute=22 module=<non-null>` and exit code 0 | [07](07-stage7-milestone.md), `fixtures/build-manifest.json` |
| API set, TLS and unwind have positive and negative tests | Positive: resolver + real fixtures; negative: unmapped TLS callbacks, broken chained flags, unsorted `.pdata` | `tools/apiset.py`, `tools/negative_report.py`, `tests/test_tools.py` |

## Commands and raw output

All commands were run in `a2/` at `8bac650`. The full outputs are
in `docs/evidence/`.

```
$ make fixtures
console_hello      73216 bytes  1e604cb5e651c27b…
fileproc           74240 bytes  2d6a6fbe6327ffc7…
gui_messagebox     73216 bytes  61e763139e670ff8…
tls_dll            70656 bytes  13bf410e9522732b…
cpp_throw         225280 bytes  70403ee7002e9402…
compiler 0.17.1; manifest written to fixtures/build-manifest.json

$ python3 tools/build_fixtures.py --check
manifest matches: 5 fixtures, compiler 0.17.1

$ python3 tools/pe_structs_check.py build/*.exe build/*.dll .cache/7zr.exe \
      .cache/Newtonsoft.Json.dll .cache/_speedups.cp311-win_amd64.pyd
263 checks, 0 failed

$ python3 tools/negative_report.py
PASS sections_swap            -> sections_ascending                 @ 0x18c 00100000 -> 00e00000
PASS reloc_block_size         -> relocation_blocks_consistent       @ 0x11c04 40000000 -> ffffff00
PASS import_terminator        -> import_descriptors_terminated      @ 0x10ac4 00000000 -> 01000000
PASS pdata_remainder          -> exception_table_entry_size         @ 0x11c 74010000 -> 75010000
PASS pdata_swap               -> exception_table_sorted             @ 0x11800 ...
PASS tls_callbacks_unmapped   -> tls_callbacks_terminated          @ 0xd0d8 ...
PASS chain_flags              -> chained_unwind_flags_clear         @ 0x1fd8 21 -> 29
PASS loadconfig_size          -> load_config_size_sane              @ 0x1b90 40010000 -> ffff0000

8 negative cases produced the expected failure, 0 problems

$ python3 tools/apiset.py --check-image build/console_hello.exe
api-ms-win-crt-heap-l1-1-0.dll               -> ucrtbase.dll [versioned]
api-ms-win-crt-locale-l1-1-0.dll             -> ucrtbase.dll [versioned]
api-ms-win-crt-runtime-l1-1-0.dll            -> ucrtbase.dll [versioned]
api-ms-win-crt-stdio-l1-1-0.dll              -> ucrtbase.dll [versioned]
KERNEL32.dll                                 -> KERNEL32.dll [literal]
api-ms-win-crt-environment-l1-1-0.dll        -> ucrtbase.dll [versioned]
api-ms-win-crt-math-l1-1-0.dll               -> ucrtbase.dll [versioned]

$ python3 tools/corpus_lint.py
558 checks, 0 failed

$ python3 -m unittest discover -s tests -v
Ran 35 tests in 0.455s
OK
```

The document checker and the evidence files are produced by `make evidence`; the
counts in `docs/evidence/docs-check.txt` are the authoritative ones for the final
commit.

## Decisions needed

| Question | Why it blocks | Recommendation |
| --- | --- | --- |
| Q1 Windows baseline | API set schema, DLL set and error codes must be pinned | Windows 10 22H2 as the baseline |
| Q2 32-bit timing | WOW64 affects the loader ABI and pointer size | x64 first, x86 at M2; design the flag now |
| Q3 syscall strategy | Determines whether the layer owns `ntdll` | Own `ntdll` equivalent plus a private boundary |
| Q4 own vs Windows `ntdll` | The loader itself lives in `ntdll` | Own `ntdll`; never patch Microsoft's |
| Q5 .NET | Managed entries are rewritten by `mscoree` | Host the Microsoft runtime |
| Q6 graphics | Translation layer vs driver model | Translation layer, D3D11 first |
| Q7 signature policy | Unsigned fixtures need an explicit, logged exception | Verify where Windows verifies; opt-in for development images |
| Q8 ReactOS/Wine use | Licence boundary | Documentation only until legal review |
| Q15 claim threshold | Prevents cherry-picked numbers | All entries in a category must pass before claiming it |

The full list with options is [09-open-questions.md](09-open-questions.md).

## Unresolved risks

- **Undocumented behaviour (R1).** The static work cannot close it; only a growing
  corpus with recorded deviations can.
- **Kernel requirements (R3–R5).** Drivers, anti-cheat and DRM remain out of
  scope. No user-mode work changes that.
- **API set schema drift (R8).** The repository ships a test schema, not
  Microsoft's; a real schema must be versioned per Windows build.
- **Graphics scope (R6).** Unchanged; D3D11 is the first honest target.
- **.NET hosting (R7).** Unchanged; the CLR is a runtime project.
- **Loader semantics (new).** Search order, forwarders, loader lock, APC delivery
  and exception dispatch are untested until M0/M1. This is the largest gap in the
  evidence, and it is a gap in kind, not only in degree.
- **Toolchain reproducibility.** Fixtures are bit-identical with zig 0.17.1 on
  this host; a different compiler version is expected to change hashes, which is
  why the version is recorded and `--check` reports drift rather than hiding it.

## Explicitly not tested

Nothing in the following list has been executed or verified:

- any Windows application, including every corpus entry (all `not-run`);
- loading, mapping or running a PE image (no loader exists);
- import resolution against real Windows DLLs (only a test schema);
- DLL search order, KnownDLLs, SxS activation contexts, API set schema of a real
  Windows build;
- TLS callback execution, `DllMain`, loader lock, APC delivery;
- exception dispatch and unwinding (structures only);
- any Win32, NT or CRT API behaviour, error code or return value;
- 32-bit execution, WOW64 thunking, 16-bit applications;
- GUI, USER/GDI, common controls, DPI;
- .NET/CLR hosting or managed code execution;
- Direct3D, DXGI, audio, media, composition;
- kernel drivers, UMDF, anti-cheat, DRM;
- code signing, Authenticode validation, CFG enforcement, HVCI;
- performance, memory use, startup time, frame time;
- network access from a Windows process, Winsock, TLS providers;
- services, installers, UAC and the security model;
- ReactOS or Wine code (none was built, run or copied).

## Sources

- Microsoft, PE format specification: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, Unwind data structures for x64: <https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64>
- Microsoft, Windows API sets: <https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets>
- Microsoft, Dynamic-link library search order: <https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order>
- Microsoft, Managed execution process: <https://learn.microsoft.com/en-us/dotnet/standard/managed-execution-process>
- Microsoft, WDDM overview: <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide>
- BattlEye FAQ: <https://www.battleye.com/faq>
- ReactOS architecture: <https://reactos.org/architecture/>
