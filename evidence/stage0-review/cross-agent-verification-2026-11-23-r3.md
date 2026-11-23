# Cross-agent verification — round 3, 2026-11-23

This is the post-A3-publication snapshot. It replaces the refs in round 2; the round-2 file stays
as the earlier snapshot. Raw command output is in
[`cross-agent-commands-2026-11-23-r3.log`](cross-agent-commands-2026-11-23-r3.log).

## Refs

| Ref | Commit | State |
| --- | --- | --- |
| `origin/main` | `147cc86` | unchanged |
| `origin/a1/stage0-review` | `30ee4f0` | PR #1, open, draft; 15 commits, 27 files, +3529 |
| `origin/agent/a2-stage0` | `807c447` | PR #2, open, draft; 4 commits, 48 files, +9967; evidence at `50b7853` |
| `origin/a3/stage0-review-response` | `15cd9ce` | published; no PR; 6 commits, 33 files, +5560/−2; IF-008 v1.1 at `b9a69d8` |
| `origin/a4/stage0-review-followup` | `245007d` plus this review commit | PR #3, open, non-draft; 19 commits at `245007d`, 82 files, +4383/−2 |

All four agent branches are pushed. The Director confirmed the pushes. A3 has no pull request, and
this note does not invent a PR number.

## Checks run

| Agent | Command | Result | Raw log |
| --- | --- | --- | --- |
| A1 | `tools/check-headers.sh` | PASS; standalone headers, umbrella C/C++, ABI asserts, device neutrality, freestanding headers, namespace | `a1-check-headers.log` |
| A1 | `cc -std=c11 -Wall -Wextra -Werror -Iinclude -fsyntax-only tests/abi_static_asserts.c` | exit 0 | `a1-abi-check.log` |
| A2 | `make check-static` | exit 0; 61 structure checks, 0 failed; lint and docs checks ran | `a2-static-check.log` |
| A3 | `make check` | exit 0; matrix 10 rows/9 families, 19 tests, header smoke, inventory syntax | `a3-check.log` |
| A3 | `make check-a1-headers A1_INCLUDE=/tmp/a1-check/include` | exit 0 against A1 `30ee4f0` headers | `a3-a1-header-check.log` |
| A4 | `make ci` at `245007d` | exit 0; 5 markers in order, 223 ms; GitHub `build-and-boot` success | `ci.log`, PR #3 checks |

A3's vendored `include/ozero/` snapshot is byte-identical to A1 `30ee4f0` for `boot.h`, `cpu.h`,
`driver.h`, `ipc.h`, `irq.h`, `mm.h`, `o0.h`, `status.h`, `thread.h` and `types.h`. A3's
`platform.h` and `block.h` are A3-owned additions. Merge order still matters: A1 first, then A3, so
the authoritative headers are not replaced by a stale snapshot.

## A1 review dependencies

- A1's `docs/a3-dma-contract.md` and `docs/interface-draft.md` §11 record a conditional sign-off:
  the generic DMA/MMIO surface is signed with one IOMMU blocker and five conditions, but final
  IF-008 approval waits for A3's published `platform.h` and `block.h`.
- A3 published IF-008 v1.1 at `b9a69d8` (branch head `15cd9ce`). A1 has not reviewed that exact
  version. **XB-001 stays Open.**
- A1's branch contains no `A1-REV-01`…`A1-REV-12` answers. A2's request is in
  `a2/docs/12-a1-contract-review.md`; A1 reviewed A2 at `f324b28`, before A2 moved to `50b7853`.
  **XB-002 and XB-014 stay Open.**
- A1's A3 review text still says A3's headers are "not published". That statement was true at
  `30ee4f0` but is stale after `b9a69d8`. A1 must update its review record when it reviews v1.1.

## A2 provenance

| Item | Evidence |
| --- | --- |
| Response header | `a2/docs/11-stage0-review-response.md` still says code revision `8bac650` at lines 4, 34, 37, 50 and 78 |
| Evidence headers | All nine `a2/docs/evidence/*.txt` headers say `# git: 50b7853…` |
| Evidence-only commit | `git diff 50b7853..807c447 -- ':!a2/docs/evidence'` is empty; 9 evidence files, +42/−21 |
| Contract-review sources | `a2/docs/12-a1-contract-review.md` cites A1 `90b004f` and `include/o0/{ipc,thread,mm}.h` at lines 4, 205 and 206 |
| IF-009 status | `a2/docs/07-stage7-milestone.md:20` says XB-003 is Signed; `a2/docs/12-a1-contract-review.md:218` says XB-003 is Open |

**XB-013 stays Open** until A2 reconciles the response header with `50b7853` / `807c447` and
updates the A1 source references to `include/ozero/`. **XB-003 stays Open** until A1 and A3 record
an explicit IF-009 decision; an internal contradiction is not a sign-off.

