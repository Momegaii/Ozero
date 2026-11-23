# Repository layout proposal

| Field | Value |
| --- | --- |
| Status | **Proposed** (ADR-0001) |
| Owner | A4 |
| Approver | Project Director |
| Applies to | Stage 0 onward |

This proposal defines where code lives, who owns it, and which directories may change without a
cross-agent review. It is a proposal, not an accepted decision. Domain names below are placeholders
until A1–A3 confirm their scope with the Director.

## Proposed tree

```
/
├── CODEOWNERS                     ownership map (machine-readable)
├── README.md                      entry point
├── AGENTS.md                      repository memory for every agent
├── Makefile                       build/test entry points
├── .github/workflows/             CI
├── docs/                          A4: process, architecture, ADRs, templates, dashboard
├── toolchain/                     A4: toolchain lock and container definition
├── scripts/                       A4: build, QEMU test, reproducibility, clean-checkout
├── harness/                       A4: boot fixture that proves the harness
├── tests/                         A4: expected markers, shared test data
├── evidence/                      A4: recorded runs tied to commit hashes
├── include/ozero/                 A1: shared public headers (interface surface)
├── kernel/                        A1: kernel core
├── arch/                          A1: architecture-specific code
├── drivers/                       A2: device drivers
├── platform/                      A2: platform and board support
├── userland/                      A3: user-space programs and runtime
├── tools/                         A3: host-side project tools
└── third_party/                   A4 intake, consuming owner approves content
```

Only the A4 directories exist today. `include/`, `kernel/`, `arch/`, `drivers/`, `platform/`,
`userland/` and `tools/` are created by their owners in Stage 1. Creating a directory does not
grant ownership of another agent's tree.

## Layout rules

1. **One top-level directory, one owner.** A pull request that changes two owners' directories
   needs both owners as reviewers. `CODEOWNERS` enforces this.
2. **Public surface is narrow.** Cross-domain includes go through `include/ozero/` only. Reaching
   into another domain's private headers is a review blocker.
3. **Shared headers are versioned.** A change to `include/ozero/` is an interface change: update
   `docs/architecture/interfaces.md`, state compatibility, and get A1 plus A4 review.
4. **Build outputs never enter git.** `build/` is ignored. Anything needed to reproduce a result
   is either source or a pinned dependency.
5. **No undeclared dependencies.** Every external dependency needs a pinned version, a licence
   note, an owner and a hash. Unpinned `latest` references are rejected.
6. **Tests live with the code they test.** A1–A3 keep unit tests inside their trees; A4 keeps
   cross-cutting boot, integration and regression tests under `tests/`.
7. **Docs live next to the process they describe.** Architecture under `docs/architecture/`,
   process under `docs/process/`, decisions under `docs/adr/`.

## Naming

- Directories: lower-case, hyphen-free, singular domain (`kernel`, `drivers`, not `Kernel/`).
- C sources: `snake_case.c`, headers `snake_case.h`.
- Assembly: `*.S` for preprocessed assembly, `*.s` for raw assembly.
- Branches: `a<N>/<ticket>-<slug>`. Tags: `stage-<n>-<status>` (for example `stage-0-accepted`).

## Build artifacts

| Path | Produced by | Purpose | Committed |
| --- | --- | --- | --- |
| `build/ozero-stage0.elf` | `make build` | ELF with debug symbols | no |
| `build/ozero-stage0.img` | `make build` | Raw boot image (512 bytes in Stage 0) | no |
| `build/ozero-stage0.map` | `make build` | Linker map | no |
| `build/manifest.json` | `make build` | Commit, toolchain, hashes, sizes | no |
| `build/qemu/result.json` | `make test` | Machine-readable test result | no |
| `build/qemu/serial.log` | `make test` | Raw serial output | no |
| `evidence/<stage>/<date>/` | A4 | Curated copy of the above, with a README | yes |

## Alternatives considered

| Option | Why not |
| --- | --- |
| Flat repository (`src/` for everything) | Ownership becomes ambiguous; CODEOWNERS cannot express domain boundaries. |
| One repository per agent | Cross-agent integration would need version pinning and submodules before the interfaces exist. Too early for Stage 0. |
| Monorepo with `agents/a1/...` nesting | Duplicates the domain split and makes shared headers awkward. A domain tree should read like a normal subsystem. |

## Consequences

- A4 can enforce review boundaries mechanically through `CODEOWNERS`.
- Adding a new domain requires a new top-level directory, an owner and a CODEOWNERS entry. That is
  intentional friction.
- Shared-header changes become slightly slower. That is the price of a stable interface.

## Open questions for the Director

1. Confirm the A1/A2/A3 domain split (kernel+arch / drivers+platform / userland+tools).
2. Confirm the GitHub team slugs used in `CODEOWNERS`, or provide the real ones.
3. Confirm whether `third_party/` is allowed at all in Stage 1.
