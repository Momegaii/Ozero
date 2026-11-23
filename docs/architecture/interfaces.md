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
| IF-006 | Public kernel header surface (`include/ozero/`) | A1 | 0.2 draft | A2, A3 | Draft; A1 published `03f8883`; headers at `30ee4f0` |
| IF-007 | Syscall / user-kernel ABI | A1 + A3 | — | A2 | Not started |
| IF-008 | DMA and platform interface contract | A3 | 1.1 | A1, A2 | Published by A3 at `b9a69d8` (branch head `15cd9ce`); A1 final approval pending; AHCI probe blocked |
| IF-009 | PE fixture and static-analysis contract | A2 | 0.1 draft | A1, A3, A4 | Proposed; A2 `07-stage7-milestone.md` says Signed, `12-a1-contract-review.md` says Open; no raw sign-off |

`IF-001` and `IF-002` are deliberately drafts. A4 owns the harness that consumes them, not the
technical decision. A1 and A2 must confirm or replace them.

`IF-008` carries a mandatory review gate: A1 must approve any A3 change to DMA or platform
interfaces before a dependent implementation merges. A3 published v1.1 at `b9a69d8`; A1's
`30ee4f0` sign-off is conditional on that publication and is not the final approval. See the
integration checklist.

`IF-009` is not Signed. A2's milestone document says it is, but A2's own contract-review document
says Open, and neither A1 nor A3 records an explicit decision. A4 keeps the registry row at
Proposed until a named owner records the version and the decision.

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
