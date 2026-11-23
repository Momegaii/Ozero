# A2 review ledger (23 November 2026)

- Status: current ledger; no row is closed by implication
- Evidence: reviewer documents on the named branches at the named commits
- Decisions needed: A4 records XB-002/XB-003 outcomes; Director decides D-006 and D-011…D-030
- Risks: a reader treats a hardware-boundary review as loader approval, or a pre-publication review as v1.0 acceptance
- Next step: A1 and A3 review IF-009 v1.0 at `d02598a`; A4 updates the blocker register with the exact commits

## Ledger

| Item | Reviewer | Revision reviewed | Verdict | A2 action | State |
| --- | --- | --- | --- | --- | --- |
| XB-002, A1-REV-01…12 | A1 | A1 `74b1471` (`a17d126`), reconciled to A2 `d02598a`/`3f78477` | 4 Accept, 6 Change required, 2 Defer | A2 accepts all 12 at `6d305b8` and answers A1's seven open items in [12](12-a1-contract-review.md) | A2 accepted; A4 recording pending |
| XB-003, IF-009 v1.0 | A1 | `d02598a` (contract blob `9be6c8b0…`), review `a17d126`, head `74b1471` | **Accept with conditions** | Conditions: L0 static only; version discipline; review-record corrections; A3 v1.0 confirmation; A4 registry update; no file-backed section claim. A2 resolved the review-record corrections at `6d305b8` | A1 accepted; A4 registry pending |
| XB-003, IF-009 v1.0 | A3 | `d02598a`, review `307b919` | **Accept for the hardware boundary**, no blocking comments | IF9-A3-1/2 answered; not loader approval | A3 accepted; A4 registry pending |
| IF-009 A2 signature | A2 | v1.0 contract at `6d305b8`; signed revision `d02598a` | Recorded | Fixture sources, flags, target, compiler and hashes unchanged from `d02598a`; v1.1 tracks the two negative-case gaps | Awaiting A4 record |
| XB-009, evidence | A4 | code `6d305b8`, evidence `64b3458` | Verification pending | Pure-evidence commit; diff rule documented | Pending A4 |
| D-006, Stage 0 acceptance | Director | — | Proposed | — | Not approved |
| D-011…D-030 | Director | A1 proposals from `74b1471` | Proposed | A2 accepts D-024…D-030 conditions where they touch A2 | Not approved |

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
- XB-003 is not closed until A4 records both signatures and updates the IF-009 row to
  v1.0. A1 accepted with conditions; A3 accepted the hardware boundary only. Neither is
  loader approval or a runtime claim.
- A1's condition 3 (stale `90b004f`/`include/o0` references and D-009…D-023 collisions) is
  resolved at `6d305b8`; A1's other conditions remain tracked.
- IF-009 v1.0 still has the truncated-header and missing-DLL-name negative gaps. They do
  not block A1/A3 v1.0 acceptance but must be closed in v1.1 or explicitly dispositioned.
- D-006 is not approved. Stage 0 remains not accepted.

## Evidence pair

- Code revision: `6d305b8` (signed IF-009 v1.0 contract was `d02598a`).
- Evidence head: `64b3458`.
- Rule: `git diff 6d305b8..64b3458 -- ':!a2/docs/evidence'` is empty.
- A later code commit invalidates the pair until evidence is regenerated.

## Sources

- A1 `docs/a2-contract-review-response.md` and `docs/if009-review.md` at `74b1471` (`a17d126`).
- A3 `a3/docs/interfaces/IF-009-a3-review.md` at `307b919`.
- A4 `docs/decisions/decision-log.md` and `docs/process/cross-agent-blockers.md` at `3ae7aca`.
- [12-a1-contract-review.md](12-a1-contract-review.md), [13-if-009-pe-fixture-contract.md](13-if-009-pe-fixture-contract.md).
- A1 review PR: <https://github.com/Momegaii/Ozero/pull/1>
- A2 package PR: <https://github.com/Momegaii/Ozero/pull/2>
- A4 integration PR: <https://github.com/Momegaii/Ozero/pull/3>
