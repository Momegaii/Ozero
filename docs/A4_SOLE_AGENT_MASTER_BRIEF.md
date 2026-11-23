# OZERO - A4 SOLE-AGENT MASTER BRIEF

Single-agent execution record and consolidated status
23 November 2026
Repository: github.com/Momegaii/Ozero
Branch: a4/stage0-review-followup
Prepared by: A4

This is the one consolidated A4 master brief. It replaces separate A1, A2 and A3 briefs. It is an
execution record, not a Stage 0 approval, merge authorization or safety waiver.

## 1. Authority and acknowledgement

A4 acknowledges the 23 November 2026 all-work takeover order. A1, A2 and A3 are inactive because
their separate sandboxes exhausted their usage. A4 is the one remaining active agent and continues
their unfinished engineering, integration, evidence, review and coordination work from one sandbox.

A4 did not restart Stage 0. It recovered existing branches, commits, pull requests and evidence
first. Historical authorship and ownership are preserved; earlier A1/A2/A3 work is not relabeled as
A4 work.

Governing principle: accelerate implementation, parallelize safe engineering, demand evidence, and
preserve safety gates.

## 2. Live baseline

| Item | State |
| --- | --- |
| main | 147cc86 (unchanged) |
| A1 branch | a1/stage0-review at 2693a34; PR #1 draft |
| A2 branch | agent/a2-stage0 at d83cfbe; code f6ed1d8; PR #2 draft |
| A3 branch | a3/stage0-review-response at 48d434f; PR #4 open |
| A4 before takeover | a3a67ab |
| A3 integration merge | 44d566e; parents 8b71be1 and 48d434f |
| A4 after takeover | 44d566e plus the takeover commit |
| Stage 0 | NOT ACCEPTED |
| D-006 | Proposed |

PR #1 is open and draft. PR #2 is open and draft. PR #3 is open and non-draft. PR #4 is open and
targets the A4 branch, not main.

## 3. Recovered A1 evidence

A1's raw log is copied verbatim to evidence/a1-stage0/a1-raw-checks-2026-11-23-r2.txt.

| Field | Value |
| --- | --- |
| Source | a1/stage0-review at 2693a34 |
| Source path | docs/evidence/a1-raw-checks-2026-11-23-r2.txt |
| Source blob | 24ce6a9a1ae3fd0284ddcce36592f3fe05b2a0e3 |
| Copy SHA-256 | 457235e5aa1c5a8c23a6efcd4eee6dc652c8264263da12d624c84d4d5cbd467e |

A4 re-ran the A1 header and ABI conformance script in an A1 worktree at 2693a34. The full output,
tool versions and exit codes are in evidence/a1-stage0/a4-verification-2026-11-23.txt.

Result: RESULT: PASS, exit 0. This covers standalone C11 compilation of the ten A1 headers, the
umbrella header in C11 and C++17, the ABI static assertions, device neutrality, freestanding headers
and the include/ozero namespace. It is static header/ABI evidence, not kernel boot or runtime
evidence. XB-016 is Closed.

## 4. A2 reconciliation and IF-009

IF-009 v1.0 remains Signed by A1, A3 and A2. XB-003 is Closed. The current A2 pair is code f6ed1d8
and evidence d83cfbe; the signed contract is d02598a; the fixture manifest blob is f505cb4d... and
is unchanged from d02598a through f6ed1d8.

A2's branch ledger at d83cfbe still names the older pair 6d305b8 / 64b3458. A4 did not rewrite A2's
branch. The consolidated reconciliation is docs/architecture/a2-review-ledger-reconciled.md. It
records the current pair, the unchanged manifest and all three signatures. XB-013 is Closed in the
A4 candidate.

IF-009 v1.1 is not signed and not implemented. A1 accepted v1.0 with two explicit gaps: truncated
PE headers and a missing DLL name in an import descriptor. The disposition is
docs/architecture/if-009-v1.1-disposition.md. It requires six negative cases:

1. truncated DOS header
2. truncated NT headers
3. truncated optional header
4. truncated section table
5. import descriptor with a zero Name RVA
6. import descriptor with an out-of-bounds Name RVA

Each case must fail for the named reason. The evidence level stays L0. A1 and A3 must re-sign v1.1;
v1.0 signatures do not carry forward. XB-021 is Open.

## 5. A3 integration

A4 reviewed PR #4 at 48d434f and integrated it into the A4 candidate as merge commit 44d566e. The
merge adds 56 files: 44 under a3/ and 12 under include/ozero/. No A4 control-plane file changed.

