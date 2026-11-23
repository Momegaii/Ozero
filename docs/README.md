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
| `docs/a2-interface-review.md` | A1 review of A2's process, address-space, handle and syscall requirements (A2 `f324b28`) |
| `docs/a2-contract-review-response.md` | A1 answers to A2's A1-REV-01…12, reconciled at A2 `d02598a` / evidence `3f78477` |
| `docs/a3-dma-contract.md` | A1 conditions for A3's `platform.h` and `block.h` (IF-008, D-007) |
| `docs/if008-v1.1-review.md` | A1 final review of A3 IF-008 v1.1 at `b9a69d8`: ACCEPT |
| `docs/if009-review.md` | A1 review of IF-009 v1.0 at A2 `d02598a`: ACCEPT (A1 side); A3/A4 pending |
| `docs/decisions/a1-global-decisions.md` | A1 proposals in the global decision ID space, D-009 through D-033 |
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

Stage 0: **not accepted**. The director's action brief v2 and the proceed brief asked
A1 to review A3's IF-008 v1.1, answer A2's A1-REV-01…12, review IF-009 v1.0, preserve
the DMA boundary and keep AHCI blocked. A1 accepted IF-008 v1.1 at A3 `b9a69d8`,
reconciled A2 at `d02598a`/`3f78477`, and accepted IF-009 v1.0 on the A1 side. A3 v1.0
confirmation and A4 registry updates remain. AHCI remains blocked on director D-007,
D-021 and A3's acceptance of the IF-008 conditions. No kernel implementation has
started. Build integration belongs to A4.
