# Interface registry

| Field | Value |
| --- | --- |
| Status | **Proposed** |
| Owner | A4 (registry), named owner per interface |
| Approver | Project Director for breaking changes |

Every shared interface has exactly one owner and an explicit version. An interface without an owner
does not merge. Breaking changes need an ADR and Director approval.

## Registry

| ID | Interface | Owner | Version | Consumers | Status |
| --- | --- | --- | --- | --- | --- |
| IF-001 | Boot handoff: image layout, entry state, memory assumptions | A1 | 0.1 draft | A2, A3, A4 | Proposed |
| IF-002 | Serial console contract (COM1, 8N1, polling write) | A2 | 0.1 draft | A1, A4 | Proposed |
| IF-003 | Boot image artifact contract (path, format, size, signature) | A4 | 1.0 | A1, A2, A3 | Implemented |
| IF-004 | Build manifest schema (`build/manifest.json`) | A4 | 1.0 | A1–A3, CI | Implemented |
| IF-005 | Test result schema (`result.json`) | A4 | 1.0 | CI, dashboard | Implemented |
| IF-006 | Public kernel header surface (`include/o0/`) | A1 | 0.0 | A2, A3 | Not started |
| IF-007 | Syscall / user-kernel ABI | A1 + A3 | — | A2 | Not started |
| IF-008 | DMA and platform interface contract | A3 | 0.1 draft | A1, A2 | Proposed |
| IF-009 | PE fixture and static-analysis contract | A2 | 0.1 draft | A4 | Proposed |

`IF-001` and `IF-002` are deliberately drafts. A4 owns the harness that consumes them, not the
technical decision. A1 and A2 must confirm or replace them.

`IF-008` carries a mandatory review gate: A1 must approve any A3 change to DMA or platform
interfaces before a dependent implementation merges. See the integration checklist.

## Versioning rules

- Versions are `MAJOR.MINOR`. `MAJOR` covers incompatible changes, `MINOR` covers additive changes.
- Additive change: update the registry row, note the consumer impact in the PR, no ADR.
- Breaking change: ADR with a migration note, Director approval, and one stage of overlap where
  both versions are accepted by the harness.
- An interface change is never bundled with an unrelated feature. One PR, one contract change.
- Cross-agent sign-off: a dependent implementation cannot merge before the interface owner records
  approval in the PR. The approval names the interface ID and version. This applies in particular
  to A1 approval of A3 DMA/platform interfaces (IF-008).

## Interface definition template

Copy this into `docs/interfaces/IF-<id>-<slug>.md` when an interface leaves draft. Until then the
registry row is enough.

Use [`docs/templates/interface.md`](../templates/interface.md) for the full document.

## Change process

1. Open a PR that updates this registry and the interface document together.
2. Tag the owner and every consumer as reviewers.
3. State compatibility: compatible, additive, or breaking.
4. For breaking changes, link the ADR and the Director approval.
5. Merge only after the consuming tests still pass against the new version.
