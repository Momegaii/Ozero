# A3 response to the 23 November 2026 proceed brief

| Field | Value |
|---|---|
| Brief | `Ozero_A3_Proceed_Brief_2026-11-23.pdf` |
| Branch | `a3/stage0-review-response` |
| A3 package | `a3/` |
| IF-008 accepted version | 1.1 at `b9a69d8` |
| A1 decision | **ACCEPT** at `4716e09` |
| A4 control-plane base | `3ae7aca` |
| A2 IF-009 v1.0 | `d02598a`; evidence head `3f78477` |
| A3 acceptance record | [`interfaces/IF-008-v1.1-acceptance.md`](interfaces/IF-008-v1.1-acceptance.md) |
| AHCI probe | **Blocked** |
| Pull request | **None** |
| Runtime evidence | **None**; static and compile-time checks only |

This response follows A3-1 through A3-6 of the proceed brief. It does not claim
Director approval, Stage 0 acceptance, merge approval or runtime functionality.

## A3-1 — Accept A1's IF-008 §6 conditions

A3 explicitly accepts all eight conditions in A1's
`docs/if008-v1.1-review.md` §6 against the exact interface commit
`b9a69d8`:

| # | Condition | A3 disposition |
|---|---|---|
| 1 | `dma_constraints` supplies a contiguous low-bit `addr_mask`, byte `max_segment` and truthful `coherent` | Accepted |
| 2 | `dma_quiesce` stops new DMA, waits for in-flight requests, is idempotent and does not free A1 buffers | Accepted |
| 3 | `reset` runs after quiesce and completion, does not free A1 buffers or unmap BARs, and A3 reinitialises | Accepted |
| 4 | `io_submit` never completes before `o0_io_submit` returns | Accepted |
| 5 | `io_cancel` is best-effort; `O0_EBUSY` still permits completion | Accepted |
| 6 | BAR access uses `o0_device_map_bar` as UC/NX, never the HHDM | Accepted |
| 7 | Transfers above `max_segment` split into multiple buffers and requests, with no SG pointer | Accepted |
| 8 | Stage 1 DMA is trusted ring-0 only; no userspace or untrusted DMA before an IOMMU domain | Accepted for Stage 1 |

This is A3's acceptance of A1's conditions. It is **not** Director D-007
approval, merge approval or runtime validation. The acceptance record is
[`interfaces/IF-008-v1.1-acceptance.md`](interfaces/IF-008-v1.1-acceptance.md).

## A3-2 — Correct stale IF-008 and review references

Updated in this response:

- `a3/README.md`, `a3/AGENTS.md` and `a3/EVIDENCE.md`: A1 **ACCEPT** at
  `b9a69d8`; Director D-007 pending.
- `a3/docs/04-driver-interface-draft.md`, `a3/docs/06-stage0-review-response.md`
  and `a3/docs/07-a3-action-brief-response.md`: A1 decision and A4 basis.
- `a3/docs/interfaces/IF-008-dma-platform.md`: status and D-007/XB-001 row.
- `a3/docs/interfaces/IF-008-v1.1-a1-review-packet.md`: A1 decision recorded.
- `a3/docs/decisions/a3-decision-requests.md`: D-007/D-021 status and A4 refresh.
- A4 basis references now use `3ae7aca`; D-009 is approved and XB-007 is closed.

A4's `3ae7aca` still lists IF-008 as `A1 final approval pending` and XB-001 as
unreviewed. A3 cannot edit A4's register; the refresh request is in
[`08-control-plane-integration.md`](08-control-plane-integration.md).

## A3-3 — Resolve shared-root conflicts

The A3 package is now under `a3/`. The branch merges the A4 control-plane base
`3ae7aca` and keeps A4's root `README.md`, `AGENTS.md`, `Makefile`, `docs/`,
`tests/`, `scripts/`, `harness/`, `toolchain/`, `evidence/`, `.github/` and
`CODEOWNERS`. A3 no longer overwrites the shared root.

The integration record is [`08-control-plane-integration.md`](08-control-plane-integration.md).
It requests that A4 close XB-017 and XB-018 after review.

## A3-4 — Respect header ownership

A1 owns `include/ozero/`. A3 owns only `platform.h` and `block.h` there, and A1
reviewed and ACCEPTed those exact paths at `b9a69d8`. A3 does not modify A1's
ten public headers. `a3/include/ozero/` is a package snapshot, not a second
authoritative surface.

A4 still needs file-level `CODEOWNERS` entries for `platform.h` and `block.h`;
A3 has requested them in the integration record and does not edit A4's file.

## A3-5 — Confirm IF-009 v1.0

A3 reviewed A2 IF-009 v1.0 at `d02598a`, with evidence head `3f78477`, and
answers both questions:

- **IF9-A3-1:** IF-009 requires no platform, DMA or block interface from A3.
- **IF9-A3-2:** A3 will not cite an IF-009 pass as a driver, AHCI or graphics
  result.

A3 **accepts IF-009 v1.0 for the hardware boundary**. A1's kernel-interface
review and A2's signature recording remain open. See
[`interfaces/IF-009-a3-review.md`](interfaces/IF-009-a3-review.md).

## A3-6 — Keep AHCI blocked

No AHCI probe, NVMe, USB, GPU, physical-hardware or DMA transfer test has run.
`make -C a3 check` and `make -C a3 check-a1-headers` are static and
compile-time checks. The AHCI probe remains blocked until:

1. A3 acceptance of A1's conditions is recorded (this response).
2. The Director records D-007 approval.
3. The Director records D-021 approval.

## Definition-of-done status

| Criterion | Status |
|---|---|
| Written A3 acceptance at the exact IF-008 hash | **Done**: `b9a69d8`; A1 `4716e09` |
| Stale documents and decision references corrected | **Done**; A4 `3ae7aca` basis |
| Shared-root/header ownership conflicts resolved in a reviewable commit | **Done** for shared root and A3 tree; A4 `CODEOWNERS` refresh requested |
| IF-009 v1.0 disposition published | **Done for A3** at A2 `d02598a`; A1/A2 still open |
| AHCI remains gated until Director decisions | **Held**; no probe work started |

## Verification

```sh
make -C a3 check
# matrix PASS; deliverables PASS; 19 tests PASS; header smoke PASS

make -C a3 check-a1-headers A1_INCLUDE=/tmp/a1include
# PASS against the external A1 include tree
```

## Handoff

A3 publishes these corrections on `a3/stage0-review-response`. The branch head
is this commit. There is no pull request. A1 and A4 should review the exact
commit hash and the records above; A4 should refresh IF-008, XB-001, XB-005,
XB-011, XB-017 and XB-018.
