# Test corpus proposal

- Status: draft ready (manifest complete; no execution)
- Evidence: [data/test-corpus.json](../data/test-corpus.json); `python3 tools/corpus_lint.py` reports 559 checks, 0 failed ([evidence/corpus-lint.txt](evidence/corpus-lint.txt)); static analysis results in [evidence/pe-inspect.txt](evidence/pe-inspect.txt)
- Decisions needed: how many entries per category are required before any compatibility statement (Q15); whether proprietary entries are allowed in the corpus at all; which baseline Windows build defines "expected" behaviour (Q1)
- Risks: corpus legality and redistribution; overfitting to five applications; games and anti-cheat cannot be included lawfully or technically; see [05-risk-report.md](05-risk-report.md)
- Next step: pin hashes for the eight pending entries, then run L2 on the console fixture as the Stage 7 exit test

## Strategy

The corpus exists to convert "compatible" into numbers. It is organised by
category and by tier:

| Tier | Definition | Why it exists | Redistribution |
| --- | --- | --- | --- |
| T0 fixture | Built from this repository's source with a documented toolchain | Deterministic, inspectable, no licensing questions | Source committed, binary not committed |
| T1 open-source | Public application with a known licence and a stable download | Real-world code paths and real import graphs | Not committed; downloaded and hash-pinned |
| T2 commercial | Proprietary application | Represents the hardest real-world cases | Analysis only; operator supplies the binary |

Five categories are mandatory because they fail for different reasons:

1. **console** — process startup, CRT, file and console APIs.
2. **gui** — subsystem 2, USER/GDI, message loop, common controls.
3. **file-process** — handles, pipes, process creation, memory, directory APIs.
4. **dotnet** — CLR header, hosting, managed libraries.
5. **directx** — graphics runtime and the device driver boundary.

A sixth category, **native-library**, holds supporting DLLs and extension modules
that exercise loader features (exports, TLS callbacks, CFG) without being an
application. Supporting entries never count toward category coverage.

## Entries

The full manifest with prerequisites, expected behaviour, pass criteria and fail
signals is [data/test-corpus.json](../data/test-corpus.json). Summary:

| ID | Category | Tier | Purpose | Status |
| --- | --- | --- | --- | --- |
| `a2-console-hello` | console | T0 | smallest loader target: imports, relocations, TLS, `.pdata` | not-run (L0 done) |
| `a2-cpp-exceptions` | console | T0 | SEH and C++ unwinding with chained and handler entries | not-run (L0 done) |
| `7zr-standalone` | console | T1 | real-world PE32, 9 546 relocations, 139 imports, resources | not-run (L0 done) |
| `a2-gui-messagebox` | gui | T0 | subsystem 2 and `MessageBoxW` | not-run (L0 done) |
| `notepad-plus-plus` | gui | T1 | common controls, activation context, file dialogs | not-run |
| `a2-fileproc` | file-process | T0 | pipes, process creation, waits, virtual memory, directory query | not-run (L0 done) |
| `7zip-file-ops` | file-process | T1 | archive round-trip, threading, file semantics | not-run |
| `curl-windows` | file-process | T1 | Winsock, DNS, TLS provider, file output | not-run |
| `a2-dotnet-hello` | dotnet | T0 | CLR hosting, framework-dependent and self-contained | not-run (blocked on SDK) |
| `newtonsoft-json` | dotnet | T1 | CLR header, metadata, strong-name, Authenticode | not-run (L0 done) |
| `powershell-7` | dotnet | T1 | large self-contained managed application | not-run |
| `paint-dotnet` | dotnet | T2 | WinForms plus GDI+ plus GPU composition | not-run |
| `imgui-d3d11` | directx | T1 | smallest honest D3D11 target | not-run |
| `d3d12-miniengine` | directx | T1 | D3D12, descriptor heaps, fences | not-run |
| `vkd3d-proton-tests` | directx | T1 | prior-art conformance suite (not an Ozero target) | not-run |
| `a2-tls-dll` | native-library | T0 | exports, TLS template, TLS callbacks, `DllMain` | not-run (L0 done) |
| `markupsafe-pyd` | native-library | T1 | PE32+ DLL with CFG and chained unwind info | not-run (L0 done) |

