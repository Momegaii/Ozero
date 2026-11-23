# A2 review ledger (23 November 2026)

- Status: current ledger; no row is closed by implication
- Evidence: reviewer documents on the named branches at the named commits
- Decisions needed: A4 records XB-002/XB-003 outcomes; Director decides D-006 and D-011…D-030
- Risks: a reader treats a hardware-boundary review as loader approval, or a pre-publication review as v1.0 acceptance
- Next step: A1 and A3 review IF-009 v1.0 at `d02598a`; A4 updates the blocker register with the exact commits

## Ledger

| Item | Reviewer | Revision reviewed | Verdict | A2 action | State |
| --- | --- | --- | --- | --- | --- |
| XB-002, A1-REV-01…12 | A1 | A2 `50b7853` + evidence `807c447`; A1 answers `30ee4f0` (`d9cdec1`) | 4 Accept, 6 Change required, 2 Defer | A2 accepts all 12 and answers A1's seven open items in [12](12-a1-contract-review.md) | A2 accepted; A4 recording pending |
| XB-003, IF-009 v1.0 | A1 | `807c447` (pre-publication manifest) | **Change required**: no IF-009 document; required contents in `docs/if009-review.md` §4 | v1.0 published at `d02598a` with identity, scope, inventory, ownership, references, delivery path, negatives, drift policy and deferrals | Open; A1 v1.0 review pending |
| XB-003, IF-009 v1.0 | A3 | `807c447` | **Accept with comments** (hardware boundary only): no hardware leakage; relative paths; neutrality sentence | Both comments applied; v1.0 confirmation pending | Open; not loader approval |
| XB-009, evidence | A4 | code `d02598a`, evidence `3f78477` | Verification pending | Pure-evidence commit; diff rule documented | Pending A4 |
| D-006, Stage 0 acceptance | Director | — | Proposed | — | Not approved |
| D-011…D-030 | Director | A1 proposals from `30ee4f0` | Proposed | A2 accepts D-024…D-030 conditions where they touch A2 | Not approved |

## A1 decisions accepted by A2

| A1-REV | Decision | Canonical ID |
| --- | --- | --- |
| 01 Process from image section | Change required (Stage 7 primitive) | D-011 |
| 02 User APCs | Change required (queue and alert hooks) | D-027 |
| 03 TEB and segment base | Accept | D-025 |
| 04 Section objects and image mapping | Change required (Stage 7) | D-011 |
| 05 Address-space layout and ASLR | Defer (A2 publishes layout) | D-026 |
| 06 Handle table and NT access masks | Accept | D-019 |
| 07 Wait-for-multiple | Change required (Stage 7) | D-011 |
| 08 Exception delivery | Change required (neutral record) | D-024 |
| 09 Unwind data access | Defer (A2 implements) | none |
| 10 Syscall boundary | Accept | D-018 |
| 11 Clock and timeouts | Change required (absolute timers) | D-029 |
| 12 Security model split | Accept | D-011, D-019 |

## What this ledger does not say

- XB-002 is not closed by A1 alone; A2 acceptance is recorded, but A4 owns the register.
- XB-003 is not closed by the A3 hardware-boundary review. A3 reviewed the boundary, not
  the loader, and did not confirm v1.0.
- IF-009 v1.0 is not accepted by A1; A1 reviewed the pre-publication manifest and
  required a contract, which now exists but has not been re-reviewed.
- D-006 is not approved. Stage 0 remains not accepted.

## Evidence pair

- Code revision: `d02598a`.
- Evidence head: `3f78477`.
- Rule: `git diff d02598a..3f78477 -- ':!a2/docs/evidence'` is empty.
- A later code commit invalidates the pair until evidence is regenerated.

## Sources

- A1 `docs/a2-contract-review-response.md` at `30ee4f0`; `docs/if009-review.md` at `d9cdec1`.
- A3 `docs/interfaces/IF-009-a3-review.md` at `2dcc0fc`.
- A4 `docs/decisions/decision-log.md` and `docs/process/cross-agent-blockers.md` at `3ae7aca`.
- [12-a1-contract-review.md](12-a1-contract-review.md), [13-if-009-pe-fixture-contract.md](13-if-009-pe-fixture-contract.md).
- A1 review PR: <https://github.com/Momegaii/Ozero/pull/1>
- A2 package PR: <https://github.com/Momegaii/Ozero/pull/2>
- A4 integration PR: <https://github.com/Momegaii/Ozero/pull/3>
