# ADR-0001: Repository layout and ownership

| Field | Value |
| --- | --- |
| Status | Proposed |
| Date | 2026-11-23 |
| Owner | A4 |
| Approver | Project Director |
| Supersedes | — |
| Superseded by | — |
| Related | IF-003, IF-006, `CODEOWNERS` |

## Context

Four agents share one project. Without a fixed layout, two agents will create competing
directories, shared headers will be copied instead of included, and review ownership will be
ambiguous. Stage 0 needs a layout that can be enforced by `CODEOWNERS` before domain code exists.

The domain split is not yet confirmed by the Director. A4 can propose a split but must not decide
it.

## Decision

We will use one top-level directory per domain, with A4 owning the control plane:

- A1: `kernel/`, `arch/`, `include/o0/`
- A2: `drivers/`, `platform/`
- A3: `userland/`, `tools/`
- A4: `docs/`, `scripts/`, `tests/`, `harness/`, `toolchain/`, `.github/`, `third_party/` intake

Cross-domain includes go only through `include/o0/`. `CODEOWNERS` is the enforced copy of the
ownership map. The full table and rules are in `docs/architecture/repository-layout.md` and
`docs/architecture/ownership.md`.

## Options considered

| Option | Pros | Cons | Verdict |
| --- | --- | --- | --- |
| Flat `src/` tree | Simple for one person | No enforceable ownership; merge conflicts across domains | Rejected |
| Repository per agent | Strong isolation | Interface versioning and submodules before interfaces exist; heavy for Stage 0 | Rejected |
| One top-level directory per domain | Clear ownership, CODEOWNERS-friendly, normal subsystem shape | Requires discipline on shared headers | **Chosen** |

## Consequences

- Positive: review boundaries are mechanical, not social. A new domain is an explicit change.
- Negative: shared-header changes need two reviewers and are slower.
- Follow-up: confirm the A1/A2/A3 domain split and the GitHub team slugs.

## Risks

| Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- |
| Proposed domain split does not match actual work | Medium | Agents create overlapping trees | Director confirms before Stage 1 starts |
| CODEOWNERS teams do not exist | High | Reviews are not enforced | Director creates teams or provides slugs |
| Agents bypass layout for speed | Medium | Ownership erosion | A4 review on every PR; layout documented in `AGENTS.md` |

## Evidence

- `CODEOWNERS`
- `docs/architecture/repository-layout.md`
- `docs/architecture/ownership.md`

Status is **Proposed**. No domain code exists yet, so there is nothing to test.

## Approval

- Approved by:
- Date:
- Link:
