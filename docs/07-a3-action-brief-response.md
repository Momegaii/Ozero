# A3 response to the 23 November 2026 action brief

| Field | Value |
|---|---|
| Brief | `Ozero_A3_Action_Brief_2026-11-23_v2.pdf` |
| Branch | `a3/stage0-review-response` |
| Published code commit | `b9a69d8` |
| Published documentation commit | this commit (HEAD) |
| Interface | IF-008 v1.1 |
| AHCI probe | **Blocked** |
| Runtime evidence | **None**; static contract checks only |

This response follows the brief's required actions A3-1 through A3-6. It does not
claim Stage 0 acceptance, A1 sign-off, merge approval or runtime functionality.

## A3-1 — Hold for A1 review, prepare a fast response

A1's exact review target is:

```sh
git show b9a69d8:include/ozero/platform.h
git show b9a69d8:include/ozero/block.h
```

A3 published a point-by-point response to A1's ten §5 conditions in
[`interfaces/IF-008-v1.1-a1-review-packet.md`](interfaces/IF-008-v1.1-a1-review-packet.md).
The packet requests an explicit **ACCEPT** or **CHANGE REQUIRED** on that exact
commit. A3 will answer every CHANGE REQUIRED item in a new commit with a new
hash.

A1 has not yet reviewed `b9a69d8`. Its `docs/a3-dma-contract.md` still describes
A3 as unpublished; that statement is stale.

## A3-2 — Maintain IF-008 contract invariants

The published contract keeps these invariants:

- `struct o0_bio` embeds one `struct o0_io_request`.
- There is no scatter-gather pointer in the generic request.
- Transfers above `max_segment` split into multiple buffers and requests.
- A3 implements A1's `struct o0_device_ops`.
- CPU `void *` addresses stay distinct from device-visible `o0_paddr_t`.
- `io_submit` completion must not run before the call returns.
- Reset invalidates requests and device state but does not free kernel DMA
  buffers or unmap BARs.
- No BAR mapping is derived from the HHDM.

These are contract obligations, not runtime evidence. The static checks are
listed in [`EVIDENCE.md`](../EVIDENCE.md).

## A3-3 — Keep the AHCI probe blocked

A3 has not started the AHCI probe. `make check`, `make check-a1-headers` and the
contract tests prove only that the modelled interface is internally consistent
and compiles against A1's snapshot. They do not prove AHCI, DMA, device or
hardware behaviour.

The gate opens only after:

1. A1 records **IF-008 v1.1 at `b9a69d8`: ACCEPT**.
2. The Director records D-007 and D-021.
3. A3 records the new approval evidence in `EVIDENCE.md`.

## A3-4 — Decision-ready wording for D-007, D-021 and D-009

A3 published concise decision text and exact impact in
[`decisions/a3-decision-requests.md`](decisions/a3-decision-requests.md).

- **D-007:** Approve IF-008 v1.1 and require A1 sign-off on the exact published
  headers. Without it, AHCI remains blocked.
- **D-021:** Approve trusted ring-0-only DMA with no IOMMU in Stage 1. Without it,
  the DMA safety model remains unresolved and AHCI remains blocked.
- **D-009:** Already **Approved** in A4 `245007d`; canonical namespace is
  `include/ozero/` only, and XB-007 is **Closed**. A3 needs no new decision.
- **D-010:** Already **Approved** in A4 `245007d`; XB-008 is **Closed**.

A4's blocker rows XB-001, XB-005 and XB-011 still describe A3 as unpublished.
A3 requested an evidence refresh in the decision document; A3 cannot edit A4's
register directly.

## A3-5 — IF-009 PE fixture contract review

A3 reviewed A2's fixture contract at `agent/a2-stage0` @ `807c447` for hardware
platform-type leakage:

- No `o0_*` or `ozero/...` hardware types appear in A2's fixture sources or
  manifest.
- `fileproc.c` uses Windows `VirtualAlloc`/`VirtualFree`, not A3 block or device
  APIs.
- A2's loader requirements describe capabilities, not A1 internal types.

A3's review is in [`interfaces/IF-009-a3-review.md`](interfaces/IF-009-a3-review.md).
A3 recommends **ACCEPT WITH COMMENTS** for A1/A2/A4:

1. IF-009 should state explicitly that fixtures and manifests are
   platform-neutral and contain no A3 hardware types.
2. A2 should normalize absolute `/workspace/project/Ozero/...` manifest paths or
   document the fixed build root.
3. A4 should keep XB-003 Open until A1's review and A2's revision are recorded.

## A3-6 — Hardware roadmap distinctions

The roadmap keeps these as distinct test cases and driver families:

| Product case | Driver family | Status |
|---|---|---|
| HDD / SATA | `ahci` | not-started |
| SSD / SATA | `ahci` | not-started |
| SSD / NVMe | `nvme` | not-started |
| USB stick / HDD | `usb-bot` | not-started |
| USB SSD / UASP | `usb-uas` | not-started |
| Legacy ATA | `ata-legacy` | not-started |
| Virtio block (test transport) | `virtio-blk` | not-started |
| SATA port multiplier | `ahci-pm` | deferred |
| NVMe ZNS | `nvme-zns` | deferred |

HDD/SATA and SSD/SATA share the AHCI transport but remain separate product test
cases. NVMe, USB BOT, UAS and legacy ATA stay separate paths. ZNS and SATA port
multiplier remain deferred unless a separate requirement is approved.

## Definition-of-done status

| Criterion | Status |
|---|---|
| A1 IF-008 v1.1 decision closed | **Pending**; review packet published, A1 has not recorded ACCEPT/CHANGE REQUIRED |
| Required corrections committed and pushed | **N/A** until A1 issues a decision |
| D-007 / D-021 disposition recorded | **Pending**; decision-ready text published, Director has not recorded approval |
| D-009 disposition recorded | **Done**; A4 `245007d` approved `include/ozero/` only |
| IF-009 reviewed | **Done for A3 hardware boundary**; A1/A4 review still pending |
| AHCI gated until explicit approval | **Held**; no AHCI probe work started |

## Evidence and limits

```sh
make check
# matrix PASS; deliverables PASS; 19 tests PASS; header smoke PASS

make check-a1-headers A1_INCLUDE=/tmp/a1include
# PASS against A1's external include tree
```

No AHCI, NVMe, USB, GPU, physical-hardware or DMA runtime test has been run.
Passing contract tests verifies the modelled interface only.
