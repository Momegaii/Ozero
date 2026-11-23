# Stage 0 report — Ozero A1

Reporting fields follow the briefing format. This is a **draft for review**, not an
acceptance certificate. Nothing below claims runtime behaviour that was not executed.

## 1. Status

| Required output | Status | Where |
|---|---|---|
| Kernel architecture note | draft, review response applied | `docs/kernel-architecture.md` |
| Boot contract | draft, review response applied | `docs/boot-contract.md` |
| CPU baseline matrix | draft, review response applied | `docs/cpu-baseline.md` |
| Interface draft | draft (sketch) | `docs/interface-draft.md`, `include/ozero/` |
| Stage 1 plan | draft | `docs/stage1-plan.md` |
| Source index and pins | complete for this review | `docs/sources.md` |
| Stage 0 report | this document | `docs/stage0-report.md` |

Overall: **draft ready for review**. No kernel implementation has started. D1–D4 remain
pending director approval. A3 interface review is signed with one recorded blocker.

## 2. Review response (23 November 2026)

| # | Review action | Status | Where |
|---|---|---|---|
| 1 | Pin exact Limine protocol commit, binary hash, header hash and base revision; refuse unsupported revision | Done | `docs/sources.md` "Pinned artefacts"; `docs/boot-contract.md` §2.2, §11 |
| 2 | Correct base-revision explanation, separating current revision from x86-specific changes | Done | `docs/boot-contract.md` §3; `docs/kernel-architecture.md` D1; `docs/sources.md` |
| 3 | Document AP lifecycle: parked stack, trampoline, MP response lifetime, AP switch, proof before reclaim | Done | `docs/boot-contract.md` §9.1–§9.4; `docs/stage1-plan.md` M7 |
| 4 | Define HHDM mapping predicates, checked arithmetic, MMIO distinction | Done | `docs/boot-contract.md` §5.1; `include/ozero/mm.h`; `docs/stage1-plan.md` M1 |
| 5 | Review A3 platform/DMA/block APIs: address width, IOMMU, coherency, lifetimes, reset, in-flight DMA | Done, with blocker | `docs/interface-draft.md` §11; `include/ozero/mm.h`, `include/ozero/driver.h` |
| 6 | Return a source-to-contract table for critical boot claims | Done | `docs/boot-contract.md` §14 (21 rows); `docs/cpu-baseline.md` §4.1 |

Definition-of-done check:

| DoD item | Status | Evidence |
|---|---|---|
| No ambiguous claim that revision 6 adds new x86 state | Met | `docs/boot-contract.md` §3, `docs/kernel-architecture.md` D1, `docs/sources.md` |
| Every entry-state register claim cites pinned source and Intel SDM section | Met | Per-row Source columns in §3.1–§3.4, §14 table, `docs/cpu-baseline.md` §4.1 |
| AP and bootloader-memory lifetime is explicit | Met | `docs/boot-contract.md` §8, §9.1–§9.4 |
| HHDM conversion checks mapped type, range and overflow | Met | `docs/boot-contract.md` §5.1; `o0_hhdm_to_virt` in `include/ozero/mm.h` |
| A1 signs A3 interface review or records blockers | Met, with one blocker | `docs/interface-draft.md` §11 |

## 3. Evidence

