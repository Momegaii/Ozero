# Ozero Engineering Workflow

## Governance

The Project Director owns product direction, priorities, major architecture approvals, and acceptance of stage deliverables. Engineering agents own implementation within their assigned domains and report evidence.

## Agent responsibilities

- **A1 — Kernel engineering:** boot, kernel core, memory, processes, threads, scheduler, and kernel interfaces.
- **A2 — Windows compatibility:** PE loading, Win32 user-mode APIs, Windows behavior research, compatibility tests, and documented gaps.
- **A3 — Hardware engineering:** platform discovery, drivers, storage, GPU memory, GPU execution, and hardware validation.
- **A4 — Architecture, integration, build/test infrastructure and QA:** cross-domain contracts, build system, CI, integration planning, test strategy, and verification.

A4 coordinates integration; it does not silently override A1–A3 domain decisions. Cross-domain disagreements are documented and escalated.

## Isolation and review

- Each agent works in a separate branch or isolated workspace.
- Do not have multiple agents edit the same working tree concurrently.
- Changes are submitted as reviewable commits or pull requests.
- Integrate only after relevant tests and domain review.
- Protect secrets, host files, and raw devices. Grant the minimum permissions needed for a task.
- Never grant unrestricted host access as a default agent setting.

## Required change report

Every implementation report must include:

1. Scope and linked issue/task.
2. Files changed and design rationale.
3. Build commands and environment.
4. Tests run, exact results, and tests not run.
5. Known limitations and risks.
6. Commit SHA and branch/PR link.
7. Any architecture decision requiring Project Director approval.

## Definition of done

A change is not done merely because code was written. It must be reviewable, buildable in the documented environment, covered by appropriate tests, and accompanied by an honest report. Hardware claims require physical validation; emulation alone is not proof of device compatibility.
