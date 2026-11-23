# Ozero (o0) — A1 kernel engineering

Stage 0 deliverables for the x86-64 kernel foundation: architecture note, boot contract,
CPU baseline matrix, interface sketch and Stage 1 plan. Nothing here is an approved
implementation; decisions marked *pending* need director sign-off.

## Documents

| File | Contents |
|---|---|
| `docs/kernel-architecture.md` | Responsibilities, privilege boundary, subsystem map, decisions, unknowns, assumptions |
| `docs/boot-contract.md` | Entry state, boot info, memory ownership, handoff and reclaim rules, QEMU-testable assertions |
| `docs/cpu-baseline.md` | Required vs optional CPU features, detection order, fallbacks, test envelope |
| `docs/interface-draft.md` | Header conventions, interface rationale, stability rules |
| `docs/stage1-plan.md` | Ordered milestones, QEMU matrix, expected serial output, risk gates |
| `docs/stage0-report.md` | Status, evidence, decisions needed, risks, next step |
| `docs/sources.md` | Citation keys and exact source URLs |

## Interface sketch

`include/o0/` contains the headers: `types.h`, `status.h`, `boot.h`, `cpu.h`, `mm.h`,
`irq.h`, `thread.h`, `ipc.h`, `driver.h`, plus the `o0.h` umbrella. They are sketches, not
a frozen ABI. Signatures may change in review.

## Checks

```
./tools/check-headers.sh
```

Compiles every header standalone as freestanding C11, checks the umbrella header as C11
and C++17, runs the ABI static assertions in `tests/abi_static_asserts.c`, and enforces
that generic headers contain no device-class or GPU-generation assumptions and no hosted
libc headers.

## Stage status

Stage 0: draft ready for review. No kernel implementation has started. Build integration
belongs to A4.
