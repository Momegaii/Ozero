# Cross-agent blocker register

One row per dependency between agents. A dependent implementation does not merge until the owner
signs the interface and the evidence is linked here. Status words are **Open**, **Signed** or
**Closed**; only the named owner can move a row to Signed.

Snapshot 2026-11-23 after A3 publication: **11 Open, 7 Closed, 0 Signed.** Raw checks are in
[`evidence/stage0-review/cross-agent-verification-2026-11-23-r3.md`](../../evidence/stage0-review/cross-agent-verification-2026-11-23-r3.md).

| ID | From → to | Dependency | State | Evidence or next action |
| --- | --- | --- | --- | --- |
| XB-001 | A1 → A3 | A1 reviews the A3 DMA/platform/block interface (IF-008) before any AHCI probe work | **Open** | A3 published IF-008 v1.1 at `b9a69d8` (branch head `15cd9ce`). A1 has not reviewed that version; A1's `30ee4f0` text still says the headers are unpublished. A1 signs the exact version before the AHCI probe |
| XB-002 | A1 → A2 | Process, address-space, handle and syscall contracts match A1's kernel contracts | **Open** | A1 reviewed A2 at `f324b28`; A2 is at `50b7853` / `807c447`. A1 has no `A1-REV-01`…`A1-REV-12` answers on its branch; XB-014 re-review first |
| XB-003 | A2 → A1/A3 | PE fixture contract (IF-009) is fixed before any PE test | **Open** | A2's `07-stage7-milestone.md` says XB-003 is Signed, but its `12-a1-contract-review.md` says Open. No explicit A1/A3 decision is recorded; an internal contradiction is not a sign-off |
| XB-004 | A1 → A4 | Limine pin, boot protocol and kernel handoff (IF-001, D-005) | **Open** | A1 pinned v12.11.1, base revision 6, with hashes; Director D-005 is still pending. The MBR harness is not evidence |
| XB-005 | A3 → A4 | Device-specific tests and hardware requirements are named with evidence | **Open** | A3 `make check` and A1-header compile pass at `15cd9ce`, but no QEMU driver run, AHCI probe, DMA transfer or real hardware exists. Static checks do not close this row |
| XB-006 | A4 → Director | Publish the A4 branch (D-008) | **Closed** | Published at `84fe81a`; upstream set to `origin/a4/stage0-review-followup` |
| XB-007 | A1 → A4 | Public header namespace conflict | **Closed** | D-009: canonical namespace is `include/ozero/`; A1's `03f8883` refactor makes it the only root; `tools/check-headers.sh` rejects `include/o0/`; A4 map updated |
| XB-008 | A1 → A3 | A1's `tools/check-headers.sh` sits in the A3-owned `tools/` tree | **Closed** | D-010: A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`; A4 owns CI integration; A3 owns other `tools/`; `CODEOWNERS` lists A1 and A4 on both files |
| XB-009 | A2 → A4 | A2 evidence headers cited an older revision than the branch head | **Closed** | A2 regenerated evidence at code revision `50b7853`; `807c447` adds only evidence files and headers cite `50b7853` |
| XB-010 | A4 → Director | Open the A4 Stage 0 review pull request | **Closed** | Opened as [#3](https://github.com/Momegaii/Ozero/pull/3) at `076ed82` |
| XB-011 | A3 → A4 | Publish A3 so A4 can refresh the repository audit | **Closed** | A3 branch `a3/stage0-review-response` is published at `15cd9ce`; the audit and round-3 verification include it. A3 has no PR, and this row does not imply one |
| XB-012 | A4 → Director | Enable `main` branch protection and required CI checks | **Open** | `branches/main` reports `protected:false` and `protection.enabled:false`; rulesets are `[]`; `CODEOWNERS` is 404 on `main`; required checks are not enabled (D-031) |
| XB-013 | A2 → A4 | A2's review response still cites import revision `8bac650` | **Open** | `11-stage0-review-response.md` cites `8bac650`; evidence headers cite `50b7853`; `807c447` is evidence-only; `12-a1-contract-review.md` still cites A1 `90b004f` and `include/o0/`. A2 reconciles all three |
| XB-014 | A1 → A2 | A1 reviewed A2 at `f324b28`; A2 has since moved to `50b7853` / `807c447` | **Open** | A1 re-reviews the added requirements or confirms no material change before XB-002 closes |
| XB-015 | A4 → Director | A1's D-009–D-030 block collided with A4's D-009–D-011 | **Closed** | D-009 and D-010 merged on the same topics; A1's D-011 retained; A4 branch protection renumbered D-031; D-012–D-030 recorded as Proposed |
| XB-016 | A1 → A4 | A1's command output is inline in its report, not a raw log | **Open** | A1 copies the output into `evidence/a1-stage0/` before merge; a report alone does not satisfy the evidence rule |
| XB-017 | A3 → A4 | A3's branch rewrites shared root files and adds interface headers under an A1-owned path | **Open** | A3 is based on `main`, adds an 84-line root `AGENTS.md` and replaces the root `README.md` with a 245-line A3 package README. It also adds `include/ozero/platform.h` and `block.h` under A1-owned `include/ozero/` (ownership map, `CODEOWNERS`). Rebase on the A4 base, keep A3 content in its own tree, and have A1 approve the two A3 interface files or move them |
| XB-018 | A3 → A4 | A3's review basis cites a stale A4 head and a stale D-009 | **Open** | A3 `docs/06` cites A4 `076ed82`; A4 is `245007d`. A3 `docs/06`, `AGENTS.md` and IF-008 v1.1 say A4 records `include/o0/` and XB-007 must reopen. D-009 is Approved as `include/ozero/` at `210653c`, and XB-007 is Closed. A3 updates the basis and D-009 row |

## Rules

- The MBR harness target (`mbr-harness-smoke`) never satisfies XB-004. It is BIOS-only and does not
  load Limine or an Ozero kernel.
- A2's PE findings remain static-only. A runtime corpus is an opt-in, sandboxed job and is not
  normal CI.
- A3 does not start AHCI probe work while XB-001 is Open. A3's `make check` and A1-header compile
  pass at `15cd9ce`, but they are static checks, not a driver test.
- A3 has a published branch but no PR. XB-011 is closed because A4 refreshed the audit; it is not a
  merge approval. A3 must rebase on the A4 control plane before a PR (XB-017) and correct its stale
  A4/D-009 basis (XB-018).
- A1's inline report is not a raw log. Copy A1's command output into `evidence/a1-stage0/` before
  merge; a report alone does not satisfy the evidence rule.
- A2's `a2/` package is the isolation model: one agent tree, no control-plane edits.
- A status change needs a commit, a command and raw output. A link to a branch alone is not evidence.
- The Director owns D-031 and the review under XB-010. A4 implemented D-009 and D-010 under the
  Director's 2026-11-23 instruction to resolve the namespace and ownership conflicts. A1's
  D-011–D-030 proposals are recorded in the canonical register; A1's D-009 and D-010 were merged
  into A4's rows on the same topics.
