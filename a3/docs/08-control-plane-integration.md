# A3 control-plane integration

| Field | Value |
|---|---|
| Purpose | Record the A3 response to XB-017/XB-018 and the A4 control-plane merge |
| A3 package tree | `a3/` |
| A4 base merged | `origin/a4/stage0-review-followup` @ `3ae7aca` |
| A3 package move commit | `a16a99b` |
| Merge commit | `f0be3c5` |
| Shared root README/AGENTS | A4 versions, not A3 |
| A3 interface headers | `include/ozero/platform.h`, `include/ozero/block.h` |
| A1 IF-008 decision | **ACCEPT** at `b9a69d8`; A1 `4716e09` |
| AHCI | **Blocked** |

This document records the shared-root and ownership cleanup requested by A4's
XB-017 and XB-018.

## 1. Shared root

Before this change A3 was based on `main` and replaced the shared root
`README.md` and `AGENTS.md`. The branch now:

- keeps A4's root `README.md`, `AGENTS.md`, `Makefile`, `docs/`, `tests/`,
  `scripts/`, `harness/`, `toolchain/`, `evidence/`, `.github/` and `CODEOWNERS`;
- keeps the A3 package in its own `a3/` tree, including `a3/README.md`,
  `a3/AGENTS.md`, `a3/EVIDENCE.md`, `a3/docs/`, `a3/matrix/`, `a3/tests/`,
  `a3/tools/`, `a3/Makefile` and `a3/include/ozero/`;
- does not overwrite the shared root README or AGENTS.

The A3 package is self-contained and runs from its subtree:

```sh
make -C a3 check
make -C a3 check-a1-headers A1_INCLUDE=/tmp/a1include
```

## 2. Shared header surface

`include/ozero/platform.h` and `include/ozero/block.h` remain at the shared
root because A1 reviewed and accepted those exact paths at `b9a69d8`.
`a3/include/ozero/` is a package snapshot, not a second authoritative surface.

A1's ten public headers are authoritative. A3 must not modify them. On merge,
A1's headers at `30ee4f0` or later win.

## 3. A4 actions still required

A4 owns the control plane. A3 requests:

1. Update `docs/architecture/interfaces.md` IF-008 from `A1 final approval
   pending` to `1.1; A1 accepted at b9a69d8; director D-007 pending`.
2. Update XB-001 evidence to the A1 decision commit `4716e09` and
   `docs/if008-v1.1-review.md`.
3. Add file-level `CODEOWNERS` entries for
   `include/ozero/platform.h` and `include/ozero/block.h` with A1 review and A3
   ownership, per A1's `docs/if008-v1.1-review.md` §5.
4. Refresh XB-005 and XB-011 evidence to the current A3 branch head; both are
   already pushed, but A4's audit text still cites `15cd9ce`.
5. Close XB-017 and XB-018 after reviewing this merge and the corrected A3
   basis.

A3 does not edit A4's decision register, blockers, interface registry or
`CODEOWNERS`.

## 4. Status

- Shared-root overwrite: **resolved** in this merge.
- A3 content isolation: **resolved** under `a3/`.
- A1 header authority: **preserved**; A1 accepted `platform.h` and `block.h`.
- A4 `CODEOWNERS` entries: **requested**, not yet applied.
- XB-017: **ready for A4 review**.
- XB-018: **ready for A4 review**; A3 basis is `3ae7aca` and D-009 is approved.
- AHCI: **blocked** pending Director D-007/D-021.

## 5. References

- A4 interface registry:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/architecture/interfaces.md>
- A4 blockers:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/process/cross-agent-blockers.md>
- A4 ownership map:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/architecture/ownership.md>
- A1 IF-008 decision:
  <https://github.com/Momegaii/Ozero/blob/a1/stage0-review/docs/if008-v1.1-review.md>
- A3 IF-008 acceptance:
  [`interfaces/IF-008-v1.1-acceptance.md`](interfaces/IF-008-v1.1-acceptance.md)
