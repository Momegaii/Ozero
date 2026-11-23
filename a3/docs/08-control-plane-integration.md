# A3 control-plane integration

| Field | Value |
|---|---|
| Purpose | Record the A3 response to XB-017/XB-018 and the A4 candidate integration gap |
| A3 package tree | `a3/` |
| A4 base merged | `origin/a4/stage0-review-followup` @ `b4232a0` |
| A3 package move commit | `a16a99b` |
| Latest A4 merge commit | `fcce71e` |
| A3 integration PR | [#4](https://github.com/Momegaii/Ozero/pull/4) |
| Shared root README/AGENTS | A4 versions, not A3 |
| A3 interface headers | `include/ozero/platform.h`, `include/ozero/block.h` |
| A1 IF-008 decision | **ACCEPT** at `b9a69d8`; A1 `74b1471`, decision `85df005` |
| Stage 0 | **NOT ACCEPTED** |
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

A4 owns the control plane. At `b4232a0` it has already:

- recorded IF-008 as A1-accepted and A3-accepted;
- recorded IF-009 v1.0 with A1 and A3 acceptance;
- added file-level `CODEOWNERS` entries for `include/ozero/platform.h` and
  `include/ozero/block.h`;
- marked XB-001, XB-017, XB-018 and XB-019 closed.

The remaining integration action is:

1. Review and merge A3 PR #4 into `a4/stage0-review-followup`.
2. Verify A4 PR #3's diff then contains `a3/` paths and the exact root
   `include/ozero/` headers. At `b4232a0` it contains **0** `a3/` files and
   **0** root `include/ozero/` files, so XB-017's closure is documentation-only
   until the merge.
3. Keep XB-005 open: no device-specific or hardware test exists.
4. Do not merge any branch to `main`; Stage 0 remains not accepted.

A3 does not edit A4's decision register, blockers, interface registry or
`CODEOWNERS`.

## 4. Status

- Shared-root overwrite: **resolved** on the A3 branch.
- A3 content isolation: **resolved** under `a3/`.
- A1 header authority: **preserved**; A1 accepted `platform.h` and `block.h`.
- A4 `CODEOWNERS` entries: **present** at `b4232a0`.
- A4 IF-008/IF-009 registry: **updated** at `b4232a0`.
- A3 package in A4 candidate: **pending PR #4 merge**.
- A4 PR #3 diff: **0 `a3/` files, 0 root `include/ozero/` files**.
- XB-017: **A4 says Closed; tree integration pending**.
- XB-018: **Closed**; A3 basis is `b4232a0` and D-009 is approved.
- XB-005: **Open**; static checks only.
- AHCI: **blocked** pending Director D-007/D-021.

## 5. References

- A4 interface registry:
  <https://github.com/Momegaii/Ozero/blob/b4232a0/docs/architecture/interfaces.md>
- A4 blockers:
  <https://github.com/Momegaii/Ozero/blob/b4232a0/docs/process/cross-agent-blockers.md>
- A4 ownership map:
  <https://github.com/Momegaii/Ozero/blob/b4232a0/docs/architecture/ownership.md>
- A1 IF-008 decision:
  <https://github.com/Momegaii/Ozero/blob/74b1471/docs/if008-v1.1-review.md>
- A3 IF-008 acceptance:
  [`interfaces/IF-008-v1.1-acceptance.md`](interfaces/IF-008-v1.1-acceptance.md)
- A3 integration PR: <https://github.com/Momegaii/Ozero/pull/4>
- A4 PR #3: <https://github.com/Momegaii/Ozero/pull/3>