- Repository: <https://github.com/Momegaii/Ozero> (shared source of truth)
- Branch: `a1/stage0-review` (integration through a pull request; `main` is not pushed to)
- Pull request: <https://github.com/Momegaii/Ozero/pull/1>
- Staging repository before integration: `/workspace/project`, branch `master`
- Commit containing all deliverables: `4943538609c25dbcaf57d60016a61f1e58c56341`
  ("Stage 0 review response: pin boot stack, correct revision claim, tighten
  AP/HHDM/DMA contracts"). This report is a follow-up commit.
- Files in that commit:
  - `docs/boot-contract.md`, `docs/cpu-baseline.md`, `docs/interface-draft.md`,
    `docs/kernel-architecture.md`, `docs/sources.md`, `docs/stage1-plan.md`
  - `include/ozero/boot.h`, `include/ozero/driver.h`, `include/ozero/mm.h`, `include/ozero/status.h`
  - `tests/abi_static_asserts.c`
- Full repository file list: `git ls-files`.

### 3.1 Commands and raw output

Header and ABI checks:

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
PASS  ozero/o0.h (C11)
PASS  ozero/o0.h (C++17)
== ABI static assertions ==
PASS  tests/abi_static_asserts.c
== generic-interface device neutrality ==
PASS  no device-class or GPU-generation terms in include/ozero
== freestanding headers only ==
PASS  no hosted libc headers in include/ozero
RESULT: PASS
```

Limine pin verification (temporary directory, 2026-11-23):

```
$ sha256sum limine.h PROTOCOL_pinned.md limine-binary.tar.xz
bd2fa78d45353725952d7f9bd1fec17e3e19e4721af95f961d4d6b9285a1ffeb  limine.h
3e702cd9dbfa2ead76eab5b34479fbc507b031d599b56fb5eb1db743cfd08f68  PROTOCOL_pinned.md
493312624a0a7a3afadcf8e9993f5418a87258d6b54f9d7ebb914037fe089220  limine-binary.tar.xz

$ grep -n 'SUPPORTED_BASE_REVISION' limine-12.11.1/common/protos/limine.c
53:#define SUPPORTED_BASE_REVISION 6

$ diff -q limine.h limine-12.11.1/limine-protocol/include/limine.h
$ diff -q PROTOCOL_pinned.md limine-12.11.1/limine-protocol/PROTOCOL.md
(no output; both files identical to the pinned protocol revision)
```

What this evidence shows:

- Headers are self-consistent, compile freestanding as C11 and C++17, documented ABI facts
  hold, generic interfaces contain no device-class or GPU-generation terms, and no hosted
  libc leaks into public headers.
- The pinned Limine release (`v12.11.1`) supports base revision 6 and vendors exactly the
  pinned protocol revision.

What it does **not** show: any runtime behaviour. See §7.

## 4. Decisions needed

Priority order: D1–D4 unblock Stage 1. D11–D14 are review-specific.

| ID | Decision | Options | Recommendation |
|---|---|---|---|
| D1 | Boot path | UEFI + Limine base revision 6; custom UEFI loader; UEFI + another protocol | **Limine**, pending approval; comparison in boot contract §2 |
| D2 | Paging mode | 4-level only; 5-level when available; 5-level default | **4-level default, 5-level opt-in** |
| D3 | Kernel placement and direct map | Fixed kernel base + discovered HHDM; fixed HHDM; fully relocatable kernel | **Fixed `0xffffffff80000000` image, discovered HHDM, never hard-coded** |
| D4 | Interrupt model | APIC + IOAPIC; legacy PIC; hybrid | **APIC/IOAPIC only**, PIC stays masked |
| D5 | SMP startup | Limine MP feature; own INIT-SIPI-SIPI trampoline | **Limine MP feature**, kernel-controlled takeover |
| D6 | ACPI depth | RSDP + MADT/FADT only; full AML interpreter | **MADT/FADT only** for Stage 1 |
| D7 | EFI runtime services | Never; at boot; on demand | **Record only, do not use** in Stage 1 |
| D8 | Syscall entry | `int 0x80` first, `SYSCALL` later; `SYSCALL` first | **Trap gate first**, fast path second |
| D9 | Object/handle model | Opaque generation-tagged handles; pointer-like handles; NT-style handles | **Opaque 64-bit handles**, ABI unfrozen |
| D10 | DMA interface | Constraint-driven; device-class-specific | **Constraint-driven**, no device assumptions |
| D11 | IOMMU policy for Stage 1 | Trusted ring-0 drivers only; require IOMMU before any DMA | **Trusted ring-0 drivers only**, blocker recorded for untrusted drivers |
| D12 | SDM citation policy | 2018 numbered sections + titles; require current-edition numbers before sign-off | **2018 numbered sections + titles**, since the current PDF is bot-blocked here |
| D13 | A3 acceptance of DMA limitations | Accept IOMMU blocker and no scatter-gather object; add both before A3 work | **Accept for Stage 1**, escalate before untrusted drivers |
| D14 | Limine host tool | Build `limine` from the pinned source tarball; pin a distro package | **Build from the pinned source**, since the binary release has no Linux host tool |

## 5. Unresolved risks

| Risk | Impact | Mitigation / status |
|---|---|---|
| No IOMMU in Stage 1 (Limine disables VT-d/AMD-Vi) | A buggy or malicious driver can DMA anywhere | Trusted ring-0 drivers only; recorded blocker D11 |
| `o0_dma_buffer` describes one contiguous buffer | A3 cannot express a transfer larger than `max_segment` in one object | A3 builds SG lists from multiple buffers; documented limitation |
| SDM numbers cited from the 2018 edition | Current-edition numbers differ for shifted chapters | Section titles are the stable anchor; D12 |
| 5-level paging not covered by the 2018 mirror | LA57 claims are cited by chapter title only | Q5 test cell; D2 |
| Parked APs and trampolines live in reclaimable memory | Early reclaim causes random AP triple faults | Hard proof gate (boot contract §9.3); no AP hotplug |
| Limine protocol pinned to a `trunk` commit | Upstream may move | Commit + SHA-256 pins; re-review on upgrade |
| No MBR harness exists | A4 must not conflate MBR boot with Limine boot | Labelled in Stage 1 plan §1 |
| QEMU success is not hardware support | Paging/APIC/AP bugs can hide in emulation | Hardware bring-up gate before any release claim |
| Secure Boot disabled in test | Production boot blocked | Signing is a release requirement, not a Stage 1 task |

## 6. Dependencies on other agents

| Agent | Dependency | State |
|---|---|---|
| Director | Approve D1–D4 and D11–D14 | Open; Stage 0 is not accepted |
| A2 | Review process, thread, address-space and handle concepts; confirm they do not freeze kernel internals | Requested; no ABI freeze before sign-off |
| A3 | Accept the DMA/MMIO interface conditions (IOMMU blocker, no SG object, dedicated UC BAR mappings) before AHCI probe or real DMA | Requested; A1 sign-off is not A3 acceptance |
| A4 | Ratify build/toolchain; build the Limine host tool from the pinned source; verify repository history and evidence; distinguish the MBR harness from UEFI/Limine kernel boot; track cross-agent blockers | Open |
| A4 | Provide QEMU + OVMF CI so B1–B14 can actually run | Open; no boot test exists yet |

Cross-agent blockers carried in this report:

- IOMMU absence blocks untrusted or userspace DMA. A1 recommends trusted ring-0 drivers
  for Stage 1; the director and A3 must accept or reject that.
- `o0_dma_buffer` cannot represent a transfer larger than `max_segment`; A3 must build SG
  lists from multiple buffers or request a segment-list API.
- SDM citations use the 2018 numbered sections plus titles; the director must accept that
  policy or require current-edition numbers before sign-off.

## 7. Not tested

Explicitly **not** done or not executed:

- No kernel implementation exists; no subsystem was written.
- No QEMU boot of any kind was run. The environment has no QEMU, cross toolchain,
  ISO tool or Limine host tool.
- No Limine ISO or disk image was built.
- No AP takeover, APIC routing, paging switch, exception self-test or reclaim was executed.
- No runtime test of `o0_hhdm_to_virt`; only its prototype and documented contract exist.
- No driver, AHCI probe or DMA transfer was executed.
- No 5-level paging test; the 2018 SDM mirror predates it.
- No IOMMU test; Stage 1 has no IOMMU.
- No physical hardware test.
- No A2, A3 or A4 sign-off has been received. A1's A3 review is signed with the IOMMU
  blocker, which is not A3 acceptance.
- No Windows compatibility layer, syscall ABI or handle ABI was tested or frozen.
- No Secure Boot or signed-image test.

The only executed checks are the compile-time header/ABI checks in §3.1 and the Limine
pin verification. Neither is a boot test.

Tool availability at report time (so the absence of a boot test is auditable):

```
qemu-system-x86_64     MISSING
clang                  MISSING
ld.lld                 MISSING
xorriso                MISSING
limine                 MISSING
gcc                    /usr/bin/gcc
```

Host `gcc` compiled the headers. No kernel image, ISO or boot test was produced.

## 8. Acceptance criteria check

| Criterion | Status | Evidence |
|---|---|---|
| Every architectural recommendation cites an Intel/UEFI/boot-protocol source | Met | Citation keys in every decision; `docs/sources.md` |
| Boot protocol alternatives compared; selection pending approval | Met | Boot contract §2, D1 marked pending |
| No Gen8+ Intel GPU assumptions in generic kernel memory interfaces | Met and automated | `tools/check-headers.sh` neutrality check |
| Boot contract implementable and testable in QEMU | Met as a plan | Ordered handoff, 14 assertions, Stage 1 QEMU matrix |
| All unknowns and CPU/firmware assumptions explicit | Met | Architecture note §7–§8; CPU matrix §6 |
| Source-to-contract traceability for critical boot claims | Met | Boot contract §14; CPU baseline §4.1 |

## 9. Next step

Director review of D1–D4 and the review-specific D11–D14. On approval: A4 ratifies the
build layout and toolchain (including the Limine host tool), then M0 (bootable image and
serial banner) starts. A3 must accept the interface conditions in
`docs/interface-draft.md` §11 before any real DMA. Until then, no kernel implementation.

Integration: the A1 branch `a1/stage0-review` is proposed in pull request #1
(<https://github.com/Momegaii/Ozero/pull/1>). Stage 0 is **not** accepted by this report;
it is evidence for director review.
