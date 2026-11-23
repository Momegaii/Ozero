# D-005 boot-protocol decision package

| Field | Value |
| --- | --- |
| Decision | D-005: choose the boot protocol and kernel handoff |
| Status | **Decision package only. D-005 remains Proposed.** |
| Owner | A1 (technical), Project Director (approval) |
| Prepared by | A4, sole active agent under the 2026-11-23 takeover order |
| Consumers | A1 kernel entry, A3 platform data, A4 harness and CI |
| Evidence level | Document and source review; no boot was executed for this package |
| Stage 0 | **NOT ACCEPTED**; this package does not change D-006 |
| Date | 2026-11-23 |

## 1. Decision requested

Choose the firmware and bootloader path that loads the Ozero x86-64 kernel and defines the
entry state, boot information and memory ownership at handoff. The choice must be explicit
before Stage 1 boot code or a Limine-specific CI job is written.

This package does not approve the choice. The Director records the decision in
[`decision-log.md`](decision-log.md); A1 then updates IF-001 and the boot contract.

## 2. Alternatives

| Option | What it means | Advantages | Costs and risks | Disposition |
| --- | --- | --- | --- | --- |
| **A. UEFI firmware + Limine base revision 6** | Limine v12.11.1 loads a non-relocatable higher-half kernel and provides the documented entry state and response structures | Firmware handoff, memory map, HHDM, SMP parking, RSDP and framebuffer discovery are specified and implemented; the protocol is versioned and pinned; the kernel can reject an unhonoured base revision | Limine is third-party code in the TCB; the host tool must be built from the pinned source; the entry state must be tested, not assumed | **Recommended** |
| B. Custom UEFI loader | Ozero implements PE/ELF loading, `GetMemoryMap`, `ExitBootServices`, page tables, stack, RSDP and SMP startup | Full control; no bootloader dependency | Duplicates security-sensitive firmware code; larger attack surface; delays Stage 1; harder to separate bootloader and kernel faults | Rejected for Stage 1 |
| C. GRUB + Multiboot2 | GRUB loads the kernel and passes a Multiboot2 information structure | Widely available; simple QEMU path | Multiboot2 is not the final UEFI handoff contract; memory-map and SMP semantics differ; another large TCB; migration later would rewrite the entry path | Rejected unless the Director requires GRUB for an external reason |
| D. UEFI direct boot stub | The kernel is a UEFI application and performs its own handoff | No separate bootloader; small dependency surface | The kernel must implement firmware exit, memory-map normalisation, paging and SMP; same cost as B with less isolation | Rejected for Stage 1 |
| E. BIOS + Limine or Multiboot | Legacy BIOS path for test convenience | Works on older QEMU and hardware | Not the target firmware; hides UEFI-specific defects; adds a second contract to maintain | Test-only at most; not the Stage 1 decision |

## 3. Recommendation

Adopt **Option A: UEFI firmware + Limine base revision 6, bootloader v12.11.1, with a
non-relocatable higher-half kernel**.

The technical rationale is the handoff boundary, not a preference for Limine:

1. Limine documents the exact x86-64 entry state: long mode, CPL 0, paging, `CR0`/`CR4`/`EFER`,
   `RFLAGS`, segment registers, zeroed general registers and a loaned stack.
2. It supplies a normalised memory map with usable, reclaimable, ACPI, framebuffer and
   reserved-mapped types, plus the HHDM offset and the kernel physical/virtual base.
3. It parks application processors and exposes a per-CPU `goto_address`, so A1 can own the AP
   takeover sequence instead of writing an INIT-SIPI-SIPI trampoline first.
4. It provides RSDP, EFI system table, EFI memory map, TSC frequency and framebuffer responses
   behind explicit feature requests.
5. The base revision is a versioned contract. The kernel can refuse to boot when the loaded
   revision does not match the requested revision.

A1's draft contract already states that the kernel must not guess: it must check the base
revision tag and fail closed. That rule is independent of the bootloader choice and should
survive any future change.

## 4. Pinned inputs

A1's source record pins the following. These values are inputs to the decision, not a claim
that A4 has rebuilt them.

