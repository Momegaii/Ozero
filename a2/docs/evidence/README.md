# Evidence artifacts

Every file here is generated, not hand-written. Regenerate with:

```sh
make evidence
```

| File | Produced by | What it shows |
| --- | --- | --- |
| `pe-inspect.txt` | `tools/pe_inspect.py` | parsed headers, imports, relocations, TLS, `.pdata`, load config, CLR header for every fixture and cached sample |
| `pe-structure-checks.txt` | `tools/pe_structs_check.py` | inventory consistency plus 263 layout checks against 8 real images |
| `corpus-lint.txt` | `tools/corpus_lint.py` | corpus structure, coverage, evidence-level, sandbox and provenance rules (559 checks) |
| `docs-check.txt` | `tools/docs_check.py` | deliverable presence, reporting fields, citations and local links |
| `unit-tests.txt` | `unittest` | 40 tool tests including the negative cases that must fail |
| `technical-findings.txt` | `tools/finding_report.py` | sample SHA-256, file offsets, raw bytes and references for API sets, TLS, relocations, chained unwind and load config |
| `negative-tests.txt` | `tools/negative_report.py` | 8 malformed-image cases, each with the check it must fail and the observed detail |
| `apiset.txt` | `tools/apiset.py --check-image` | API set contracts imported by the console fixture and their test-schema hosts |
| `reproducibility.txt` | `tools/build_fixtures.py --check` | fixture rebuild compared with the committed build manifest |

Each file starts with the git revision and date it was produced. Binary inputs
are not stored here: fixtures are rebuilt from `fixtures/src`, and third-party
samples are fetched into `.cache/` and verified against the SHA-256 in
`data/test-corpus.json`.

Nothing in these files is an execution result for a Windows application. They are
static-analysis, negative-test and document-validation results only. The
`negative-tests.txt` failures are expected: each one is produced by a deliberately
corrupted copy of a real image, not by a defect in the checker.

## Revision history

| Date | Code revision | Evidence commit | Note |
| --- | --- | --- | --- |
| 2026-11-23 | `8bac650` | `f324b28` | initial import; historical |
| 2026-11-23 | `50b7853` | `807c447` | director review response; historical once superseded |

## Revision rule

Evidence is generated from a code revision and committed in a pure-evidence
commit whose parent is that revision. The header in each file names the code
revision. To verify that the evidence matches the reviewed tree:

```sh
git diff <code-revision>..HEAD -- ':!a2/docs/evidence'
```

The diff must be empty. This keeps the evidence tied to a verifiable commit
without a self-referential hash inside the evidence files.
