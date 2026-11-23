# Cross-agent blocker register

One row per dependency between agents. A dependent implementation does not merge until the owner
signs the interface and the evidence is linked here. Status words are **Open**, **Signed** or
**Closed**; only the named owner can move a row to Signed.

| ID | From → to | Dependency | State | Evidence or next action |
| --- | --- | --- | --- | --- |
| XB-001 | A1 → A3 | A1 reviews the A3 DMA/platform/block interface (IF-008) before any AHCI probe work | **Open** | A3 has no branch or interface version. A1 reviewed its own draft; that is not A3 sign-off. A3 publishes IF-008, then A1 signs that version |
| XB-002 | A1 → A2 | Process, address-space, handle and syscall contracts match A1's kernel contracts | **Open** | A2 published `a2/docs/03-a1-interface-requirements.md`; no A1 review is recorded |
| XB-003 | A2 → A1/A3 | PE fixture contract (IF-009) is fixed before any PE test | **Open** | A2 published the static-only package and evidence; A1/A3 review of the fixture contract is pending |
| XB-004 | A1 → A4 | Limine pin, boot protocol and kernel handoff (IF-001, D-005) | **Open** | A1 pinned v12.11.1, base revision 6, with hashes; Director D-005 is still pending. The MBR harness is not evidence |
| XB-005 | A3 → A4 | Device-specific tests and hardware requirements are named with evidence | **Open** | A3 has no branch, commit or test evidence |
| XB-006 | A4 → Director | Publish the A4 branch (D-008) | **Closed** | Published at `84fe81a`; upstream set to `origin/a4/stage0-review-followup` |
| XB-007 | A1 → A4 | Public header namespace conflict | **Closed** | D-009: canonical namespace is `include/o0/`; A1 keeps its headers; map, `CODEOWNERS`, ADR and interfaces updated |
| XB-008 | A1 → A3 | A1's `tools/check-headers.sh` sits in the A3-owned `tools/` tree | **Closed** | D-010: A1 owns `tools/check-headers.sh` with A3 review; specific `CODEOWNERS` entry added |
| XB-009 | A2 → A4 | A2 evidence headers cite `8bac650`, but the branch head is `f324b28` | **Open** | A2 regenerates evidence at the final head before merge |
| XB-010 | A4 → Director | Open the A4 Stage 0 review pull request | **Open** | Branch is on the remote; PR creation is in progress in this round |
| XB-011 | A3 → A4 | Publish A3 so A4 can refresh the repository audit | **Open** | A3 has no branch; the audit is refreshed for A1/A2/A4 and must be refreshed again after A3 publishes |
| XB-012 | A4 → Director | Enable `main` branch protection and required CI checks | **Open** | `protected:false`; rulesets count 0; required checks are not enabled (D-011) |

## Rules

- The MBR harness target (`mbr-harness-smoke`) never satisfies XB-004. It is BIOS-only and does not
  load Limine or an Ozero kernel.
- A2's PE findings remain static-only. A runtime corpus is an opt-in, sandboxed job and is not
  normal CI.
- A3 does not start AHCI probe work while XB-001 is Open.
- A1's inline report is not a raw log. Copy A1's command output into `evidence/a1-stage0/` before
  merge; a report alone does not satisfy the evidence rule.
- A2's `a2/` package is the isolation model: one agent tree, no control-plane edits.
- A status change needs a commit, a command and raw output. A link to a branch alone is not evidence.
- The Director owns D-011 and the review under XB-010. A4 implemented D-009 and D-010 under the
  Director's 2026-11-23 instruction to resolve the namespace and ownership conflicts.