| Input | Pin | SHA-256 |
| --- | --- | --- |
| Limine protocol | `877c2d4c50e170575f8b8e7fbf7b8fa89b1dc157` | `PROTOCOL.md` `3e702cd9…`; `limine.h` `bd2fa78d…` |
| Limine bootloader | v12.11.1 | `limine-binary.tar.xz` `49331262…`; `BOOTX64.EFI` `4ddecdc6…`; `limine-bios.sys` `dfa6878a…` |
| Base revision | 6 | `SUPPORTED_BASE_REVISION 6` in the v12.11.1 source |

The full hashes and the source citations are in A1 `docs/sources.md` at A1 `2693a34`.
The A1 boot contract is `docs/boot-contract.md`; the canonical structure is
`include/ozero/boot.h`.

## 5. Compatibility implications

| Area | Implication |
| --- | --- |
| Firmware | UEFI is the supported path. A hybrid image may be produced for test convenience, but kernel behaviour may not depend on firmware type beyond RSDP/EFI-table presence. |
| Architecture | x86-64 long mode only. No 32-bit or legacy BIOS guarantee is created by this decision. |
| Kernel image | Non-relocatable higher-half image; the kernel must be linked at the address the protocol requires. |
| Secure Boot | Not claimed. This package does not establish a signed boot chain or a Secure Boot policy. |
| Trust boundary | Limine is in the TCB. The version, hashes and host-tool build must be pinned and reviewed. |
| A3 platform data | A3 consumes A1's boot/platform data. A3 does not own the bootloader decision and must not start AHCI or DMA work because of it. |
| A4 harness | The MBR fixture/harness remains protocol-neutral. A Limine job is a separate future target; CI must not imply that the MBR smoke test is kernel boot. |
| A2 | No dependency. PE fixture parsing is user-mode static analysis and does not consume the boot protocol. |
| Windows compatibility | No effect at this stage. A boot protocol choice is not a Windows loader or driver claim. |

## 6. Required approvals

| Approval | Who | What must be recorded |
| --- | --- | --- |
| D-005 decision | Project Director | Accept Option A, reject it, or defer with a named alternative and date |
| Boot contract | A1 | IF-001 revision, entry-state assertions, boot-info structure and failure policy |
| Harness impact | A4 | Separate `future-limine-kernel-boot` target; no change to the MBR target label |
| Platform consumption | A3 | Confirmation that IF-008/IF-009 do not gain a bootloader dependency |
| Review gate | A1 + A4 | Limine host-tool build and pinned-artifact verification before the first boot test |

D-023 (build the Limine host tool from the pinned source tarball) is related and remains
Proposed. D-005 must not be recorded as Approved merely because A1 recommended Limine.

## 7. Exit criteria for the decision

If the Director accepts Option A, the next measurable milestone is a QEMU UEFI boot test that
proves all of the following:

1. The loaded base revision equals the requested revision; a mismatch fails closed.
2. The kernel reaches the entry stub and prints the revision and firmware type on serial.
3. The copied boot information contains a sorted memory map, HHDM offset, kernel physical and
   virtual base, CPU list and RSDP pointer.
4. The kernel installs its own GDT, IDT and stack before it releases any bootloader-reclaimable
   page.
5. The test record names the QEMU version, machine type, firmware, Limine artifact hashes and
   the exact kernel commit.

Until that test exists, `future-limine-kernel-boot` stays **Not implemented** and XB-004 stays
**Open**.

## 8. What this package does not do

- It does not approve D-005, D-006, D-007, D-021 or D-023.
- It does not add Limine to CI, the build, or the repository tree.
- It does not change the MBR harness or claim kernel boot.
- It does not start AHCI, DMA, GPU or hardware work.
- It does not make Stage 0 accepted.

## Sources

- A1 `docs/boot-contract.md` and `docs/sources.md` at `origin/a1/stage0-review` `2693a34`.
- A1 `include/ozero/boot.h` at `2693a34`.
- A4 [`ADR-0003`](../adr/0003-stage0-build-and-qemu-harness.md), [`test-targets.md`](../process/test-targets.md) and [`interfaces.md`](../architecture/interfaces.md) IF-001.
- A3 `docs/05-hardware-test-plan.md` at `48d434f`, for the separation between boot, L1 and hardware tests.
- Limine protocol and bootloader pins recorded in A1 `docs/sources.md`; no external download was performed for this package.
