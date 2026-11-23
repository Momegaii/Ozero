# Ozero A2 — Windows compatibility: Stage 0 package

Stage 0 research and design for a Windows application compatibility
environment. This repository contains the required deliverables, the machine
readable data behind them, and the tools that check both. The director's briefing
is held internally and is not redistributed in this repository.

**Nothing here runs Windows applications.** No loader exists, no API is
implemented, and no application has been executed. The only measured results are
static analyses of eight PE images. That distinction is enforced by the corpus
manifest and its linter.

## Deliverables

| Required output | Document | Status |
| --- | --- | --- |
| Compatibility scope map | [docs/00-compatibility-scope.md](docs/00-compatibility-scope.md) | draft ready |
| PE loader requirements | [docs/01-pe-loader-requirements.md](docs/01-pe-loader-requirements.md) | draft ready |
| NT/Win32 dependency map | [docs/02-nt-win32-dependency-map.md](docs/02-nt-win32-dependency-map.md) | draft ready |
| A1 interface requirements | [docs/03-a1-interface-requirements.md](docs/03-a1-interface-requirements.md) | draft ready |
| Test corpus proposal | [docs/04-test-corpus.md](docs/04-test-corpus.md) | draft ready |
| Risk report | [docs/05-risk-report.md](docs/05-risk-report.md) | draft ready |
| ReactOS review | [docs/06-reactos-review.md](docs/06-reactos-review.md) | draft ready |
| Stage 7 milestone | [docs/07-stage7-milestone.md](docs/07-stage7-milestone.md) | draft ready |
| A3 graphics requirements | [docs/08-a3-graphics-requirements.md](docs/08-a3-graphics-requirements.md) | draft ready |
| Open questions | [docs/09-open-questions.md](docs/09-open-questions.md) | draft ready |
| Technical findings (hashes, offsets, raw bytes) | [docs/10-technical-findings.md](docs/10-technical-findings.md) | draft ready |
| Stage 0 review response | [docs/11-stage0-review-response.md](docs/11-stage0-review-response.md) | draft ready |

Machine readable data:

- [data/pe32plus-structures.json](data/pe32plus-structures.json) — 21 PE
  structures with field offsets, sizes, citations and known specification gaps.
- [data/test-corpus.json](data/test-corpus.json) — 17 corpus entries across the
  five required categories, with prerequisites, pass criteria and honest status.

Tools:

- `tools/pe_inspect.py` — parses PE32/PE32+ headers, imports, exports,
  relocations, TLS, `.pdata`/unwind info (including chained entries), load
  config, CLR header, resources and certificates.
- `tools/build_fixtures.py` — builds the fixtures and records compiler version,
  exact command, source/output hashes and expected behaviour in
  `fixtures/build-manifest.json`; `--check` detects toolchain drift.
