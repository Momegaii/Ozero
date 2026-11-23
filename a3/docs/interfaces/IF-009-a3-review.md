# IF-009 — A3 review of the PE fixture contract

| Field | Value |
|---|---|
| Interface | IF-009, PE fixture and static-analysis contract |
| Version | 1.0 |
| Owner | A2 |
| A3 role | Hardware-boundary reviewer; not owner and not approver |
| A2 code revision reviewed | `d02598a` |
| A2 evidence head | `64b3458` |
| A2 contract | `a2/docs/13-if-009-pe-fixture-contract.md` |
| A4 registry reviewed | `b4232a0` |
| A3 decision | **ACCEPT IF-009 v1.0 for the A3 hardware boundary** |
| A1 decision | **ACCEPT (A1 side) with conditions** at `a17d126`; A1 owns the kernel-interface side |
| XB-003 | Open until A2 records both signatures at `6d305b8` / `64b3458` |

A3 does not own PE parsing, mapping or loader policy. This review answers the
two A3 questions in IF-009 v1.0 and checks that hardware/platform types do not
leak into the Windows-facing loader contract.

## 1. Answers to IF9-A3-1 and IF9-A3-2

**IF9-A3-1 — Does IF-009 require any platform, DMA or block interface from A3?**

**No.** IF-009 fixtures are regular files read by user-mode tools. The contract
does not require an A3 block device, AHCI, NVMe, USB, GPU, IRQ or DMA interface.
If a future loader reads fixture bytes from storage, it uses A1/A2 file objects
and A3's IF-008 block path only as the delivery mechanism, not as part of the
IF-009 contract.

**IF9-A3-2 — Will A3 cite an IF-009 pass as a driver, AHCI or graphics result?**

**No.** An IF-009 pass is L0 static PE evidence. A3 will not cite it as an AHCI
probe, DMA transfer, driver test, GPU test or runtime compatibility result.

## 2. Boundary checks against A2 `d02598a`

| Check | Result | Evidence |
|---|---|---|
| Fixture sources contain no `o0_*`/`ozero` hardware types | Pass | A2's five fixture sources; no hardware type matches |
| Fixture sources use Windows APIs, not A3 block/device APIs | Pass | `fileproc.c` uses `VirtualAlloc`/`VirtualFree`; no `o0_block_*` or `o0_device_*` |
| Manifest paths are relative and hashes are pinned | Pass | A2 v1.0 states paths are relative to `a2/`; manifest records source/output SHA-256 |
| Contract states platform neutrality | Pass | IF-009 v1.0 says fixtures and manifest are platform-neutral and contain no A3 device type |
| A1 kernel primitives remain opaque | Pass for A3 boundary | A2 lists capabilities; A1 internals remain A1's design |
| A4 registry lists A1 and A3 as reviewers/consumers | Pass at `b4232a0` | A4 registry records IF-009 v1.0 with A1 ACCEPT and A3 hardware-boundary acceptance; A2 signature pending |

A3's earlier comments from `807c447` were applied in A2 v1.0: relative manifest
paths and the platform-neutrality sentence are present.

## 3. What A3 does not review

- PE header parsing, relocations, imports, TLS, `.pdata`, unwind metadata, CFG
  or CLR behaviour.
- Loader runtime behaviour. A2's package is static-only.
- Any claim that a fixture runs under Ozero or on Windows.

## 4. A3 decision and conditions

A3 **accepts IF-009 v1.0 at A2 `d02598a`** for the hardware boundary, with no
blocking comments.

Non-blocking requests:

1. A2 should record A3's signature in IF-009 with version `1.0`, A2 code revision
   `d02598a`, evidence head `64b3458`, and the A3 review commit.
2. A4 has added A1 and A3 as reviewers/consumers at `b4232a0`; XB-003 stays
   Open until A2 records both signatures at `6d305b8` / `64b3458`.
3. A2 must not treat this signature as A1's kernel-interface approval.

## 5. Status

A3's hardware-boundary review is complete. A3 accepts IF-009 v1.0 at `d02598a`.
A1 recorded **ACCEPT (A1 side) with conditions** at `a17d126`; A2's recorded
signature remains open at `6d305b8` / `64b3458`. XB-003 stays Open. Stage 0
remains not accepted.

## 6. References

- A2 IF-009 v1.0:
  <https://github.com/Momegaii/Ozero/blob/d02598a/a2/docs/13-if-009-pe-fixture-contract.md>
- A2 fixture manifest:
  <https://github.com/Momegaii/Ozero/blob/d02598a/a2/fixtures/build-manifest.json>
- A2 evidence:
  <https://github.com/Momegaii/Ozero/tree/64b3458/a2/docs/evidence>
- A4 interface registry:
  <https://github.com/Momegaii/Ozero/blob/b4232a0/docs/architecture/interfaces.md>
- A4 blockers:
  <https://github.com/Momegaii/Ozero/blob/b4232a0/docs/process/cross-agent-blockers.md>
- A1 IF-009 review:
  <https://github.com/Momegaii/Ozero/blob/a17d126/docs/if009-review.md>
