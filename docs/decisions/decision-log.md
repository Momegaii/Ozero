# Decision register

| Field | Value |
| --- | --- |
| Owner | A4 |
| Approver | Project Director |
| Entry format | [`docs/templates/decision-log.md`](../templates/decision-log.md) |
| ADR index | [`docs/adr/README.md`](../adr/README.md) |

The register is the authoritative list of open decisions. A decision is **Approved** only when the
named Director approver records it with a date and a link. Silence is not approval.

## Status vocabulary

| Status | Meaning | Who may set it |
| --- | --- | --- |
| **Proposed** | Written down, awaiting a decision | A4 or the decision owner |
| **Approved** | Director accepted it, with a date and a link | Project Director |
| **Rejected** | Director declined it, with a reason | Project Director |
| **Deferred** | Director postponed it to a stated date or trigger | Project Director |

ADR status uses the same words. An ADR marked "Accepted" in older text maps to **Approved** here;
the register wins.

## Register

| ID | Decision | Status | Owner | Director approver | Raised | Needed by | Link |
| --- | --- | --- | --- | --- | --- | --- | --- |
| D-001 | Confirm the A1/A2/A3 domain split and GitHub team slugs | **Proposed** | A4 | Project Director | 2026-11-23 | Before Stage 1 | ADR-0001 |
| D-002 | Approve branch isolation and the review gate, then protect `main` | **Proposed** | A4 | Project Director | 2026-11-23 | Before Stage 1 | ADR-0002 |
| D-003 | Approve the Stage 0 build and QEMU MBR harness approach | **Proposed** | A4 | Project Director | 2026-11-23 | Stage 0 review | ADR-0003 |
| D-004 | Approve agent trust boundaries and workspace profiles | **Proposed** | A4 | Project Director | 2026-11-23 | Before Stage 1 | ADR-0004 |
| D-005 | Choose the boot protocol and kernel handoff (Limine is a candidate only) | **Proposed** | A1 | Project Director | 2026-11-23 | Stage 1 design | IF-001 |
| D-006 | Accept Stage 0 | **Proposed** | A4 | Project Director | 2026-11-23 | Stage 0 exit | `docs/templates/stage-acceptance.md` |
| D-007 | Approve the A3 DMA/platform interface and the required A1 sign-off gate | **Proposed** | A1 + A3 | Project Director | 2026-11-23 | Before A3 DMA work | IF-008 |
| D-008 | Approve publishing the A4 Stage 0 branch for independent verification | **Approved** | A4 | Project Director | 2026-11-23 | Stage 0 review | [#3](https://github.com/Momegaii/Ozero/pull/3), `evidence/stage0-review/director-review-2026-11-23.md` |
| D-009 | Adopt `include/ozero/` as the only public header root; remove `include/o0/`; keep `o0_` symbol prefixes; A2/A3 include `<ozero/...>` | **Approved** | A1 + A4 | Project Director | 2026-11-23 | Before A1 merge | A1 `03f8883`, `docs/architecture/ownership.md` |
| D-010 | A1 owns `tools/check-headers.sh` and `tests/abi_static_asserts.c`; A4 owns CI integration; A3 owns other `tools/`; file-level `CODEOWNERS` overrides | **Approved** | A1 + A4 | Project Director | 2026-11-23 | Before A1 merge | `CODEOWNERS` |
| D-011 | Accept A2's process, address-space, handle and syscall requirements as Stage 7 input; no ABI freeze | **Proposed** | A1 + A2 | Project Director | 2026-11-23 | Before Stage 7 | `docs/a2-interface-review.md` (A1 branch) |
| D-012 | Paging: 4-level default, 5-level opt-in | **Proposed** | A1 | Project Director | 2026-11-23 | Stage 1 design | A1 `docs/kernel-architecture.md` |
| D-013 | Kernel placement and direct map: fixed image base, discovered HHDM | **Proposed** | A1 | Project Director | 2026-11-23 | Stage 1 design | A1 `docs/kernel-architecture.md` |
| D-014 | Interrupt model: APIC/IOAPIC, legacy PIC masked | **Proposed** | A1 | Project Director | 2026-11-23 | Stage 1 design | A1 `docs/kernel-architecture.md` |
| D-015 | SMP: Limine MP feature, kernel-controlled AP takeover | **Proposed** | A1 | Project Director | 2026-11-23 | Stage 1 design | A1 `docs/kernel-architecture.md` |
| D-016 | ACPI: RSDP plus MADT/FADT only in Stage 1 | **Proposed** | A1 | Project Director | 2026-11-23 | Stage 1 design | A1 `docs/kernel-architecture.md` |
| D-017 | EFI runtime services: record only, do not use | **Proposed** | A1 | Project Director | 2026-11-23 | Stage 1 design | A1 `docs/kernel-architecture.md` |
| D-018 | Syscall entry: trap gate first, `SYSCALL` fast path second | **Proposed** | A1 | Project Director | 2026-11-23 | Before syscall work | A1 `docs/kernel-architecture.md` |
| D-019 | Object and handle model: opaque, generation-tagged, per-process | **Proposed** | A1 | Project Director | 2026-11-23 | Before handle work | A1 `docs/kernel-architecture.md` |
| D-020 | DMA is constraint-driven, never device-class-driven | **Proposed** | A1 | Project Director | 2026-11-23 | Before DMA work | A1 `docs/a3-dma-contract.md` |
| D-021 | IOMMU policy: trusted ring-0 only in Stage 1; blocker recorded | **Proposed** | A1 | Project Director | 2026-11-23 | Before DMA work | A1 `docs/a3-dma-contract.md` |
| D-022 | SDM citations: 2018 numbered sections plus titles | **Proposed** | A1 | Project Director | 2026-11-23 | Stage 0 review | A1 `docs/sources.md` |
| D-023 | Limine host tool: build from the pinned source tarball | **Proposed** | A1 | Project Director | 2026-11-23 | Stage 1 build | A1 `docs/boot-contract.md` |
| D-024 | Exception record shape: neutral kernel record, A2 translates to Windows shapes | **Proposed** | A1 | Project Director | 2026-11-23 | Before Stage 7 | A1 `docs/a2-interface-review.md` |
| D-025 | TEB ownership: A2 owns layout and allocation; A1 provides user pointer and GS/FS base | **Proposed** | A1 + A2 | Project Director | 2026-11-23 | Before Stage 7 | A1 `docs/a2-interface-review.md` |
| D-026 | Address-space layout and ASLR: A2 policy; A1 provides map/protect/query and fixed kernel half | **Proposed** | A1 + A2 | Project Director | 2026-11-23 | Before Stage 7 | A1 `docs/a2-interface-review.md` |
| D-027 | APC delivery: A1 defines return-to-user and alertable-wait points; A2 owns wait semantics | **Proposed** | A1 + A2 | Project Director | 2026-11-23 | Before Stage 7 | A1 `docs/a2-interface-review.md` |
| D-028 | Handle inheritance: A1 provides inherit flags and list; A2 chooses policy | **Proposed** | A1 + A2 | Project Director | 2026-11-23 | Before Stage 7 | A1 `docs/a2-interface-review.md` |
| D-029 | Clock: A1 provides monotonic nanoseconds; resolution measured, not promised | **Proposed** | A1 + A2 | Project Director | 2026-11-23 | Before Stage 7 | A1 `docs/a2-interface-review.md` |
| D-030 | 32-bit and WOW64 outside A1 Stage 1 scope; deferred to the x86 user-mode milestone | **Proposed** | A1 + A2 | Project Director | 2026-11-23 | Deferred | A1 `docs/a2-interface-review.md` |
| D-031 | Enable `main` branch protection and required CI checks | **Proposed** | A4 | Project Director | 2026-11-23 | Before Stage 1 | `evidence/stage0-review/github-settings.txt` |

## Decided

| ID | Decision | Outcome | Date | Approver | Link |
| --- | --- | --- | --- | --- | --- |
| D-008 | Publish the A4 Stage 0 branch | Approved; branch published and PR [#3](https://github.com/Momegaii/Ozero/pull/3) opened | 2026-11-23 | Project Director | `evidence/stage0-review/director-review-2026-11-23.md` |
| D-009 | Canonical public header namespace | Approved: `include/ozero/` only; `include/o0/` removed; A1 `03f8883` implements it | 2026-11-23 | Project Director | `docs/architecture/ownership.md` |
| D-010 | Ownership of `tools/check-headers.sh` and `tests/abi_static_asserts.c` | Approved: A1 owns both; A4 owns CI integration; A3 owns other `tools/` | 2026-11-23 | Project Director | `CODEOWNERS` |

## Rules

- An entry leaves **Proposed** only with a named approver, a date and a link to the approval.
- A **Deferred** entry states the trigger or date that brings it back.
- A **Rejected** entry states the reason, so it is not proposed again unchanged.
- A4 records decisions; A4 does not approve its own architecture or process proposals.
- D-005 belongs to A1. A4 records it because the harness must not pre-empt the boot protocol.
- D-009 and D-010 were resolved under the Director's 2026-11-23 instruction to resolve the namespace
  and ownership conflicts. A1's proposals on the same topics were merged into these rows. A1's
  D-011–D-030 proposals are recorded here but remain **Proposed**. A4's branch-protection proposal
  was renumbered to D-031 because A1's D-011 already uses that ID.
