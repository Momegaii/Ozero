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

## Stage acceptance handoff

When every box is checked, A4 fills `docs/templates/stage-acceptance.md` with the evidence links and
submits it to the Director. A4 does not mark the stage accepted. Only the Director does.
