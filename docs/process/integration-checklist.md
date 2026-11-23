# Integration checklist

| Field | Value |
| --- | --- |
| Status | **Proposed** (ADR-0002) |
| Owner | A4 |
| Approver | Project Director |

Use this checklist for every pull request that changes build, boot, a shared interface, or more than
one domain. Copy it into the PR description and fill in the evidence. An unchecked box is a no.

## Merge gate

- [ ] **Scope.** Task id, owner, one-line outcome, and the files changed are stated.
- [ ] **Ownership.** Every changed top-level directory has its `CODEOWNERS` reviewer approved.
- [ ] **Clean build.** `make verify-clean` or the CI link shows a build from a clean checkout.
- [ ] **Tests.** The named target passed and `build/qemu/result.json` is attached or linked. Do not
      write `make test` when the claim is kernel boot.
- [ ] **Warnings.** No new assembler, linker or compiler warnings.
- [ ] **Interfaces.** Any shared interface change is versioned and registered in
      `docs/architecture/interfaces.md`.
- [ ] **Cross-agent sign-off.** A1 has approved any A3 change to DMA or platform interfaces
      (IF-008) before dependent implementation merges. The approval names the interface and version.
      A1 accepted IF-008 v1.1 at A3 `b9a69d8`; A3 accepted the §6 conditions at `307b919`.
      D-007/D-021 remain Proposed. The row in
      [`cross-agent-blockers.md`](cross-agent-blockers.md) moves to Signed in the same PR.
- [ ] **Docs.** The document that describes the changed behaviour is updated in the same PR.
- [ ] **Evidence.** The agent report is attached and includes the commit hash and the test command.
- [ ] **Rollback.** `git revert <sha>` restores the previous state with no data migration.
- [ ] **Security.** No new secret, host mount, network dependency, privileged operation or raw
      device. If any is needed, ADR plus Director approval.
- [ ] **CI safety.** `make check-ci-safety` passes. No privileged container, docker socket, raw disk
      write or third-party Windows binary execution in normal CI.

## Required tests by change type

| Change | Required tests |
| --- | --- |
| Documentation only | Link check by review; no build required |
| Build script or toolchain | `make build` twice, `make verify-repro`, `make test-mbr-harness` |
| MBR harness or marker contract | `make test-mbr-harness` plus the negative test: remove one marker and confirm failure |
| Shared header | Build all consumers; `make ci`; interface registry updated |
| DMA or platform interface (A3) | A1 sign-off recorded; A3 unit tests; `make test-mbr-harness` |
| Driver or platform code | Owner's unit tests plus `make test-mbr-harness` |
| PE parsing or static analysis | Static inspection only; `make test-pe-static` once implemented; no execution in normal CI |
| Userland or tooling | Owner's unit tests plus `make test-mbr-harness` |
| CI workflow | Workflow run link; `make ci` locally with the same commands |

## Negative tests (harness self-check)

The harness is only trustworthy if it fails when it should. Before accepting a change to the boot
contract, run:

```sh
# 1. Missing marker: expect non-zero exit and the marker named in result.json
cp tests/qemu/expected-markers.txt /tmp/markers.bak
printf 'OZERO-STAGE0-NEVER-PRINTED\n' >> tests/qemu/expected-markers.txt
make test; echo "exit=$?"        # must be non-zero
cp /tmp/markers.bak tests/qemu/expected-markers.txt

# 2. Corrupt image: expect a harness error before QEMU starts
#    Call the harness directly; `make test` would rebuild a valid image first.
cp build/ozero-stage0.img /tmp/good.img
printf '\x00' >> build/ozero-stage0.img
scripts/test-qemu.sh; echo "exit=$?"        # must be 2 (harness error)
cp /tmp/good.img build/ozero-stage0.img

# 3. Timeout: expect a timeout result, not a hang
TIMEOUT=0 make test; echo "exit=$?"
```

## Regression handling

- A previously passing test that fails is a blocker, even if the current change looks unrelated.
- Bisect first, guess second. Attach the first failing commit to the PR.
- A flaky test needs an owner, an issue link and an expiry date. After the expiry it is either fixed
  or deleted with an ADR.
- Never weaken a marker or a timeout to make a test pass. Change the contract through the interface
  registry, or fix the code.
- Never delete a failing test in the same PR that introduces the failure.

## Rollback

Stage 0 has no persistent state, so rollback is a revert. For later stages:

1. Revert the merge commit on `main`; do not rewrite history.
2. Re-run `make ci` on the revert.
3. Record the revert and the reason in the decision log.
4. If a data or interface migration was shipped, the revert needs a forward fix, not a downgrade.
   That case needs Director approval before merge in the first place.

## Current branch integration order

At the 2026-11-23 R2 audit snapshot the open branches are `a4/stage0-review-followup` (PR #3,
head `b4232a0` plus this audit), `a1/stage0-review` (PR #1, head `74b1471`),
`agent/a2-stage0` (PR #2, head `64b3458`, code `6d305b8`) and `a3/stage0-review-response` (head
`307b919`, no PR; based on A4 `3ae7aca`, **not in A4**). Merge in this order:

1. A4 control plane: it defines `CODEOWNERS`, `Makefile`, `tests/targets.tsv`, `scripts/` and the
   process documents. Nothing else can be checked against the gates until this lands.
2. A1: rebase onto the A4 base. Resolve `AGENTS.md`, `README.md` and `docs/README.md` in favour of
   the A4 control-plane text, then add A1's content. A1's `03f8883` refactor makes `include/ozero/`
   the only public root (D-009); `tools/check-headers.sh` and `tests/abi_static_asserts.c` are
   A1-owned, with A4 owning CI integration (D-010). Register the ABI test in `tests/targets.tsv`.
3. A2: rebase onto the same base. Keep the `a2/` package intact and add a root `test-pe-static`
   target; do not merge `a2/Makefile` into the root Makefile. Record the current evidence pair
   (`6d305b8` / `64b3458`) and A1/A3's IF-009 v1.0 signatures in the response and review ledger
   (XB-013, XB-003).
4. A3: `307b919` is based on A4 `3ae7aca` and keeps its package in `a3/`; the root `README.md` /
   `AGENTS.md` are A4's on the A3 branch (XB-017 Closed). The review basis is A4 `3ae7aca` (XB-018
   Closed). A3 accepted A1's §6 conditions at `307b919` (XB-019 Closed). A4 `b4232a0` and PR #3 do
   not contain `a3/` or root `include/ozero/`; integrate A3 without overwriting shared files or keep
   it separate (XB-020). Merge only after D-007, D-021 and Director authorization; A1's IF-008
   ACCEPT at `b9a69d8` is recorded.

A1's `74b1471` review is a final interface ACCEPT for IF-008 v1.1, not hardware validation, not a
runtime DMA test and not Stage 0 acceptance. A3's `make check` and A1-header compile are static
evidence, not an AHCI probe or DMA test. The AHCI probe stays blocked until the Director decides
D-007/D-021.

## Stage acceptance handoff

When every box is checked, A4 fills `docs/templates/stage-acceptance.md` with the evidence links and
submits it to the Director. A4 does not mark the stage accepted. Only the Director does.