- `tools/apiset.py` — API set name parser and resolver with positive and negative
  tests (`data/apiset-schema.json` is a test schema, not Microsoft's).
- `tools/pe_mutate.py` — creates malformed images that each break one invariant.
- `tools/negative_report.py` — runs the malformed-image suite and requires the
  expected check to fail.
- `tools/finding_report.py` — prints hashes, file offsets and raw bytes behind
  the technical findings.
- `tools/pe_structs_check.py` — validates the structure inventory and checks it
  against real images (230 checks).
- `tools/corpus_lint.py` — validates the corpus manifest and its honesty rules
  (408 checks).
- `tools/docs_check.py` — validates that every deliverable exists, is linked,
  carries the required reporting fields and cites sources.
- `tools/fetch_corpus.py` — downloads pinned third-party samples into `.cache/`
  and verifies their SHA-256.

## Quick start

```sh
make check            # structure checks, negative tests, API sets, lint, docs, tests
make fixtures         # cross-compile fixtures and record provenance
make fixtures-check   # rebuild and compare against the committed manifest
make inspect          # parse fixtures and cached samples
make negative         # malformed-image suite with expected failures
make evidence         # write docs/evidence/*.txt
```

`make check` needs Python 3.10+ and a PE toolchain. The default recipe uses the
`ziglang` wheel (`pip install ziglang`), which bundles the MinGW-w64 headers and
libc++; the C++ fixture also links zig's libc++/libc++abi. A system MinGW-w64
compiler can build the C fixtures with `make ZIG=x86_64-w64-mingw32-gcc fixtures`,
but the C++ recipe would need adjusting to libstdc++.

Without any toolchain, `make check-static` validates the structure inventory,
the corpus manifest and the documents only.

No project licence has been chosen yet. The fixture sources are internal Stage 0
material; third-party binaries are never redistributed from this repository.

## Measured results in this repository

| Result | Command | Output |
| --- | --- | --- |
| 8 PE images parsed (5 fixtures, 3 third-party) | `make inspect` | [docs/evidence/pe-inspect.txt](docs/evidence/pe-inspect.txt) |
| 263 structure and layout checks | `python3 tools/pe_structs_check.py ...` | [docs/evidence/pe-structure-checks.txt](docs/evidence/pe-structure-checks.txt) |
| 558 corpus manifest checks | `python3 tools/corpus_lint.py` | [docs/evidence/corpus-lint.txt](docs/evidence/corpus-lint.txt) |
| Deliverable structure and citation checks | `python3 tools/docs_check.py` | [docs/evidence/docs-check.txt](docs/evidence/docs-check.txt) |
| 35 unit tests, including negative cases | `python3 -m unittest discover -s tests -v` | [docs/evidence/unit-tests.txt](docs/evidence/unit-tests.txt) |
| 8 malformed-image cases, each with an expected failure | `python3 tools/negative_report.py` | [docs/evidence/negative-tests.txt](docs/evidence/negative-tests.txt) |
| API set contracts resolved for a fixture | `python3 tools/apiset.py --check-image build/console_hello.exe` | [docs/evidence/apiset.txt](docs/evidence/apiset.txt) |
| Fixture rebuild matches the committed manifest | `python3 tools/build_fixtures.py --check` | [docs/evidence/reproducibility.txt](docs/evidence/reproducibility.txt) |

What the measurements show:

- The documented PE32+ layout reproduces real images: optional header size,
  alignment rules, sorted/adjacent sections, directory mapping, `.pdata` entry
  size, relocation blocks, import terminators, TLS callback termination, CLR
  header size and load-config size all pass on all eight images.
- Even `hello world` built with UCRT imports six `api-ms-win-crt-*` API set
  contracts, not a literal DLL file name. API set resolution is a loader
  requirement, not a later feature.
- The sample contains a real 32-bit binary with 9 546 relocations, a managed
  assembly whose entry point is rewritten by `mscoree`, and a native DLL with
  Control Flow Guard and chained unwind info.

What they do not show: that any application runs. Every corpus entry is
`not-run`.

## Acceptance criteria (from the briefing)

| Criterion | How it is met | Evidence |
| --- | --- | --- |
| PE format facts sourced to Microsoft documentation | every structure carries a source key; the inventory records the exact caveat for each gap | [data/pe32plus-structures.json](data/pe32plus-structures.json), `docs_check` citation rule |
| PE loading, Win32 compatibility and kernel drivers clearly distinguished | three classes (U/G/K) with separate tests and non-goals | [docs/00-compatibility-scope.md](docs/00-compatibility-scope.md) |
| Every milestone has observable pass/fail tests | M0–M5 with pass criteria and evidence format | [docs/07-stage7-milestone.md](docs/07-stage7-milestone.md) |
| No "full compatibility" claim without a corpus and measured results | claim phrases require measured evidence in the manifest; lint fails otherwise | `tools/corpus_lint.py`, negative test in `tests/test_tools.py` |
| Every sample has provenance, hash and an evidence level | `evidence_level`, `sandbox_required` and `provenance` are required fields; fixture sources are checked against the build manifest | `data/test-corpus.json`, `fixtures/build-manifest.json` |
| API sets, TLS, relocations, unwind and load config have negative tests | 8 mutations, each required to trigger a named check | `tools/negative_report.py`, `docs/10-technical-findings.md` |
| Dependencies on A1 interfaces are listed | 50+ interfaces with semantics, failure behaviour and tests | [docs/03-a1-interface-requirements.md](docs/03-a1-interface-requirements.md) |

## Reporting format

Every document opens with the fields the briefing requires:

```
- Status:            not started / researching / draft ready / blocked / submitted for review
- Evidence:          files, commands, outputs
- Decisions needed:  question, options, recommendation
- Risks:             what can fail, impact, mitigation
- Next step:         one concrete action
```

`tools/docs_check.py` enforces the presence of those fields.

## Repository layout

```
docs/            deliverables and evidence
data/            machine readable structure inventory and corpus manifest
tools/           inspection, validation and fetch tools
tests/           unit tests, including negative cases
fixtures/src/    sources for the locally built PE fixtures
build/           fixture output (git-ignored)
.cache/          downloaded third-party samples (git-ignored)
```

## Honesty rules

1. A pass or fail requires an execution record: environment, command, date and
   artifacts. The linter rejects the manifest otherwise.
2. Static analysis is recorded separately and labelled L0.
3. Third-party binaries are never committed. They are downloaded from a pinned
   URL, verified against a recorded SHA-256, and run only in a disposable
   sandbox with no credentials or host mounts.
4. Proprietary applications are analysis-only and are supplied by the operator.
5. Kernel drivers, anti-cheat and DRM are documented as out of scope, not as
   future features.

## Sources

- Microsoft, PE format specification: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, Unwind data structures for x64: <https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64>
- Microsoft, Dynamic-link library search order: <https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order>
- Microsoft, Windows API sets: <https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets>
- Microsoft, WOW64 implementation details: <https://learn.microsoft.com/en-us/windows/win32/winprog64/wow64-implementation-details>
- Microsoft, Managed execution process: <https://learn.microsoft.com/en-us/dotnet/standard/managed-execution-process>
- Microsoft, WDDM overview: <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide>
- Microsoft, Windows Driver Frameworks: <https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/>
- BattlEye FAQ: <https://www.battleye.com/faq>
- ReactOS architecture: <https://reactos.org/architecture/>
- Intel 64 and IA-32 Architectures Software Developer Manuals: <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
