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
| `docs/a2-interface-review.md` | A1 review of A2's process, address-space, handle and syscall requirements |
| `docs/a3-dma-contract.md` | A1 conditions for A3's `platform.h` and `block.h` (IF-008, D-007) |
| `docs/decisions/a1-global-decisions.md` | A1 proposals in the global decision ID space, D-009 through D-030 |
| `docs/sources.md` | Citation keys and exact source URLs |

## Interface sketch

`include/ozero/` contains the headers: `types.h`, `status.h`, `boot.h`, `cpu.h`, `mm.h`,
`irq.h`, `thread.h`, `ipc.h`, `driver.h`, plus the `o0.h` umbrella. They are sketches, not
a frozen ABI. Signatures may change in review.

## Checks

```
./tools/check-headers.sh
```

Compiles every header standalone as freestanding C11, checks the umbrella header as C11
and C++17, runs the ABI static assertions in `tests/abi_static_asserts.c`, enforces that
generic headers contain no device-class or GPU-generation assumptions and no hosted libc
headers, and rejects `include/o0/` or legacy `o0/` includes. A1 owns the script; A4 owns
CI integration (D-010).

## Stage status

Stage 0: **not accepted**. The director's second review asked A1 to resolve the header
namespace and tooling ownership, review A2's kernel-facing requirements and confirm the
A3 DMA interface. Those responses are in this branch. No kernel implementation has started.
Build integration belongs to A4.
