This PR was created by an AI agent (OpenHands) on behalf of A4.

## Summary

Stage 0 review follow-up for A4, against `main` (`147cc86`). This PR publishes the A4 control plane,
the review evidence, the cross-agent verification and the Director's 23 Nov A4 actions.

**Stage 0 is not accepted.** This PR does not request a stage promotion. D-006 remains Proposed.

## Director A4 actions addressed

| # | Action | Result |
| --- | --- | --- |
| 1 | Open the A4 Stage 0 review PR | This PR |
| 2 | Refresh the audit after A3 is published | Pending A3; audit covers A1/A2/A4 (XB-011) |
| 3 | Verify A1/A2/A3 evidence and ancestry | A1 at `115a7f4`, A2 at `807c447`, A3 absent; A1 reviewed A2 at older `f324b28` (XB-014) |
| 4 | Resolve namespace and ownership | D-009: `include/ozero/` only, A1 `03f8883`; D-010: A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`, A4 owns CI integration, A3 owns other `tools/` |
| 5 | Keep MBR harness separate from kernel boot | Only `mbr-harness-smoke` ran; Limine/kernel target not implemented |
| 6 | Confirm branch protection and checks | Not enabled: `"protected": false`, rulesets empty |

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
| audit commit | Record A1 second-round head, namespace alignment and ID mapping |

## Tests run at the published head

```text
$ make ci
serial:   build/qemu/serial.log
test: ok (5 markers in order)
ci: ok
exit=0
```

- QEMU 10.0.13, TCG, no network, no display, no default devices.
- 512-byte MBR fixture; 5 serial markers in order.
- Reproducibility, clean checkout and negative tests were recorded at `4396155` and are unchanged.

## Tests not run

- UEFI boot.
- Limine loading an Ozero kernel.
- Ozero kernel entry or any kernel subsystem.
- Real-hardware boot or device matrix.
- PE loading or Windows compatibility.
- A3 code, AHCI probe or DMA transfer.
- Branch protection enforcement, because protection is not enabled.

## Cross-agent dependencies

| ID | Dependency | State |
| --- | --- | --- |
| XB-001 | A1 reviews A3 DMA/platform/block interface before AHCI probe | Open; A1 conditions issued in `docs/a3-dma-contract.md`; A3 absent |
| XB-002 | A2 contracts match A1 process/address-space/handle/syscall | Open; A1 reviewed `f324b28`; A2 moved; XB-014 re-review |
| XB-003 | A2 PE fixture contract before any PE test | Open; A2 static-only |
| XB-004 | A1 pins Limine and kernel handoff | Open; D-005 pending |
| XB-005 | A3 names device tests and hardware requirements with evidence | Open; A3 absent |
| XB-006 | A4 publishes the review branch | Closed at `84fe81a` |
| XB-007 | Header namespace | Closed by D-009: `include/ozero/` only |
| XB-008 | `tools/check-headers.sh` ownership | Closed by D-010: A1 owns script and ABI asserts; A4 CI; A3 other tools |
| XB-009 | A2 evidence revision mismatch | Closed: evidence at `50b7853`; `807c447` evidence-only |
| XB-010 | A4 opens the Stage 0 PR | Closed by this PR |
| XB-011 | A3 publishes so A4 can refresh the audit | Open; A3 absent |
| XB-012 | Enable `main` protection and required checks | Open; D-031 |
| XB-013 | A2 response still cites `8bac650` | Open; update header to `50b7853` / `807c447` |
| XB-014 | A1 review of A2 is at `f324b28`; A2 moved | Open; A1 re-review or no-change confirmation |
| XB-015 | A1 decision block collided with A4 IDs | Closed: D-009/D-010 merged; A1 D-011 kept; A4 protection renumbered D-031 |

## Decisions

D-008 (publish/PR), D-009 (`include/ozero/` only) and D-010 (tools ownership) are **Approved** by the
Director's 2026-11-23 instruction. D-001–D-007 and D-011–D-031 remain **Proposed**. D-006 (accept
Stage 0) is **not** approved. A1's D-009 and D-010 proposals were merged on the same topics; A1's
D-011–D-030 are recorded as Proposed; A4's branch-protection row is D-031.

## Evidence

- `evidence/stage0-review/REPORT.md` — required report and post-review addendum.
- `evidence/stage0-review/cross-agent-verification-2026-11-23-r2.md` — ancestry and evidence check.
- `evidence/stage0-review/director-review-2026-11-23.md` — Director review and A4 disposition.
- `evidence/stage0-review/github-settings.txt` — protection and required-check state.
- `evidence/stage0-review/repo-audit.txt` — refs and commit graph.
- `evidence/stage0-review/ci.log`, `repro.log`, `clean-checkout.log`, `negative-*.log`.
