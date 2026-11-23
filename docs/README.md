# Ozero documentation index

| Area | Document | Status |
| --- | --- | --- |
| Architecture | [Repository layout](architecture/repository-layout.md) | Proposed |
| Architecture | [Ownership map](architecture/ownership.md) | Proposed |
| Architecture | [Interface registry](architecture/interfaces.md) | Proposed |
| Architecture | [IF-009 v1.1 disposition](architecture/if-009-v1.1-disposition.md) | Implemented at L0; signature pending |
| Architecture | [XB-005 hardware-evidence plan](architecture/xb-005-a3-hardware-evidence-plan.md) | Plan; blocker Open |
| Process | [Branch isolation and review gate](process/branching-and-review.md) | Proposed |
| Process | [Branch-protection audit](process/branch-protection-audit.md) | Audit complete; D-031 Proposed |
| Process | [Build and test plan](process/build-and-test.md) | Tested |
| Process | [Test targets](process/test-targets.md) | Implemented |
| Process | [CI safety](process/ci-safety.md) | Implemented |
| Process | [Cross-agent blocker register](process/cross-agent-blockers.md) | Live |
| Process | [Trust boundaries](process/trust-boundaries.md) | Reviewed |
| Process | [Integration checklist](process/integration-checklist.md) | Proposed |
| Process | [Reporting rules](process/reporting.md) | Implemented |
| Decisions | [ADR index](adr/README.md) | Proposed |
| Decisions | [Decision log](decisions/decision-log.md) | Live |
| Decisions | [D-005 boot-protocol package](decisions/D-005-boot-protocol-package.md) | Package; D-005 Proposed |
| Templates | [Agent report](templates/agent-report.md) | Ready to use |
| Templates | [Interface definition](templates/interface.md) | Ready to use |
| Templates | [Stage acceptance](templates/stage-acceptance.md) | Ready to use |
| Templates | [Decision log entry](templates/decision-log.md) | Ready to use |
| Dashboard | [Director dashboard](dashboard/director-dashboard.md) | Live |

## Reading order for a new agent

1. `AGENTS.md` — commands, conventions, non-negotiables.
2. `docs/architecture/ownership.md` — what you own and who reviews you.
3. `docs/architecture/interfaces.md` — what you must not break.
4. `docs/process/branching-and-review.md` — how work lands.
5. `docs/process/build-and-test.md` — how to prove it works.
6. `docs/process/trust-boundaries.md` — what you must not touch.

## Status meaning

**Proposed** means written and awaiting the Director. **Implemented** means the file or code exists.
**Tested** means a command was run and its output is attached. **Approved** means the Director
approved it, with a date and a link. Only the Director can move an item to Approved.
