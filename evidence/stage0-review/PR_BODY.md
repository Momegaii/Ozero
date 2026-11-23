This PR was created by an AI agent (OpenHands) on behalf of A4.

## Summary

Stage 0 review follow-up for A4, against `main` (`147cc86`). This PR publishes the A4 control plane,
the review evidence, the cross-agent verification and the Director's 23 Nov A4 actions. It records
A1's IF-008 ACCEPT at `b9a69d8`, A3's acceptance of the §6 conditions at `307b919`, A3's integration
on the A4 control plane, and the A1/A3 IF-009 v1.0 reviews at `d02598a`. Publication is not approval.

**Stage 0 is not accepted.** This PR does not request a stage promotion. D-006 remains Proposed.

## Director A4 actions addressed

| # | Action | Result |
| --- | --- | --- |
| 1 | Open the A4 Stage 0 review PR | This PR |
| 2 | Refresh the audit after A3 is published | Done: A3 `307b919` integrated on the A4 control plane; XB-011 Closed |
| 3 | Verify A1/A2/A3 evidence and ancestry | A1 `74b1471`, A2 `64b3458` / `6d305b8`, A3 `307b919`; all descend from `main`; XB-001/014/017/018/019 Closed |
| 4 | Resolve namespace and ownership | D-009: `include/ozero/` only, A1 `03f8883`; D-010: A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`, A4 owns CI integration, A3 owns other `tools/` |
| 5 | Keep MBR harness separate from kernel boot | Only `mbr-harness-smoke` ran; Limine/kernel target not implemented |
| 6 | Confirm branch protection and checks | Not enabled: `protected:false`, `protection.enabled:false`, rulesets `[]`, `CODEOWNERS` 404 on `main` |

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
| this proceed-brief commit | A1 IF-008 ACCEPT, A3 integration and §6 acceptance, A1/A3 IF-009 reviews, round-4 verification |

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

Cross-agent static checks, raw logs in `evidence/stage0-review/`:

| Agent | Command | Result |
| --- | --- | --- |
| A1 | `tools/check-headers.sh`; ABI static asserts | PASS at `74b1471`; ABI log at `30ee4f0` |
| A2 | `make check-static` | exit 0; 61 checks, 0 failed at `64b3458` / code `6d305b8` |
| A3 | `make -C a3 check` | exit 0; 19 tests at `307b919` |
| A3 | `make -C a3 check-a1-headers A1_INCLUDE=...` | exit 0 against A1 `74b1471` |

## Tests not run

- UEFI boot.
- Limine loading an Ozero kernel.
- Ozero kernel entry or any kernel subsystem.
- Real-hardware boot or device matrix.
- PE loading or Windows compatibility.
- A3 driver code, AHCI probe or DMA transfer; A3's checks are static only.
- Branch protection enforcement, because protection is not enabled.

## Cross-agent dependencies

| ID | Dependency | State |
| --- | --- | --- |
| XB-001 | A1 reviews A3 DMA/platform/block interface before AHCI probe | Closed; A1 ACCEPT at `b9a69d8` (`74b1471`); A3 accepts §6 |
| XB-002 | A2 contracts match A1 process/address-space/handle/syscall | Signed; A1 answered all 12; A2 accepts all 12 at `6d305b8`; no ABI freeze |
| XB-003 | A2 PE fixture contract (IF-009) before any PE test | Open; A1 ACCEPT at `d02598a`; A3 ACCEPT at `307b919`; A2 signature record pending |
| XB-004 | A1 pins Limine and kernel handoff | Open; D-005 pending |
| XB-005 | A3 names device tests and hardware requirements with evidence | Open; static checks pass at `307b919`, but no QEMU/hardware driver test |
| XB-006 | A4 publishes the review branch | Closed at `84fe81a` |
| XB-007 | Header namespace | Closed by D-009: `include/ozero/` only |
| XB-008 | `tools/check-headers.sh` ownership | Closed by D-010: A1 owns script and ABI asserts; A4 CI; A3 other tools |
| XB-009 | A2 evidence revision mismatch | Closed: evidence headers at `6d305b8`; `64b3458` evidence-only |
| XB-010 | A4 opens the Stage 0 PR | Closed by this PR |
| XB-011 | A3 publishes so A4 can refresh the audit | Closed: A3 at `307b919`; audit refreshed; no PR implied |
| XB-012 | Enable `main` protection and required checks | Open; `protected:false`, rulesets `[]`, `CODEOWNERS` 404 on `main` (D-031) |
| XB-013 | A2 response and ledger lag the evidence pair and both signatures | Open; record `6d305b8` / `64b3458`, A1 `a17d126`, A3 `307b919` |
| XB-014 | A1 review of A2 is at `f324b28`; A2 moved | Closed; A1 re-reviewed `50b7853` / `807c447`; A2 accepts all 12 |
| XB-015 | A1 decision block collided with A4 IDs | Closed: D-009/D-010 merged; A1 D-011 kept; A4 protection renumbered D-031 |
| XB-016 | A1 command output is inline, not a raw log | Open; copy to `evidence/a1-stage0/` before merge |
| XB-017 | A3 rewrites root files and adds `platform.h` / `block.h` under A1-owned `include/ozero/` | Closed; A3 `307b919` integrates on A4, package in `a3/`, root docs preserved |
| XB-018 | A3 cites a stale A4 head | Closed; A3 `307b919` reviews against A4 `3ae7aca` |
| XB-019 | A3 has not accepted A1's IF-008 §6 conditions | Closed; A3 accepted all eight at `307b919` |

Snapshot: **6 Open, 12 Closed, 1 Signed.** A3 is integrated on the A4 control plane but has no PR;
the AHCI probe remains blocked by D-007/D-021. A3's package checks pass, but no driver or hardware
test ran.

## Decisions

D-008 (publish/PR), D-009 (`include/ozero/` only) and D-010 (tools ownership) are **Approved** by the
Director's 2026-11-23 instruction. D-001–D-007 and D-011–D-031 remain **Proposed**. D-006 (accept
Stage 0) is **not** approved. A1's D-009 and D-010 proposals were merged on the same topics; A1's
D-011–D-030 are recorded as Proposed; A4's branch-protection row is D-031.

## Evidence

- `evidence/stage0-review/REPORT.md` — required report and post-review addendum.
- `evidence/stage0-review/cross-agent-verification-2026-11-23-r4.md` — current refs, A1/A3 decisions, A3 integration, blocker snapshot.
- `evidence/stage0-review/cross-agent-commands-2026-11-23-r4.log` — raw commands for heads, ancestry, checks and GitHub settings.
- `evidence/stage0-review/a1-check-headers-74b1471.log`, `a1-abi-check.log`, `a2-static-check-64b3458.log`, `a3-check-307b919.log`, `a3-a1-header-check-307b919.log` — cross-agent static checks.
- `evidence/stage0-review/director-review-2026-11-23.md` — Director review and A4 disposition.
- `evidence/stage0-review/github-settings.txt` — protection, rulesets and CODEOWNERS state.
- `evidence/stage0-review/repo-audit.txt` — refs and commit graph.
- `evidence/stage0-review/ci.log`, `repro.log`, `clean-checkout.log`, `negative-*.log`.