Coverage: console 3, gui 2, file-process 3, dotnet 4, directx 3, native-library 2.
Every required category has at least two entries and at least one non-supporting
executable entry. The linter enforces both rules.

## What has actually been measured

Static analysis (L0) was performed on eight images. Nothing was executed.

| Image | Key measured facts |
| --- | --- |
| `console_hello.exe` | PE32+ CUI, 7 imports DLLs / 38 symbols, 32 DIR64 relocations, 8-byte TLS template with 2 callbacks, 31 `.pdata` entries |
| `fileproc.exe` | PE32+ CUI, 7 / 49, 32 DIR64, 31 `.pdata` |
| `gui_messagebox.exe` | PE32+ GUI (subsystem 2), 8 / 41, 32 DIR64 |
| `tls_dll.dll` | 2 named exports, 16-byte TLS template with 3 callbacks, 26 relocations |
| `cpp_throw.exe` | 9 / 52, 1 410 relocations, 470 `.pdata` entries |
| `7zr.exe` | PE32 i386, 5 / 139, 9 546 relocations (HIGHLOW), resources, no `.pdata` |
| `Newtonsoft.Json.dll` | PE32 IL-only, CLR header size 72, runtime 2.5, strong-name signed, Authenticode, 1 import (`mscoree`) |
| `_speedups.pyd` | PE32+ DLL, 4 / 20, 40 `.pdata` (8 chained), load config 320 bytes, CFG instrumented |

This is deliberately described as L0. It proves the structures exist and are read
correctly; it proves nothing about behaviour.

## Manifest schema

Top level:

| Field | Meaning |
| --- | --- |
| `corpus_version` | integer, bumped on incompatible changes |
| `policy` | honesty, redistribution and claims rules |
| `required_categories` | categories that must be covered |
| `categories` | definitions |
| `entries` | the corpus |

Per entry:

| Field | Required | Notes |
| --- | --- | --- |
| `id` | yes | lowercase, hyphenated, unique |
| `category` | yes | one of the defined categories |
| `tier` | yes | `fixture`, `open-source`, `commercial` |
| `supporting` | yes | true for library/DLL entries that do not count toward coverage |
| `name`, `version`, `license` | yes | non-empty |
| `source` | yes | `repo` (path must exist), `external-repo` or `download` (https URL), or `planned` |
| `sha256` | for pinned binaries | 64 hex characters |
| `hash_pending` + `hash_pending_reason` | alternative to `sha256` | allowed only while `execution.status == not-run` |
| `prerequisites` | yes | non-empty list |
| `apis_exercised` | yes | non-empty list |
| `expected_behavior` | yes | one observable statement |
| `pass_criteria` | yes | non-empty list of observable conditions |
| `fail_signals` | yes | non-empty list |
| `static_analysis` | yes | `done` requires tool, command, date and observed values |
| `execution` | yes | `not-run`, `blocked`, `pass` or `fail` |
| `claims` | only for absolute claims | must carry measured corpus evidence |
| `evidence_level` | yes | `none`, `L0`…`L4`; `L0` requires a completed static analysis |
| `sandbox_required` | yes | must be true for every non-fixture entry |
| `provenance` | non-fixtures | retrieval date (nullable), method, `redistribute: false`, `verify: sha256` |
| `fixture_id`, `build_manifest` | built fixtures | must match `fixtures/build-manifest.json` |

### Lint rules

`tools/corpus_lint.py` enforces, among others:

- unique ids and unique hashes;
- every required category has ≥ 2 entries and ≥ 1 executable entry;
- `execution.status == pass|fail|blocked` requires `environment`, `command`,
  `date` and `artifacts`;
- `execution.status == not-run` must not carry observed results and must give a
  reason;
- an unpinned binary (`hash_pending`) may not be executed;
- `static_analysis.status == done` requires a tool, command, date and observed
  values;
- absolute phrases such as "full compatibility" require measured evidence;
- `evidence_level: L0` requires `static_analysis.status: done`, and a completed
  static analysis forces `L0` while the entry is `not-run`;
- an unpinned binary may not have an evidence level above `none`;
- non-fixture entries require `sandbox_required: true` and a provenance block;
- fixture sources must match the SHA-256 recorded in the build manifest.

The negative cases are covered by unit tests: a manifest with an unmeasured pass
claim, a duplicate id, missing category coverage, or an executed unpinned binary
must fail the lint.

