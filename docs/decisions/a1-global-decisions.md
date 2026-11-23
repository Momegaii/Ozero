# A1 proposals for the global decision register

| Field | Value |
|---|---|
| Owner | A1 (Kernel Engineering) |
| Status | Proposed; no approval implied |
| Approver | Project Director |
| Canonical register | `docs/decisions/decision-log.md`, owned by A4 |
| Date | 23 November 2026 |

A4 owns the canonical register. This document records A1's proposals in the global ID space
so that reports and cross-agent documents can cite one ID. A4 must merge the accepted rows
into `docs/decisions/decision-log.md`; A1 does not edit A4's register directly.

A1 proposes the block `D-009` through `D-030`. `D-005` and `D-007` already exist in A4's
register and are reused here. If another agent has claimed an ID in this block, A4 renumbers
the A1 rows before merging and keeps the mapping below.

## New decisions from this review

| ID | Decision | Status | Owner | Approver | Link |
|---|---|---|---|---|---|
| D-009 | `include/ozero/` is the only public header root. `include/o0/` is removed, with no compatibility shim before 1.0. Symbols keep the `o0_` prefix. A2/A3 include `<ozero/...>`. | Proposed | A1 | Director | this document; `tools/check-headers.sh` |
| D-010 | A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`. A4 owns CI integration and invokes the script unmodified. A3 owns other content under `tools/`. Changes to header rules need A1 review; changes to CI invocation need A4 review. | Proposed | A1 + A4 | Director | this document; `AGENTS.md` |
| D-011 | A2's process, address-space, handle and syscall requirements are accepted as input to the Stage 7 contract. A1 provides kernel primitives; A2 owns Windows semantics. No ABI is frozen. Conditions are in `docs/a2-interface-review.md`. | Proposed | A1 + A2 | Director | `docs/a2-interface-review.md` |

## Required A4 control-plane edits for D-010

A4's `CODEOWNERS` assigns `/tools/` to A3. Add these lines **after** the `/tools/` line so
the last match wins:

```
/tools/check-headers.sh              @ozero/a1-kernel @ozero/a4-integration
/tests/abi_static_asserts.c          @ozero/a1-kernel @ozero/a4-integration
```

The `/tools/` rule stays for A3's other tools. The `/tests/` rule stays for A4's
cross-cutting tests.

## A1 architecture decisions mapped to global IDs

The local IDs in `docs/kernel-architecture.md` remain for readability. Reports and
cross-agent documents use the global IDs.

| Local | Global | Decision | Status |
|---|---|---|---|
| D1 | D-005 | Boot path: UEFI + Limine base revision 6 | Proposed (A4 register) |
| D2 | D-012 | Paging: 4-level default, 5-level opt-in | Proposed |
| D3 | D-013 | Kernel placement and direct map: fixed image base, discovered HHDM | Proposed |
| D4 | D-014 | Interrupt model: APIC/IOAPIC, legacy PIC masked | Proposed |
| D5 | D-015 | SMP: Limine MP feature, kernel-controlled AP takeover | Proposed |
| D6 | D-016 | ACPI: RSDP + MADT/FADT only in Stage 1 | Proposed |
| D7 | D-017 | EFI runtime services: record only, do not use | Proposed |
| D8 | D-018 | Syscall entry: trap gate first, `SYSCALL` fast path second | Proposed |
| D9 | D-019 | Object and handle model: opaque, generation-tagged, per-process | Proposed |
| D10 | D-020 | DMA is constraint-driven, never device-class-driven | Proposed |
| D11 | D-021 | IOMMU policy: trusted ring-0 only in Stage 1; blocker recorded | Proposed |
| D12 | D-022 | SDM citations: 2018 numbered sections plus titles | Proposed |
| D13 | D-007 | A3 DMA/platform interface and A1 sign-off gate | Proposed (A4 register) |
| D14 | D-023 | Limine host tool: build from the pinned source tarball | Proposed |

## A2 contract sub-decisions

These refine D-011. Each one is independently reviewable.

| ID | Decision | Status | Owner | Approver |
|---|---|---|---|---|
| D-024 | Exception record shape: the kernel delivers a neutral `o0_exception_record` and `o0_context`; A2 translates to Windows `EXCEPTION_RECORD` and `CONTEXT`. | Proposed | A1 | Director |
| D-025 | TEB ownership: A2/userland owns the TEB layout and allocation; A1 provides a per-thread user pointer and GS/FS base control. | Proposed | A1 + A2 | Director |
| D-026 | Address-space layout and ASLR: A2 owns the user policy; A1 provides address-space create/map/protect/query and keeps the kernel half fixed. | Proposed | A1 + A2 | Director |
| D-027 | APC delivery: A1 defines the return-to-user and alertable-wait delivery points; A2 owns wait and alert semantics. | Proposed | A1 + A2 | Director |
| D-028 | Handle inheritance: A1 provides inherit flags and an explicit inherit list at process creation; A2 chooses the policy. | Proposed | A1 + A2 | Director |
| D-029 | Clock: A1 provides a monotonic nanosecond clock; resolution is measured, not promised; A2 maps it to Windows counters. | Proposed | A1 + A2 | Director |
| D-030 | 32-bit and WOW64 are outside A1 Stage 1 scope; the decision is deferred until the x86 user-mode milestone. | Proposed | A1 + A2 | Director |

## Approval rule

A row leaves **Proposed** only when the Director records a date and a link. A1 citing an ID
in a report is not approval. Until then, A1 treats every row as a proposal.
