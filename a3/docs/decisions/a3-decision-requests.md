# A3 decision requests — D-007, D-021, D-009

| Field | Value |
|---|---|
| Owner | A3 |
| Purpose | Decision-ready wording for the A3 action brief (23 November 2026) |
| Canonical register | A4 `docs/decisions/decision-log.md` @ `b4232a0` |
| A3 status | Input only; A3 does not approve global decisions |
| Gate | A1 IF-008 v1.1 ACCEPT recorded; Director D-006/D-007/D-021/D-031 before AHCI |

A4 owns the canonical IDs and statuses. This document gives the Director concise
text and exact impact; it does not create a parallel register.

## D-007 — Approve the A3 DMA/platform interface and A1 sign-off gate

**Canonical A4 wording:** "Approve the A3 DMA/platform interface and the required
A1 sign-off gate."

**A3 decision-ready wording:**

> Approve IF-008 v1.1, published by A3 at commit `b9a69d8`, as the Stage 1
> DMA/platform contract. A1 has reviewed that exact `platform.h` and `block.h`
> and recorded **ACCEPT** in decision `85df005` at review head `74b1471`. A3 may
> start the AHCI probe only after the Director records D-007 and D-021.

**Status:** Proposed in A4 `b4232a0`. A1 recorded **ACCEPT** at `b9a69d8` (`85df005`, review head `74b1471`); Director approval pending. Owner A1 + A3; approver Project Director; needed before A3 DMA work.

**Impact if approved:**
- The A1 review gate becomes the formal Stage 1 contract gate.
- The A1 gate is met at `b9a69d8`; Director approval of D-007 completes the
  decision. Director approval of D-021 is still required before the probe.
- No runtime support claim changes. All storage rows remain `not-started`.

**Impact if not approved:**
- AHCI probe and real DMA work remain blocked.
- A3 continues documentation and static contract checks only.
- No code rollback is required; the headers are already published for review.

**A3 position:** Accept. A3 has published the exact review target and the §5
checklist in [`IF-008-v1.1-a1-review-packet.md`](../interfaces/IF-008-v1.1-a1-review-packet.md).

## D-021 — IOMMU policy: trusted ring-0 only in Stage 1

**Canonical A4 wording:** "IOMMU policy: trusted ring-0 only in Stage 1; blocker
recorded."

**A3 decision-ready wording:**

> Approve the Stage 1 restriction that DMA is available only to trusted ring-0
> drivers, with no IOMMU domain, no IOVA allocator and no translated DMA address
> type. Any userspace or untrusted DMA requires a new interface version, a new
> global decision and Director approval.

**Status:** Proposed in A4 `b4232a0`. Owner A1; approver Project Director; needed
before DMA work.

**Impact if approved:**
- A3 keeps `o0_dma_buffer.phys` as an untranslated `o0_paddr_t`.
- A3 does not program an IOMMU or expose DMA to userspace.
- The limitation is recorded rather than implied; it is not an isolation claim.

**Impact if not approved:**
- A3 cannot claim a stable Stage 1 DMA policy; IF-008 v1.1 remains conditional.
- The AHCI probe remains blocked because the DMA safety model is unresolved.
- A future IOMMU design would need a new IF-008 major version.

**A3 position:** Accept. A3's headers contain no IOMMU domain, IOVA or translated
address type.

## D-009 — Public header namespace (already approved)

**Canonical A4 status:** **Approved** at `b4232a0`. The canonical namespace is
`include/ozero/` only; `include/o0/` is removed; symbols keep the `o0_` prefix;
A2/A3 include `<ozero/...>`. A1's `03f8883` implements it. XB-007 is **Closed**.

**A3 alignment:** A3's `b9a69d8` uses `include/ozero/` exclusively and vendors
A1's public-header snapshot. A3 does not request a new decision.

**Impact:** No open namespace conflict remains. A4 `b4232a0` records the
approved namespace and XB-007 closed.

## D-010 — Header-check ownership (already approved)

**Canonical A4 status:** **Approved** at `b4232a0`. A1 owns
`tools/check-headers.sh` and `tests/abi_static_asserts.c`; A4 owns CI
integration; A3 owns other `tools/` content. XB-008 is **Closed**.

**A3 alignment:** A3 does not add or modify A1's header-check files. A3's
`make check-a1-headers` is an A3-local verification target, not a replacement
for A1's checker.

**Impact:** No ownership conflict remains. A4 `b4232a0` has the approved
`CODEOWNERS` overrides, including the two A3 interface headers.

## A4 candidate integration gap

A4 `b4232a0` now records IF-008 and IF-009 correctly and closes XB-001,
XB-017, XB-018 and XB-019. However, the A4 candidate tree contains no `a3/`
directory and no root `include/ozero/` directory. A4 PR #3's diff has **0**
files in both paths. XB-017's closure is therefore documentation-only until
A4 merges the A3 package.

A3 opened PR [#4](https://github.com/Momegaii/Ozero/pull/4) into
`a4/stage0-review-followup`. A4 should:

1. Review and merge PR #4.
2. Verify A4 PR #3 then contains `a3/` paths and the exact root
   `include/ozero/` headers.
3. Keep XB-005 Open: no device-specific or hardware test exists.
4. Keep D-006, D-007, D-021 and D-031 Proposed; do not merge to `main`.

## References

- A4 decision register:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/decisions/decision-log.md>
- A4 cross-agent blockers:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/process/cross-agent-blockers.md>
- A1 conditions:
  <https://github.com/Momegaii/Ozero/blob/a1/stage0-review/docs/a3-dma-contract.md>
- A3 review packet:
  [`IF-008-v1.1-a1-review-packet.md`](../interfaces/IF-008-v1.1-a1-review-packet.md)
