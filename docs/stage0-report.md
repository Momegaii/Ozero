# Stage 0 report — Ozero A1

Reporting fields follow the briefing format.

## Status

| Required output | Status | Where |
|---|---|---|
| Kernel architecture note | draft ready | `docs/kernel-architecture.md` |
| Boot contract | draft ready | `docs/boot-contract.md` |
| CPU baseline matrix | draft ready | `docs/cpu-baseline.md` |
| Interface draft | draft ready (sketch) | `docs/interface-draft.md`, `include/o0/` |
| Stage 1 plan | draft ready | `docs/stage1-plan.md` |
| Source index | complete | `docs/sources.md` |

Overall: **draft ready for review**. No implementation started. Decisions D1–D4 need
director approval before Stage 1.

## Evidence

- Commit: `c46ee8627eb48eb5a64be00a508dcb417c6ddfe4`
  (all deliverables below are in this commit; this report is a follow-up commit).
- Files:
  - `docs/kernel-architecture.md`, `docs/boot-contract.md`, `docs/cpu-baseline.md`,
    `docs/interface-draft.md`, `docs/stage1-plan.md`, `docs/sources.md`
  - `include/o0/{types,status,boot,cpu,mm,irq,thread,ipc,driver,o0}.h`
  - `tests/abi_static_asserts.c`, `tools/check-headers.sh`
- Command and actual output:

```
$ ./tools/check-headers.sh
== standalone header compilation ==
PASS  boot.h
PASS  cpu.h
PASS  driver.h
PASS  ipc.h
PASS  irq.h
PASS  mm.h
PASS  o0.h
PASS  status.h
PASS  thread.h
PASS  types.h
== umbrella header, C and C++ ==
PASS  o0/o0.h (C11)
PASS  o0/o0.h (C++17)
== ABI static assertions ==
PASS  tests/abi_static_asserts.c
== generic-interface device neutrality ==
PASS  no device-class or GPU-generation terms in include/o0
== freestanding headers only ==
PASS  no hosted libc headers in include/o0
RESULT: PASS
```

What this evidence does and does not show:

- Shows: headers are self-consistent, compile freestanding as C11 and C++17, documented
  ABI facts hold, generic interfaces contain no device-class or GPU-generation terms, and
  no hosted libc leaks into public headers.
- Does not show: any runtime behaviour. QEMU boot tests are Stage 1 and have not run. This
  environment has no QEMU, no cross toolchain and no Limine, so a boot test was not
  possible here even if Stage 0 allowed implementation.

## Decisions needed

Priority order: D1–D4 unblock Stage 1. D5–D10 can be ratified in parallel.

| ID | Decision | Options | Recommendation |
|---|---|---|---|
| D1 | Boot path | UEFI + Limine base revision 6; custom UEFI loader; UEFI + another protocol | **Limine**, pending approval; comparison in boot contract §2 |
| D2 | Paging mode | 4-level only; 5-level when available; 5-level default | **4-level default, 5-level opt-in** via the paging-mode feature |
| D3 | Kernel placement and direct map | Fixed kernel base + discovered HHDM; fixed HHDM; fully relocatable kernel | **Fixed `0xffffffff80000000` image, discovered HHDM, never hard-coded** |
| D4 | Interrupt model | APIC + IOAPIC; legacy PIC; hybrid | **APIC/IOAPIC only**, PIC stays masked |
| D5 | SMP startup | Limine MP feature; own INIT-SIPI-SIPI trampoline | **Limine MP feature**, kernel-controlled takeover |
| D6 | ACPI depth | RSDP + MADT/FADT only; full AML interpreter | **MADT/FADT only** for Stage 1 |
| D7 | EFI runtime services | Never; at boot; on demand | **Record only, do not use** in Stage 1 |
| D8 | Syscall entry | `int 0x80` first, `SYSCALL` later; `SYSCALL` first | **Trap gate first**, fast path second |
| D9 | Object/handle model | Opaque generation-tagged handles; pointer-like handles; NT-style handles | **Opaque 64-bit handles**, ABI unfrozen |
| D10 | DMA interface | Constraint-driven; device-class-specific | **Constraint-driven**, no device assumptions |

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Limine's contract changes between pinned revisions | Boot contract drifts | Pin the protocol commit (`877c2d4c`) and re-review on upgrade |
| QEMU success is mistaken for hardware support | Field failures in paging/APIC/APs | Stage 1 matrix plus a hardware bring-up gate before any release claim |
| No IOMMU in Stage 1 (Limine disables VT-d/AMD-Vi) | A malicious or buggy driver can DMA anywhere | Keep all drivers in ring 0 and trusted; revisit with U4 |
| Parked APs live in bootloader-reclaimable memory | Reclaiming too early causes random AP triple faults | Hard reclaim checklist (boot contract §8) and an AP acknowledgement counter |
| `CR0.MP=0` at entry and SSE code generation | Fault on the first compiler-generated SSE instruction | `-mgeneral-regs-only` entry unit or enable SSE first (boot contract §3.5) |
| HHDM does not map reserved/bad memory | A wrong `o0_phys_to_virt` is a silent wild access, not a fault | Region-type assertion in debug builds; ownership table in boot contract §5 |
| Secure Boot disabled in test | Production boot blocked | Signing is a release requirement, not a Stage 1 task |
| 5-level paging untested | Claimed support without coverage | Drop from Stage 1 if QEMU cannot exercise it (gate G4) |

## Acceptance criteria check

| Criterion | Status | Evidence |
|---|---|---|
| Every architectural recommendation cites an Intel/UEFI/boot-protocol source | Met | Citation keys in every decision; `docs/sources.md` |
| Boot protocol alternatives compared; selection pending approval | Met | Boot contract §2, D1 marked pending |
| No Gen8+ Intel GPU assumptions in generic kernel memory interfaces | Met and automated | `tools/check-headers.sh` neutrality check passes |
| Boot contract implementable and testable in QEMU | Met as a plan | Ordered handoff, 12 assertions, Stage 1 QEMU matrix |
| All unknowns and CPU/firmware assumptions explicit | Met | Architecture note §7–§8; CPU matrix §6 |

## Next step

Director review of D1–D4. On approval: A4 ratifies the build layout and toolchain, then
M0 (bootable image and serial banner) starts. Until then, no kernel implementation.