## Evidence levels

Every entry names the strongest evidence that exists for it. The levels are
defined once in [data/test-corpus.json](../data/test-corpus.json) under
`policy.evidence_levels`:

| Level | Meaning | Current entries |
| --- | --- | --- |
| `none` | no analysis | 9 |
| `L0` | parsed by `tools/pe_inspect.py`, no code run | 8 |
| `L1` | mapped and imports resolved, entry point not reached | 0 |
| `L2` | ran to exit with recorded stdout and exit code | 0 |
| `L3` | interactive behaviour verified against a reference | 0 |
| `L4` | corpus measured with published per-entry results | 0 |

A level is not a claim about adjacent levels. `L0` says the bytes were read
correctly; it says nothing about loading or running.

## Sandbox and provenance

Third-party binaries are never committed and never run on a host with credentials
or mounts. Each non-fixture entry carries a provenance block with the retrieval
date, method, `redistribute: false` and `verify: sha256`. The linter rejects an
entry that lacks one, that would be executed without `sandbox_required: true`, or
that is analysed before its hash is pinned.

## Measurement model

| Metric | Definition | Source of truth |
| --- | --- | --- |
| M1 loader success | images that reach L1 / images attempted | loader trace |
| M2 import resolution | symbols resolved / symbols imported | loader trace |
| M3 API coverage | imported functions with a working implementation / imported functions | trace plus import list |
| M4 pass rate | entries at pass / entries attempted, per category | manifest execution records |
| M5 stability | runs without crash or hang / runs | harness log |
| M6 performance (later) | startup time and frame time vs a Windows reference | harness timing |

A category is only "supported" when M4 is reported for a fixed corpus and the
manifest contains the per-entry evidence. No aggregate number may be quoted
without the underlying records.

### Evidence format per run

```
environment: host build, kernel, A1 build, A2 build, date
command:     exact command line
stdout:      captured output (or a hash plus a stored artifact)
exit_code:   integer
artifacts:   logs, screenshots, API traces, file hashes
```

Screenshots and logs are stored under `docs/evidence/` and referenced from the
manifest. Binary artifacts from third parties are never committed.

## 32-bit scope

The corpus contains one 32-bit binary (`7zr.exe`) because the format needs to be
read correctly, not because 32-bit execution is supported. No 32-bit run is in
the Stage 7 exit criterion, and one sample is not evidence of WOW64 support.
Question Q2 in [09-open-questions.md](09-open-questions.md) owns the decision.

## Selection criteria

An entry is admitted only if:

1. It is lawfully obtainable and its licence allows testing (not redistribution).
2. Its behaviour can be stated as observable pass/fail criteria.
3. It exercises a distinct API family or loader feature.
4. It has a stable version and a pinnable hash.
5. It does not require kernel-mode components (otherwise it is recorded as a
   known limitation, not as a test).

Games with kernel anti-cheat are excluded by criterion 5. They are discussed in
the risk report, not in the corpus.

## Gaps in the current corpus

| Gap | Consequence | Proposed fix |
| --- | --- | --- |
| No audio entry | WASAPI/DirectSound risk is unmeasured | add a small open-source player |
| No installer or service entry | SCM, MSI and UAC paths unmeasured | add an open-source MSI-based installer |
| No 32-bit GUI entry | WOW64 GUI path unmeasured | add a 32-bit open-source GUI tool |
| No browser/WebView2 entry | sandbox and GPU process untested | defer until Class G exists |
| No network server entry | only client-side Winsock covered | add a small open-source server |
| No large game engine | engine-level API breadth unmeasured | defer; anti-cheat excludes most candidates |

These gaps are recorded so that "the corpus passes" cannot quietly mean "the
corpus was easy".

## Evidence

```
$ python3 tools/corpus_lint.py
408 checks, 0 failed
```

The linter is not a substitute for running tests. Its value is that it makes
false claims structurally impossible to add without also adding evidence.

## Sources

- Microsoft, PE format specification: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, Managed execution process: <https://learn.microsoft.com/en-us/dotnet/standard/managed-execution-process>
- Microsoft, WDDM overview: <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide>
- Microsoft, Windows API sets: <https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets>
- BattlEye FAQ (why kernel anti-cheat entries are excluded): <https://www.battleye.com/faq>
