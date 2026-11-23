# AGENTS.md — repository memory

Read this before changing anything. It is the short version; the linked documents are normative.

## Purpose

Ozero (o0) is a four-agent engineering project. A4 owns this control-plane repository: shared
process, repository contracts, build/test automation, integration quality and evidence tracking.
A1–A3 own their domain source trees and the technical correctness of their subsystems.

## Commands

```sh
make check-toolchain    # verify as/ld/objcopy/make/qemu against toolchain/toolchain.lock
make build              # -> build/ozero-stage0.{elf,img,map}, build/manifest.json
make test-mbr-harness   # QEMU MBR fixture smoke test -> build/qemu/{serial.log,result.json,summary.txt}
make test               # alias for test-mbr-harness; not kernel boot
make targets            # list test targets and their status
make check-ci-safety    # reject privileged, raw-disk or Windows-binary CI steps
make ci                 # check-toolchain + check-ci-safety + build + test-mbr-harness
make verify-repro       # two clean git-archive builds must be bit-identical
make verify-clean       # clone HEAD to a temp dir and run `make ci` there
make clean
scripts/audit-repos.sh [peer]   # commit graph, branches and clean-tree state
```

Override the build directory with `BUILD_DIR=/path`. The QEMU test accepts
`IMAGE=...`, `TIMEOUT=...` and `OUTDIR=...`.

## Conventions

- Branches: `a<N>/<ticket>-<slug>` (for example `a1/boot-001-multiboot-header`). One task per branch.
- Commits: imperative subject, body explains why, reference the task id. Include
  `Co-authored-by: openhands <openhands@all-hands.dev>` when an agent authors the commit.
- PRs: small, one concern, with the agent report from `docs/templates/agent-report.md` pasted in.
- Status words are reserved: **Proposed**, **Implemented**, **Tested**, **Approved**. Do not
  upgrade a status without evidence. Only the Director sets **Approved**.
- Test names are claims: `mbr-harness-smoke` proves the MBR fixture and harness, not kernel boot.
  See `docs/process/test-targets.md`.
- Build outputs go to `build/` and are never committed. Evidence goes to `evidence/` and is.
- No new dependency without a pinned version and an owner. No vendored code without a hash.
- Cross-agent interfaces need the owner's sign-off before dependent implementation. A1 approves A3
  DMA/platform interface changes (IF-008).
- Normal CI must pass `make check-ci-safety`: no privileged container, docker socket, host device,
  raw disk write, loop device or third-party Windows binary execution.

## Ownership boundaries

- `include/ozero/` is the shared header surface. A1 owns it; A4 reviews every change; consumers
  must not fork it.
- A1–A3 must not edit `docs/`, `scripts/`, `tests/`, `harness/`, `toolchain/` or `.github/` without
  an A4 review on the PR.
- A4 must not edit A1–A3 domain source. If a fix is needed there, open an issue and request the
  owner.
- `CODEOWNERS` is the machine-readable copy of the ownership map. Keep both in sync.

## Environment notes

- The Stage 0 harness needs only binutils, make, bash and QEMU. It does not need a cross-compiler,
  libc, GRUB or Limine.
- QEMU runs in TCG mode with no network device and no host mounts beyond the repository.
- If `qemu-system-x86_64` is missing: `sudo apt-get install -y qemu-system-x86`.
- Reference toolchain used for recorded evidence: Debian 13, binutils 2.44, GNU make 4.4.1,
  QEMU 10.0.13. Bit-identical rebuilds require the same binutils version.

## Non-negotiables

1. Never report an agent as launched, or work as complete, without a real execution trace.
2. Never mark a task complete because code compiled. Show the test and its result.
3. Never give an agent a broader workspace, secret or network scope than the task requires.
4. Never let two agents write to the same working tree.
5. Never describe the 512-byte MBR fixture as kernel boot. It proves the harness only.
6. Never run normal CI with privileges, raw disks, host devices or third-party Windows binaries.
