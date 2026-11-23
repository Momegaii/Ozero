# Cross-agent blocker register

One row per dependency between agents. A dependent implementation does not merge until the owner
signs the interface and the evidence is linked here. Status words are **Open**, **Signed** or
**Closed**; only the named owner can move a row to Signed.

| ID | From → to | Dependency | State | Evidence or next action |
| --- | --- | --- | --- | --- |
| XB-001 | A1 → A3 | A1 reviews the A3 DMA/platform/block interface (IF-008) before any AHCI probe work | **Open** | A3 has no branch or interface version. A1 issued conditions in `docs/a3-dma-contract.md`; A3 publishes IF-008, then A1 signs that version |
| XB-002 | A1 → A2 | Process, address-space, handle and syscall contracts match A1's kernel contracts | **Open** | A1 reviewed A2 at `f324b28` with conditions; A2 moved to `50b7853` / `807c447`; XB-014 re-review before close |
| XB-003 | A2 → A1/A3 | PE fixture contract (IF-009) is fixed before any PE test | **Open** | A2 published the static-only package and evidence; A1/A3 review of the fixture contract is pending |
| XB-004 | A1 → A4 | Limine pin, boot protocol and kernel handoff (IF-001, D-005) | **Open** | A1 pinned v12.11.1, base revision 6, with hashes; Director D-005 is still pending. The MBR harness is not evidence |
| XB-005 | A3 → A4 | Device-specific tests and hardware requirements are named with evidence | **Open** | A3 has no branch, commit or test evidence |
| XB-006 | A4 → Director | Publish the A4 branch (D-008) | **Closed** | Published at `84fe81a`; upstream set to `origin/a4/stage0-review-followup` |
| XB-007 | A1 → A4 | Public header namespace conflict | **Closed** | D-009: canonical namespace is `include/ozero/`; A1's `03f8883` refactor makes it the only root; `tools/check-headers.sh` rejects `include/o0/`; A4 map updated |
| XB-008 | A1 → A3 | A1's `tools/check-headers.sh` sits in the A3-owned `tools/` tree | **Closed** | D-010: A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`; A4 owns CI integration; A3 owns other `tools/`; `CODEOWNERS` updated |
| XB-009 | A2 → A4 | A2 evidence headers cited an older revision than the branch head | **Closed** | A2 regenerated evidence at code revision `50b7853`; `807c447` adds only evidence files and headers cite `50b7853` |
| XB-010 | A4 → Director | Open the A4 Stage 0 review pull request | **Closed** | Opened as [#3](https://github.com/Momegaii/Ozero/pull/3) at `076ed82` |
| XB-011 | A3 → A4 | Publish A3 so A4 can refresh the repository audit | **Open** | A3 has no branch; the audit is refreshed for A1/A2/A4 and must be refreshed again after A3 publishes |
| XB-012 | A4 → Director | Enable `main` branch protection and required CI checks | **Open** | `protected:false`; rulesets count 0; required checks are not enabled (D-031) |
| XB-013 | A2 → A4 | A2's review response still cites import revision `8bac650` | **Open** | A2 updates the response header to code revision `50b7853` and evidence commit `807c447` |
| XB-014 | A1 → A2 | A1 reviewed A2 at `f324b28`; A2 has since moved to `50b7853` / `807c447` | **Open** | A1 re-reviews the added requirements or confirms no material change before XB-002 closes |
| XB-015 | A4 → Director | A1's D-009–D-030 block collided with A4's D-009–D-011 | **Closed** | D-009 and D-010 merged on the same topics; A1's D-011 retained; A4 branch protection renumbered D-031; D-012–D-030 recorded as Proposed |
| XB-016 | A1 → A4 | A1's command output is inline in its report, not a raw log | **Open** | A1 copies the output into `evidence/a1-stage0/` before merge; a report alone does not satisfy the evidence rule |

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
- The Director owns D-031 and the review under XB-010. A4 implemented D-009 and D-010 under the
  Director's 2026-11-23 instruction to resolve the namespace and ownership conflicts. A1's
  D-011–D-030 proposals are recorded in the canonical register; A1's D-009 and D-010 were merged
  into A4's rows on the same topics.
