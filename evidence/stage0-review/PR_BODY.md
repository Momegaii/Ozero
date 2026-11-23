This PR was created by an AI agent (OpenHands) on behalf of A4.

## Summary

Stage 0 review follow-up for A4, against `main` (`147cc86`). This PR publishes the A4 control plane,
the review evidence, the cross-agent verification and the Director's 23 Nov A4 actions. It records
A1's IF-008 ACCEPT at `b9a69d8`, A3's acceptance of the §6 conditions at `307b919`, and the IF-009
v1.0 signatures: A1 **ACCEPT** with conditions at `74b1471` (`a17d126`), A3 **ACCEPT** for the
hardware boundary at `307b919`, and A2 signature at `6d305b8` (contract `d02598a`). Publication is
not approval.

The R2 audit corrected a material claim: A3 `307b919` is based on A4 `3ae7aca`, but A3 was **not**
integrated into this candidate. The R3 audit reviews A3 PR #4 at `48d434f`: it adds 56 files on top
of A4 `b4232a0` (44 under `a3/`, 12 under `include/ozero/`), changes no A4 control-plane file, and
merges cleanly from A4 `eaca921`. **A4 did not merge PR #4** because D-007 and D-021 remain Proposed
and the integration checklist requires Director authorization. A4 `eaca921` and this PR still
contain no `a3/` or root `include/ozero/` paths. XB-020 remains Open.

**Stage 0 is not accepted.** This PR does not request a stage promotion. D-006 remains Proposed.

## Director A4 actions addressed

