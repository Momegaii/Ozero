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
| IF-006 | Public kernel header surface (`include/ozero/`) | A1 | 0.2 draft | A2, A3 | Draft; A1 published `03f8883`; headers unchanged at `74b1471` |
| IF-007 | Syscall / user-kernel ABI | A1 + A3 | — | A2 | Not started |
| IF-008 | DMA and platform interface contract | A3 | 1.1 | A1, A2 | A1 **ACCEPT** at A3 `b9a69d8` (A1 `74b1471`); A3 accepted §6 at `307b919`; D-007/D-021 Proposed; AHCI probe blocked |
| IF-009 | PE fixture and static-analysis contract | A2 | 1.0 | A1, A3, A4 | **Signed** v1.0: A1 **ACCEPT** with conditions at `74b1471` (`a17d126`); A3 **ACCEPT** for hardware boundary at `307b919`; A2 signature recorded at `6d305b8` (contract `d02598a`); XB-003 Closed. v1.1 six negative cases implemented at `31e6825`, evidence `37e9bf0`; XB-021 Closed for implementation; A1/A3 v1.1 re-sign pending |

`IF-001` and `IF-002` are deliberately drafts. A4 owns the harness that consumes them, not the
technical decision. A1 and A2 must confirm or replace them.

`IF-008` carries a mandatory review gate: A1 must approve any A3 change to DMA or platform
interfaces before a dependent implementation merges. A1 recorded **ACCEPT** for v1.1 at `b9a69d8`
in A1 `74b1471`, with eight conditions in §6 of its review. A3 accepted all eight conditions at
`307b919` in `a3/docs/interfaces/IF-008-v1.1-acceptance.md`. Director D-007 and D-021 remain
Proposed, so the AHCI probe remains blocked. A1's acceptance is an interface decision, not hardware
or runtime evidence.

`IF-009` is **Signed** at v1.0. A2 published v1.0 at `d02598a`; A1 **ACCEPT**ed it with conditions
at A1 `74b1471` (decision commit `a17d126`), A3 **ACCEPT**ed the hardware boundary at `307b919`, and
A2 recorded its signature at `6d305b8`. The fixture manifest is unchanged from `d02598a` through A2
`f6ed1d8`; the current A2 code/evidence pair is `f6ed1d8` / `d83cfbe`. A2's branch ledger still names
the older `6d305b8` / `64b3458` pair; A4's consolidated reconciliation is
[`a2-review-ledger-reconciled.md`](a2-review-ledger-reconciled.md), and XB-013 is closed in the A4
candidate. The signature is not loader approval and does not authorize a PE run; no loader exists.

The IF-009 v1.1 candidate adds four truncated-header cases and two import-Name-RVA cases. It is
implemented at `31e6825`, with raw evidence at `37e9bf0`; the six cases all fail for the named
reason. XB-021 is closed for implementation, but IF-009 v1.1 is **not signed**: A1 and A3 must
review and re-sign. v1.0 remains the accepted version until then.
The v1.1 disposition, implementation and evidence are in
[`if-009-v1.1-disposition.md`](if-009-v1.1-disposition.md); XB-021 is closed for implementation and
the v1.1 signature gate is still open.

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
