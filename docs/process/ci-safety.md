# CI safety policy

| Field | Value |
| --- | --- |
| Status | **Implemented** |
| Owner | A4 |
| Approver | Project Director |
| Enforced by | `make check-ci-safety` |

## Scope

This policy covers normal CI: every push and pull request. Opt-in jobs that need hardware or
third-party binaries are separate and are listed at the end.

## Allowed in normal CI

- Compiling and linking repository source with the pinned toolchain.
- Running QEMU in TCG mode with `-nodefaults -nic none -display none`.
- Capturing serial output to a file inside `build/`.
- Installing distribution packages needed to build or test (`binutils`, `make`,
  `qemu-system-x86`).
- Uploading `build/` artifacts.
- Running the static CI-safety check itself.

## Forbidden in normal CI

| Forbidden | Why |
| --- | --- |
| `--privileged`, `--cap-add`, docker socket mounts | Container escape reaches the host |
| Host device passthrough (`--device`) | Guest or container controls host hardware |
| Raw disk writes (`dd of=/dev/sd*`, `losetup`, `mkfs`, `fdisk`, `parted`) | Destroys host or runner state |
| `/dev/mem`, `/dev/kmem`, `/dev/port` | Direct kernel or port access |
| `wine`, direct `.exe` execution | Runs untrusted third-party Windows binaries |
| `curl ... \| sh`, `wget ... \| sh` | Executes unreviewed remote code |
| Secrets in commands, logs or artifacts | Disclosure |
| KVM or host CPU passthrough | Not needed for Stage 0 and broadens the attack surface |

## Opt-in jobs

These must not run on ordinary pull requests:

- **Hardware matrix**: named physical machines, manual approval, no secrets beyond the test account.
- **PE runtime corpus**: Windows binaries in a disposable, network-isolated VM. Static analysis is
  allowed in normal CI; execution is not.
- **Limine / kernel boot**: a disposable VM. QEMU is acceptable; host boot is not.

Opt-in jobs state their isolation, owner and expiry in the workflow and in the agent report.

## Enforcement

`scripts/check-ci-safety.sh` scans `.github/workflows/*.yml`, `*.yaml` and `scripts/*.sh` for the
forbidden patterns above. It runs as part of `make ci` and as a CI step. A finding fails the build.

The check is static. It reduces risk; it does not prove that a workflow is safe. Review still
matters.

## Review checklist

- [ ] No privileged mode, added capabilities or docker socket.
- [ ] No host device, raw block device, loop device or partition tool.
- [ ] No third-party Windows binary execution.
- [ ] No remote code piped to a shell.
- [ ] No secret printed or uploaded.
- [ ] QEMU has no network device and no display.
- [ ] Any opt-in job is manual, isolated and time-limited.