| Check | Result |
| --- | --- |
| Files added | 56 |
| a3/ files | 44 |
| include/ozero/ files | 12 |
| A1 root headers | 10 of 10 byte-identical to A1 74b1471 |
| IF-008 headers | platform.h and block.h match a3/include/ozero/ |
| A4 control-plane changes | none outside a3/ and include/ozero/ |
| PR #4 on GitHub | still open; not merged to main |
| XB-020 | Closed |

The file-level ownership map is docs/architecture/a3-integration-map.md. The evidence is
evidence/stage0-review/a3-integration-verification-2026-11-23.md and
evidence/stage0-review/a3-integration-checks-2026-11-23.log.

Integration is not a hardware result. A3 remains static-only. No AHCI probe, DMA transfer, GPU
execution or real-hardware test exists. XB-005 stays Open. D-007 and D-021 remain Proposed.

## 6. Verification

Commands run on the integrated tree:

    $ make -C a3 check
    Ran 19 tests in 0.004s
    OK
    exit=0

    $ make -C a3 check-a1-headers A1_INCLUDE=include
    cc -std=c11 -Wall -Wextra -Werror -Iinclude -Iinclude -fsyntax-only tests/header_smoke.c tests/a1_header_smoke.c
    exit=0

    $ make clean && make ci
    ...
    test: ok (5 markers in order)
    ci: ok
    exit=0

The image is 512 bytes with SHA-256
37ad8227720cd7f40b1393a8ad554a78f76d08b09ff97be68b1d2579f5e15319. QEMU 10.0.13 runs with TCG,
no network, no display and no default devices.

The CI job named build-and-boot runs the MBR fixture/harness smoke test. It is not UEFI, Limine or
Ozero kernel boot. The 512-byte MBR fixture is not the Ozero kernel.

The final takeover check is in evidence/stage0-review/final-takeover-checks-2026-11-23.log:
check-ci-safety: ok, ci: ok, 5 markers in order, exit 0. GitHub Actions build-and-boot passed at
bc3f566 in 4m55s (run 45195798408); evidence/stage0-review/ci-run-bc3f566.txt.

## 7. Blocker snapshot

4 Open, 16 Closed, 1 Signed.

| State | Rows |
| --- | --- |
| Open | XB-004 boot protocol / D-005; XB-005 A3 hardware evidence; XB-012 branch protection / D-031; XB-021 IF-009 v1.1 gaps |
| Signed | XB-002 A1/A2 contracts; D-011 Proposed; no ABI freeze |
| Closed this round | XB-013 A2 ledger reconciliation; XB-016 A1 raw evidence; XB-020 A3 integration |

The live register is docs/process/cross-agent-blockers.md.

## 8. Decisions and gates

D-008, D-009 and D-010 are Approved. D-001 through D-007 and D-011 through D-031 are Proposed.
D-006 is explicitly not approved. Stage 0 remains NOT ACCEPTED.

| Gate | State |
| --- | --- |
| D-006 Stage 0 acceptance | Proposed; not accepted |
| D-007 A3 DMA/platform interface | Proposed; no AHCI work authorized |
| D-021 IOMMU policy | Proposed; no DMA work authorized |
| D-031 main branch protection | Proposed; main is unprotected |
| Windows PE | Static L0 only; no load, import resolution or execution claim |
| Boot | MBR fixture only; not UEFI, Limine or kernel boot |
| GPU / hardware | No device execution, residency, submission or completion-fence evidence |

## 9. Not tested

UEFI, Limine loading an Ozero kernel, Ozero kernel entry, real hardware, PE loading or execution,
Windows compatibility, A3 driver code, AHCI probe, DMA transfer, GPU execution, A1/A2 integration,
IF-009 v1.1 negative cases, and branch-protection enforcement.

## 10. Next measurable milestone

The next milestone is Director disposition of D-006, D-007, D-021 and D-031, followed by either:

1. A1/A2 integration into the A4 candidate with the same file-level proof used for A3; or
2. implementation of the six IF-009 v1.1 negative cases with L0 negative reports and A1/A3 re-sign.

Completion test: the A4 tree contains the agreed change, the named checks pass with raw output, and
the relevant blocker row moves only when its evidence condition is met.

## 11. Commit record

| Commit | Change |
| --- | --- |
| 8b71be1 | Recover A1 evidence and reconcile IF-009 v1.1 gaps |
| 44d566e | Merge A3 PR #4 into the A4 candidate |
| takeover commit | Record sole-agent takeover, A3 integration proof, A2 reconciliation and master brief |

main remains 147cc86. No merge to main was performed.
