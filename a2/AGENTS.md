# Repository notes

Stage 0 research package for Ozero A2 (Windows compatibility). The deliverables
are documents; the code is validation tooling and PE fixtures.

## Commands

- `make check` — full validation: build fixtures, structure checks, negative
  malformed-image cases, API set resolution, corpus lint, docs check, unit tests.
  Needs `python3` and the `ziglang` wheel (`pip install ziglang`); the C++ fixture
  links zig's libc++.
- `make fixtures-check` — rebuild fixtures and compare hashes with
  `fixtures/build-manifest.json` (toolchain drift detection).
- `make negative` — 8 malformed images, each required to trigger a named check.
- `make apiset-check` — resolve the API set contracts imported by a fixture.
- `make check-static` — inventory, corpus and docs checks without a PE toolchain.
- `make fixtures` — cross-compile `fixtures/src/*` into `build/`.
- `make inspect` — print parsed structure summaries for fixtures and `.cache/`.
- `make evidence` — regenerate `docs/evidence/*.txt`.
- `make corpus-fetch` — download hash-pinned samples into `.cache/`.

## Conventions

- Never claim an application runs. Corpus entries move off `not-run` only with
  environment, command, date and artifacts recorded; the lint enforces this.
- Every PE fact must cite a source. The inventory is
  `data/pe32plus-structures.json`; the checker validates it against real images.
- Third-party binaries are never committed. Pin a SHA-256 before executing one,
  and run them only in a disposable sandbox.
- Corpus entries carry `evidence_level` (`none`/`L0`…`L4`), `sandbox_required` and
  `provenance`; the lint rejects contradictions.
- Kernel drivers, anti-cheat and DRM are documented as out of scope, not planned.
- Do not copy ReactOS or Wine source; see `docs/06-reactos-review.md`.