## A3 publication and integration hygiene

A3's `make check` and A1-header checks pass. The package is static: no QEMU, no hardware, no AHCI
probe and no DMA transfer. Its matrix has no `complete` row.

Two integration issues remain:

1. **Stale review basis (XB-018).** `docs/06-stage0-review-response.md:6` says the A4 basis is
   `076ed82`; A4 is now `245007d`. The same file, `AGENTS.md` and IF-008 v1.1 say A4 records
   `include/o0/` as approved and that XB-007 must reopen. That is false: D-009 is **Approved** as
   `include/ozero/` at `210653c`, and XB-007 is Closed. A3 must update the basis and the D-009
   row before merge. A3's `EVIDENCE.md` also labels "A1 review response commit: this commit
   (HEAD)", which is ambiguous; the A1 review is at `30ee4f0`, not A3 HEAD.
2. **Shared-root and namespace hygiene (XB-017).** A3 is based on `main`, not on the A4 control
   plane. It adds an 84-line root `AGENTS.md` and replaces the 2-line root `README.md` with a
   245-line A3 package README. That would overwrite shared control-plane text. It also adds
   `include/ozero/platform.h` and `block.h` under A1-owned `include/ozero/` (ownership map and
   `CODEOWNERS`). A3 must rebase on the A4 base, keep its package under its own tree, and have A1
   approve the two A3 interface files or move them.

A3's IF-008 v1.1 itself is registered as `1.1` with owner A3. It is **Published**, not approved.
D-007 remains Proposed; the AHCI probe remains blocked.

## Governance controls

Raw output in [`github-settings.txt`](github-settings.txt) and
[`cross-agent-commands-2026-11-23-r3.log`](cross-agent-commands-2026-11-23-r3.log):

```text
$ curl .../branches/main
{'protected': False, 'protection.enabled': False,
 'required_status_checks': {'enforcement_level': 'off', 'contexts': [], 'checks': []}}

$ curl .../rulesets
[]

$ curl .../contents/CODEOWNERS?ref=main
404
```

`main` is not protected, has no rulesets and no base-branch `CODEOWNERS`. Required checks are not
enabled. The protection endpoints return 403 for both available tokens because neither has
Administration: read, but the public branch metadata is sufficient to show `protected: false`.
**D-031 / XB-012 remain Open.** A passing `build-and-boot` check does not prove enforcement.

## CI scope

`make ci` at `245007d` and the GitHub `build-and-boot` job exercise a 512-byte MBR fixture in QEMU
with five serial markers. They do not load Limine, do not enter an Ozero kernel, do not test UEFI,
do not execute PE images and do not touch real hardware. The A3 package's static checks are not a
driver test.

## Blocker effects

| ID | State after this refresh | Reason |
| --- | --- | --- |
| XB-001 | Open | A1 has not approved IF-008 v1.1 at `b9a69d8` |
| XB-002 | Open | A1 has not answered A1-REV-01…12 against `50b7853` / `807c447` |
| XB-003 | Open | A2 says Signed in `07`, Open in `12`; no explicit A1/A3 sign-off |
| XB-004 | Open | D-005 Limine/handoff still Proposed |
| XB-005 | Open | A3 static checks pass, but no QEMU/hardware driver evidence |
| XB-011 | Closed | A3 branch `15cd9ce` is published; audit now includes it; no PR exists |
| XB-012 | Open | `main` unprotected, no rulesets, no required checks, no base CODEOWNERS |
| XB-013 | Open | A2 response cites `8bac650`; evidence is `50b7853` / `807c447` |
| XB-014 | Open | A1 reviewed A2 at `f324b28`; A2 is at `807c447` |
| XB-016 | Open | A1 evidence is inline, not a raw log under `evidence/` |
| XB-017 | Open | A3 rewrites root `README.md` / `AGENTS.md` and adds `platform.h` / `block.h` under A1-owned `include/ozero/`; not rebased on A4 |
| XB-018 | Open | A3 review basis cites A4 `076ed82` and stale D-009 `include/o0/` |

XB-006, XB-007, XB-008, XB-009, XB-010, XB-011 and XB-015 are Closed. No blocker is Signed.

## Pull requests

| PR | Head | Base | Draft |
| --- | --- | --- | --- |
| #1 | `a1/stage0-review` at `30ee4f0` | `main` | yes |
| #2 | `agent/a2-stage0` at `807c447` | `main` | yes |
| #3 | `a4/stage0-review-followup` at `245007d` plus this review commit | `main` | no |
| — | A3 `a3/stage0-review-response` at `15cd9ce` | — | no PR |