| # | Action | Result |
| --- | --- | --- |
| 1 | Open the A4 Stage 0 review PR | This PR |
| 2 | Refresh the audit after A3 is published | Done: A3 PR #4 `48d434f` reviewed; it is not in A4; XB-011 Closed; XB-020 Open |
| 3 | Verify A1/A2/A3 evidence and ancestry | A1 `2693a34`, A2 `d83cfbe` / code `f6ed1d8`, A3 PR #4 `48d434f`, A4 `eaca921`; all descend from `main`; A3 is not an ancestor of A4; XB-001/014/017/018/019 Closed for their branches |
| 4 | Resolve namespace and ownership | D-009: `include/ozero/` only, A1 `03f8883`; D-010: A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`, A4 owns CI integration, A3 owns other `tools/`; `CODEOWNERS` now lists `/a3/` as A3-owned with A4 co-review |
| 5 | Keep MBR harness separate from kernel boot | Only `mbr-harness-smoke` ran; Limine/kernel target not implemented |
| 6 | Confirm branch protection and checks | Not enabled: `protected:false`, `protection.enabled:false`, rulesets `[]`, `CODEOWNERS` 404 on `main` |

## R3 audit

| # | Action | Result |
| --- | --- | --- |
| 1 | Review PR #4 as an integration PR | 56 files: 44 under `a3/`, 12 under `include/ozero/`; no non-intended path; no A4 control-plane overwrite |
| 2 | Verify ownership and headers | A1's ten root headers are byte-identical to A1 `74b1471`; A3's `platform.h`/`block.h` match its package; `/include/ozero/` and `/a3/` ownership recorded |
| 3 | Test the merge | Merge test from A4 `eaca921` is clean; PR #4 CI `build-and-boot` passed in 1m25s (run 45185176782); local `make -C a3 check` passes 19 tests and `check-a1-headers` exits 0 |
| 4 | Decide whether to merge | **Not merged**: D-007/D-021 Proposed and no Director authorization; A4 tree and PR #3 unchanged |
| 5 | Reconcile A2 IF-009 contract/ledger | IF-009 v1.0 **Signed**; XB-003 **Closed**; A2 ledger still names `6d305b8`/`64b3458`; current pair `f6ed1d8`/`d83cfbe`; XB-013 **Open** |
| 6 | Resolve XB-016 | **Closed**: A1 raw log at `docs/evidence/a1-raw-checks-2026-11-23-r2.txt` (A1 `2693a34`) |
| 7 | Recompute blockers | **5 Open, 14 Closed, 1 Signed** |

## R2 audit (historical)

| # | Action | Result at R2 |
| --- | --- | --- |
| 1 | Verify A3/A4 ancestry and paths | `3ae7aca` is an ancestor of A3 `307b919`; `307b919` is not an ancestor of A4 `b4232a0` |
| 2 | Check the A4 tree | No `a3/` and no root `include/ozero/` |
| 3 | Check the PR diff at `b4232a0` | 95 changed files; no `a3/` or root `include/ozero/`; the R2 audit commit added docs/evidence only |
| 4 | Correct XB-003 | A1 accepts IF-009 at `74b1471` (`a17d126`); A3 accepts the hardware boundary at `307b919`; A2 contract/ledger stale at that snapshot |
| 5 | Recompute the blocker snapshot | 7 Open, 12 Closed, 1 Signed; XB-020 added |
| 6 | Refresh A4 references | Dashboard, blocker register, evidence and PR body use A4 `b4232a0` at that snapshot |

## Commits

| Commit | Change |
| --- | --- |
| `5a50c0d` | Import A4 Stage 0 control plane |
| `4396155` | Implement Stage 0 review actions |
| `f990bf7` | Record Stage 0 review evidence |
| `907093d` | Refresh repository audit |
| `596fc6f` | Add cross-agent blocker register |
| `84fe81a` | Verify A1/A2 branches and track blockers |
| `8e73e09` | Record A4 branch publication |
| `5642c16` | Refresh audit with published A4 branch |
| `19dac9d` | Director round 2: namespace, ownership, settings, r2 verification |
| `076ed82` | Verify updated A2 evidence and ancestry |
| `9ef13bd` | Record Stage 0 PR #3 and close XB-010 |
| `210653c` | Align A4 with A1's second-round response and merge the decision IDs |
| `313138c` | Refresh the audit for the PR body update and add XB-016 |
| `56bcff0` | Apply A1's exact `CODEOWNERS` overrides from `30ee4f0` |
| `245007d` | Refresh the audit after A1's second-round commits |
| `3ae7aca` | Round-3 verification and A3 publication audit |
| `b4232a0` | A1 IF-008 ACCEPT, A3 §6 acceptance, A1/A3 IF-009 reviews, round-4 verification |
| `eaca921` | R2 audit: correct the A3 integration claim; add XB-020; refresh refs, blockers, evidence and PR body |
| this R3 audit commit | Review A3 PR #4 without merging; sign IF-009 v1.0 and close XB-003; close XB-016; record XB-013; refresh blockers, dashboard and evidence |

## Tests run at the published head

```text
$ make ci
serial:   build/qemu/serial.log
test: ok (5 markers in order)
ci: ok
exit=0
```

- QEMU 10.0.13, TCG, no network, no display, no default devices.
- 512-byte MBR fixture; 5 serial markers in order. This is not UEFI, Limine or kernel boot.
- Reproducibility, clean checkout and negative tests were recorded at `4396155` and are unchanged.
- GitHub Actions `build-and-boot` passed on A4 `eaca921` in 4m54s (run 45185158269) and on A3 PR #4
  `48d434f` in 1m25s (run 45185176782). The local `make ci` above was rerun on the R3 audit tree.

Cross-agent static checks, raw logs in `evidence/stage0-review/`:

| Agent | Command | Result |
| --- | --- | --- |
| A1 | `tools/check-headers.sh`; ABI static asserts | PASS at `74b1471`; ABI log at `30ee4f0`; raw log at A1 `2693a34` |
| A2 | `make check-static` | exit 0; 228 docs checks and 61 structure checks, 0 failed at `d83cfbe` / code `f6ed1d8` |
| A3 | `make -C a3 check` | exit 0; 19 tests at PR #4 `48d434f` |
| A3 | `make -C a3 check-a1-headers A1_INCLUDE=...` | exit 0 against A1 `74b1471` at PR #4 `48d434f` |

## Tests not run

- UEFI boot.
- Limine loading an Ozero kernel.
- Ozero kernel entry or any kernel subsystem.
- Real-hardware boot or device matrix.
- PE loading or Windows compatibility.
- A3 driver code, AHCI probe or DMA transfer; A3's checks are static only.
- Any merge of A3 PR #4 into the A4 branch.
- Branch protection enforcement, because protection is not enabled.

## Cross-agent dependencies

| ID | Dependency | State |
| --- | --- | --- |
| XB-001 | A1 reviews A3 DMA/platform/block interface before AHCI probe | Closed; A1 ACCEPT at `b9a69d8` (`74b1471`); A3 accepts §6 |
| XB-002 | A2 contracts match A1 process/address-space/handle/syscall | Signed; A1 answered all 12; A2 accepts all 12 at `6d305b8`; no ABI freeze |
| XB-003 | A2 PE fixture contract (IF-009) before any PE test | Closed; IF-009 v1.0 Signed by A1, A3 and A2; fixture manifest unchanged through `f6ed1d8` |
| XB-004 | A1 pins Limine and kernel handoff | Open; D-005 pending |
| XB-005 | A3 names device tests and hardware requirements with evidence | Open; static checks pass at `48d434f`, but no QEMU/hardware driver test |
| XB-006 | A4 publishes the review branch | Closed at `84fe81a` |
| XB-007 | Header namespace | Closed by D-009: `include/ozero/` only |
| XB-008 | `tools/check-headers.sh` ownership | Closed by D-010: A1 owns script and ABI asserts; A4 CI; A3 other tools |
| XB-009 | A2 evidence revision mismatch | Closed: evidence headers at `f6ed1d8`; evidence commit `d83cfbe` |
| XB-010 | A4 opens the Stage 0 PR | Closed by this PR |
| XB-011 | A3 publishes so A4 can refresh the audit | Closed: A3 PR #4 `48d434f`; audit refreshed; no merge implied |
| XB-012 | Enable `main` protection and required checks | Open; `protected:false`, rulesets `[]`, `CODEOWNERS` 404 on `main` (D-031) |
| XB-013 | A2 response and ledger lag the evidence pair | Open; ledger names `6d305b8` / `64b3458`; current pair `f6ed1d8` / `d83cfbe`; signatures recorded |
| XB-014 | A1 review of A2 is at `f324b28`; A2 moved | Closed; A1 re-reviewed `50b7853` / `807c447`; A2 accepts all 12 |
| XB-015 | A1 decision block collided with A4 IDs | Closed: D-009/D-010 merged; A1 D-011 kept; A4 protection renumbered D-031 |
| XB-016 | A1 command output is inline, not a raw log | Closed; raw log at `docs/evidence/a1-raw-checks-2026-11-23-r2.txt` (A1 `2693a34`) |
| XB-017 | A3 rewrites root files and adds `platform.h` / `block.h` under A1-owned `include/ozero/` | Closed for the A3 branch; package in `a3/`, root docs preserved; A3 is not in A4 |
| XB-018 | A3 cites a stale A4 head | Closed; A3 `307b919` reviews against A4 `3ae7aca` |
| XB-019 | A3 has not accepted A1's IF-008 §6 conditions | Closed; A3 accepted all eight at `307b919` |
| XB-020 | A3's `a3/` package and root `include/ozero/` headers are not in A4 `eaca921` | Open; PR #4 reviewed, merge-clean, CI pass, but not merged; A4 tree and this PR still lack those paths |

Snapshot: **5 Open, 14 Closed, 1 Signed.** A3 PR #4 is reviewed but not integrated; the AHCI probe
remains blocked by D-007/D-021. A3's package checks pass on PR #4, but no driver or hardware test ran.

## Decisions

D-008 (publish/PR), D-009 (`include/ozero/` only) and D-010 (tools ownership) are **Approved** by the
Director's 2026-11-23 instruction. D-001–D-007, D-011–D-030 and D-031 remain **Proposed**. D-006
(accept Stage 0) is **not** approved. D-007 and D-021 are **not** approved, so PR #4 was not merged.
IF-009 v1.0 is Signed; that is an interface signature, not Stage 0 acceptance.

## Evidence

- `evidence/stage0-review/REPORT.md` — required report and post-review addendum.
- `evidence/stage0-review/cross-agent-verification-2026-11-23-r6.md` — R3 audit: PR #4 review, header hashes, A2 pair, A1 raw log, blocker snapshot.
- `evidence/stage0-review/cross-agent-commands-2026-11-23-r6.log` — raw commands for PR #4 scope, merge test, A2 pair, A1 log, checks and governance.
- `evidence/stage0-review/cross-agent-verification-2026-11-23-r5.md` — R2 audit, corrected by r6 on the A3 PR state.
- `evidence/stage0-review/cross-agent-commands-2026-11-23-r5.log` — R2 raw commands.
- `evidence/stage0-review/a1-check-headers-74b1471.log`, `a1-abi-check.log`, `a2-static-check-64b3458.log`, `a3-check-307b919.log`, `a3-a1-header-check-307b919.log` — cross-agent static checks.
- `evidence/stage0-review/director-review-2026-11-23.md` — Director review and A4 disposition.
- `evidence/stage0-review/github-settings.txt` — protection, rulesets and CODEOWNERS state.
- `evidence/stage0-review/repo-audit.txt` — refs and commit graph.
- `evidence/stage0-review/ci.log`, `repro.log`, `clean-checkout.log`, `negative-*.log`.

## What this PR does not do

- It does not merge into `main`.
- It does not merge A3 PR #4 into the A4 branch.
- It does not accept Stage 0, D-006, D-007, D-021 or D-031.
- It does not claim AHCI, DMA, kernel boot, UEFI, Limine or Windows compatibility.
