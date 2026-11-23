# ADR-0004: Agent trust boundaries and workspace isolation

| Field | Value |
| --- | --- |
| Status | Proposed |
| Date | 2026-11-23 |
| Owner | A4 |
| Approver | Project Director |
| Supersedes | — |
| Superseded by | — |
| Related | ADR-0002 |

## Context

Each agent can run shell commands and edit every file in its workspace. OpenHands separates the
browser UI, the backend and the workspace, and the workspace is the boundary that determines what an
agent can reach. A build task does not need host files, cloud credentials, a docker socket or raw
devices. A kernel boot task may need a disposable VM, but still not the host.

## Decision

We will run every agent in the least-privileged workspace that can do the task:

- Documentation and review: local workspace containing only the repository.
- Build and test: fresh container per task, repository mounted read-write, no host paths, no docker
  socket, no privileged mode.
- Kernel boot and raw-disk work: disposable VM with snapshot rollback and no host shares.
- Secrets are injected per task, never committed, never copied into an image or a log.
- Network is off by default; a new network dependency needs an ADR.
- QEMU runs with `-nodefaults -nic none -display none`, unprivileged, TCG only.
- A boundary violation stops the task, is reported, and triggers credential rotation.

Details and the per-agent permission matrix are in `docs/process/trust-boundaries.md`.

## Options considered

| Option | Pros | Cons | Verdict |
| --- | --- | --- | --- |
| Local host workspace for all agents | Fast, simple | Host and personal data exposed; no isolation between tasks | Rejected |
| Container per task | Good isolation, reproducible, cheap | Some setup; no kernel privileges | **Chosen** default |
| VM per task | Strong isolation, kernel-capable | Slower, heavier | **Chosen** for boot work |
| Shared long-lived sandbox | Fewer environments | Cross-task contamination, stale secrets | Rejected |

## Consequences

- Positive: a mistake is contained to a disposable workspace.
- Negative: VM tasks are slower and need a snapshot workflow.
- Follow-up: Director confirms the GitHub/CI secret scope; A4 documents the VM image.

## Risks

| Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- |
| Secret leaks into a log or image | Low | Credential compromise | Per-task injection, evidence review, rotation runbook |
| Over-broad mount for convenience | Medium | Host data exposure | Permission matrix, A4 review, no host paths in scripts |
| Prompt injection from repository text | Medium | Agent takes unsafe action | Treat docs as data; PR review; no auto-execution of fetched content |
| QEMU guest-to-host escape | Low | Host compromise | Reviewed images, disposable runner, no KVM/device passthrough |

## Evidence

- `docs/process/trust-boundaries.md`
- QEMU command line in `scripts/test-qemu.sh`
- CI workflow in `.github/workflows/stage0.yml`

## Approval

- Approved by:
- Date:
- Link:
