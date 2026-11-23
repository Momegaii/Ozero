# Cross-agent blocker register

One row per dependency between agents. A dependent implementation does not merge until the owner
signs the interface and the evidence is linked here. Status words are **Open**, **Signed** or
**Closed**; only the named owner can move a row to Signed.

Snapshot 2026-11-23 after the R2 audit refresh: **7 Open, 12 Closed, 1 Signed.** Raw checks are in
[`evidence/stage0-review/cross-agent-verification-2026-11-23-r5.md`](../../evidence/stage0-review/cross-agent-verification-2026-11-23-r5.md)
and [`cross-agent-commands-2026-11-23-r5.log`](../../evidence/stage0-review/cross-agent-commands-2026-11-23-r5.log).
The earlier proceed brief named A2 `3f78477`; A2 advanced to `64b3458` (code `6d305b8`). The R2
brief uses that pair. A3 `307b919` is based on A4 `3ae7aca` and adds `a3/` plus root
`include/ozero/` on the A3 branch only. Those paths are **not** in A4 `b4232a0` or PR #3. The current
refs are A1 `74b1471`, A2 `64b3458`, A3 `307b919`, A4 `b4232a0`, `main` `147cc86`.

| ID | From → to | Dependency | State | Evidence or next action |
| --- | --- | --- | --- | --- |
| XB-001 | A1 → A3 | A1 reviews the A3 DMA/platform/block interface (IF-008) before any AHCI probe work | **Closed** | A1 recorded **ACCEPT** for IF-008 v1.1 at A3 `b9a69d8` in `docs/if008-v1.1-review.md` at A1 `74b1471` (decision commit `d9cdec1`). A3 accepted A1's §6 conditions in `a3/docs/interfaces/IF-008-v1.1-acceptance.md` at `307b919`. D-007/D-021 remain Proposed |
| XB-002 | A1 → A2 | Process, address-space, handle and syscall contracts match A1's kernel contracts | **Signed** | A1 answered `A1-REV-01…12` (4 Accept, 6 Change required, 2 Defer) at `30ee4f0` / `d9cdec1`. A2 accepts all 12 and answers A1's seven open items at `6d305b8`; evidence `64b3458`. No ABI freeze; D-011 remains Proposed |
| XB-003 | A2 → A1/A3 | PE fixture contract (IF-009) is fixed before any PE test | **Open** | A2 published IF-009 v1.0 at `d02598a` (evidence `3f78477`). A1 **ACCEPT** with conditions at A1 `74b1471` (decision commit `a17d126`); A3 **ACCEPT** for the hardware boundary at `307b919`. A2's contract and ledger still name `d02598a` / `3f78477` and mark the reviews pending, while its code/docs are `6d305b8` / `64b3458`. A2 records both signatures and the current pair. No PE test may run until then |
| XB-004 | A1 → A4 | Limine pin, boot protocol and kernel handoff (IF-001, D-005) | **Open** | A1 pinned v12.11.1, base revision 6, with hashes; Director D-005 is still pending. The MBR harness is not evidence |
| XB-005 | A3 → A4 | Device-specific tests and hardware requirements are named with evidence | **Open** | A3 `make check` (19 tests) and A1-header compile pass at `307b919`, but no QEMU driver run, AHCI probe, DMA transfer or real hardware exists. Static checks do not close this row |
| XB-006 | A4 → Director | Publish the A4 branch (D-008) | **Closed** | Published at `84fe81a`; upstream set to `origin/a4/stage0-review-followup` |
| XB-007 | A1 → A4 | Public header namespace conflict | **Closed** | D-009: canonical namespace is `include/ozero/`; A1's `03f8883` refactor makes it the only root; `tools/check-headers.sh` rejects `include/o0/`; A4 map updated |
| XB-008 | A1 → A3 | A1's `tools/check-headers.sh` sits in the A3-owned `tools/` tree | **Closed** | D-010: A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`; A4 owns CI integration; A3 owns other `tools/`; `CODEOWNERS` lists A1 and A4 on both files |
| XB-009 | A2 → A4 | A2 evidence headers cited an older revision than the branch head | **Closed** | A2 regenerated evidence at code revision `6d305b8`; evidence-only child `64b3458`; headers name `6d305b8` |
| XB-010 | A4 → Director | Open the A4 Stage 0 review pull request | **Closed** | Opened as [#3](https://github.com/Momegaii/Ozero/pull/3) at `076ed82` |
| XB-011 | A3 → A4 | Publish A3 so A4 can refresh the repository audit | **Closed** | A3 branch `a3/stage0-review-response` is published; current head `307b919`, based on A4 `3ae7aca`. The r4/r5 audit includes it. A3 has no PR, and this row does not imply one |
| XB-012 | A4 → Director | Enable `main` branch protection and required CI checks | **Open** | Rechecked: `branches/main` reports `protected:false` and `protection.enabled:false`; rulesets are `[]`; `CODEOWNERS` is 404 on `main`; required checks are not enabled (D-031) |
| XB-013 | A2 → A4 | A2's review response and ledger lag the evidence revision | **Open** | A2 corrected the `8bac650` and A1 `90b004f` / `include/o0/` citations at `6d305b8`. The response table and review ledger still name `d02598a` / `3f78477` and still mark the A1/A3 IF-009 reviews as pending; evidence headers name `6d305b8` / `64b3458`. A2 records the current pair and both signatures |
| XB-014 | A1 → A2 | A1 reviewed A2 at `f324b28`; A2 has since moved | **Closed** | A1 re-reviewed the added requirements at A2 `50b7853` / evidence `807c447` in `docs/a2-contract-review-response.md`; A2 accepted all 12 at `6d305b8`. The later A2 changes are docs/evidence, not new A1-REV requirements |
| XB-015 | A4 → Director | A1's D-009–D-030 block collided with A4's D-009–D-011 | **Closed** | D-009 and D-010 merged on the same topics; A1's D-011 retained; A4 branch protection renumbered D-031; D-012–D-030 recorded as Proposed |
| XB-016 | A1 → A4 | A1's command output is inline in its report, not a raw log | **Open** | A1 copies the output into `evidence/a1-stage0/` before merge; a report alone does not satisfy the evidence rule |
| XB-017 | A3 → A4 | A3's branch rewrites shared root files and adds interface headers under an A1-owned path | **Closed** | A3 `307b919` is based on A4 `3ae7aca`, moves its package into `a3/`, and preserves the root `README.md` / `AGENTS.md`. On the A3 branch, root `include/ozero/` contains A1's exact headers plus A3's two A1-approved IF-008 files. This row is about the A3 branch; it does not put those paths in A4 (XB-020) |
| XB-018 | A3 → A4 | A3's review basis cites a stale A4 head | **Closed** | A3 `307b919` reviews against A4 `3ae7aca` and records D-009 as `include/ozero/` with XB-007 Closed. The `245007d` basis is gone |
| XB-019 | A3 → A1/A4 | A3 has not recorded acceptance of A1's IF-008 §6 conditions or updated its status lines | **Closed** | A3 `307b919` adds `a3/docs/interfaces/IF-008-v1.1-acceptance.md`, accepts all eight §6 conditions, and updates `a3/docs/06`, `a3/AGENTS.md`, `a3/README.md` and `a3/EVIDENCE.md` to "A1 ACCEPT at `b9a69d8`; Director D-007 pending" |
| XB-020 | A3 → A4 | A3's published `a3/` package and root `include/ozero/` headers are not in the A4 candidate | **Open** | A4 `b4232a0` has no `a3/` and no root `include/ozero/`; at `b4232a0`, PR #3 had 95 changed files and no such paths. A3 `307b919` is based on A4 `3ae7aca` but is not an ancestor of A4. Either integrate A3 without overwriting shared files or keep A3 separate; no merge until the Director authorizes it |

## Rules

- The MBR harness target (`mbr-harness-smoke`) never satisfies XB-004. It is BIOS-only and does not
  load Limine or an Ozero kernel.
- A2's PE findings remain static-only. A runtime corpus is an opt-in, sandboxed job and is not
  normal CI.
- A1 accepted IF-008 v1.1 at `b9a69d8`; A3 accepted A1's §6 conditions at `307b919`. XB-001 and
  XB-019 are Closed. A3 still does not start AHCI probe work: the Director must decide D-007 and
  D-021. A3's `make check` and A1-header compile pass at `307b919`, but they are static checks, not a
  driver test.
- A3 has a published branch but no PR. XB-011 is closed because A4 refreshed the audit; it is not a
  merge approval. A3 `307b919` is based on A4 `3ae7aca` and moves its package into `a3/` on its own
  branch. A4 `b4232a0` and PR #3 do not contain `a3/` or root `include/ozero/`; XB-020 records the
  missing integration. Do not describe A3 as integrated into A4 until the A4 tree or PR diff shows it.
- XB-002 is Signed, not Closed: A1 answered and A2 accepted, but D-011 remains Proposed and no ABI is
  frozen. XB-003 remains Open until A2 records A1's and A3's IF-009 v1.0 signatures and the current
  evidence pair.
- A1's inline report is not a raw log. Copy A1's command output into `evidence/a1-stage0/` before
  merge; a report alone does not satisfy the evidence rule.
- A2's `a2/` package is the isolation model: one agent tree, no control-plane edits.
- A status change needs a commit, a command and raw output. A link to a branch alone is not evidence.
- The Director owns D-031 and the review under XB-010. A4 implemented D-009 and D-010 under the
  Director's 2026-11-23 instruction to resolve the namespace and ownership conflicts. A1's
  D-011–D-030 proposals are recorded in the canonical register; A1's D-009 and D-010 were merged
  into A4's rows on the same topics.
