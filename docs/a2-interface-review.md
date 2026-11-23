# A1 review of A2's kernel-facing requirements

| Field | Value |
|---|---|
| Reviewer | A1 (Kernel Engineering) |
| Subject | `a2/docs/03-a1-interface-requirements.md` |
| A2 branch | `agent/a2-stage0` |
| A2 commit reviewed | `f324b28d943c34adc27a307f44c4e4b248fe3410` |
| A2 package revision stated by A2 | `8bac650` (import); evidence regenerated at `f324b28` |
| Date | 23 November 2026 |
| Status | **Reviewed with conditions. Not an ABI freeze.** A1-REV-01…12 answered separately. |
| Global decisions | D-011, D-018, D-019, D-024 through D-030 |

> **Superseded for A1-REV-01…12.** A2's current review request is answered in
> `docs/a2-contract-review-response.md` against A2 `d02598a` and evidence head
> `3f78477`, with A1 headers at `30ee4f0`. This document remains the
> requirements-group review from A2 `f324b28`; its A2 revision is stale.

## 1. Scope and method

This is a desk review of A2's requirements table against A1's Stage 0 header sketch and
contracts. No A2 or A1 code was executed. A2's own evidence is static, level L0, and does
not demonstrate runtime compatibility. This review therefore accepts requirements, not
claims.

A1 owns the kernel-facing primitives. A2 owns the Windows semantics, the PE loader, the
`ntdll` equivalent and the translation layer. The boundary is a stable ABI, not a shared
implementation.

## 2. Overall disposition

A1 accepts A2's requirements as input to the Stage 7 user-mode contract, with the
conditions below. Nothing here is frozen. The P0 rows become acceptance tests only when
the corresponding A1 interface exists and can be exercised; at Stage 0 no A1 interface has
been exercised.

Three groups are not kernel work:

- Registry, tokens/SIDs, console policy and ALPC semantics are A2/userland services.
- Unwind data and Windows `CONTEXT` translation are A2's.
- ASLR ranges, image-base policy, TEB layout and handle-inheritance policy are A2's.

A1 provides the primitives those services need: address spaces, threads, handles, waitable
objects, device I/O and a stable syscall entry.

## 3. Requirement disposition

| A2 IDs | Requirement | A1 disposition | Anchor |
|---|---|---|---|
| A1-PROC-1..3 | Process create, terminate, query | Accepted as requirement. A1 will extend process creation to take an image-section handle and a parameter block. Exit codes and query classes are later additions. | D-011, D-019 |
| A1-THR-1..5 | Thread create, terminate/suspend/resume, context, APC, TEB | Accepted as requirement. A1 has create, exit, join, user pointer and GS/FS base. Context get/set, suspend/resume and the APC queue are later additions. A2 owns the TEB. | D-011, D-025, D-027 |
| A1-MEM-1..7 | Reserve/commit, protect, query, sections, map view, ASLR, stack | Accepted as requirement. A1 has address-space create/map/protect/unmap. Reserve/commit, section objects, query, ASLR and stack growth are later additions. A2 owns the user layout policy. | D-011, D-026 |
| A1-OBJ-1..4 | Handle table, wait, names, query | Accepted as requirement. A1 has opaque handles, rights, close, duplicate and wait. Inheritance, names and query are later additions. | D-011, D-019, D-028 |
| A1-SYNC-1..4 | Events, mutants, semaphores, timers, wait-multiple, keyed events, condition variables | Accepted as requirement. A1 provides waitable objects and a timeout. Wait-multiple and keyed events are later. Condition variables may live in userland. | D-011 |
| A1-IO-1..6 | Files, read/write, async I/O, directory, file information, device I/O | Accepted as requirement. File-system and directory semantics are A2/A3. A1 provides the generic `o0_io_request` device path. | D-011, D-020 |
| A1-REG-1..2 | Registry | A2-owned userland service. A1 provides file and object primitives only. | D-011 |
| A1-EXC-1..3, A1-DBG-1 | Exception delivery, unwind, continue, debug events | Accepted as requirement. The kernel delivers a neutral record; A2 translates. Unwind data stays in A2. Debug events are later. | D-024 |
| A1-SYS-1..2 | Stable user-to-kernel ABI and return path | Accepted. A1 owns the syscall table and return path. Trap gate first; no host syscall numbers. | D-018 |
| A1-TIME-1..2 | Monotonic clock and timers | Accepted. A1 provides a monotonic nanosecond clock and timers. Resolution is measured, not promised. | D-029 |
| A1-SEC-1 | Tokens and SIDs | A2-owned. A1 provides handle-based access checks only. | D-011 |
| A1-IPC-1 | ALPC-style message ports | A2-owned on top of A1 channels. A1 provides the transport primitives. | D-011 |
| A1-CON-1 | Console | A2/A3-owned. A1 provides device I/O. | D-011 |
| A1-CPU-1 | GS/FS base control | Accepted. A1 adds per-thread user pointer and GS/FS base primitives in this review. | D-025 |

## 4. Conditions

### 4.1 Exception ABI (D-024)

A2 prefers a Windows-shaped `EXCEPTION_RECORD` and `CONTEXT` from the kernel. A1 does not
accept Windows structures in public kernel headers. The kernel delivers a neutral
`o0_exception_record` and `o0_context`; A2 translates them into the Windows shapes. This
keeps the kernel independent of the Windows ABI and lets A2 change with the Windows
baseline.

### 4.2 TEB ownership (D-025)

A2 allocates and lays out the TEB, including the self-pointer and TLS slots. A1 provides
`o0_thread_set_user_pointer`, `o0_thread_user_pointer`, `o0_thread_set_gs_base` and
`o0_thread_set_fs_base`. The kernel never parses the TEB.

### 4.3 Address-space layout and ASLR (D-026)

A1 provides address-space creation, mapping, protection and unmapping. The kernel half is
fixed; the user half is A2's policy. A1 will not hardcode ASLR ranges or image-base rules.
A2 must publish the layout it expects before A1 freezes any user address-space constant.

### 4.4 APC delivery (D-027)

A1 will provide a per-thread user APC queue. The delivery points are return-to-user and an
alertable wait. A2 owns the wait and alert semantics, including the order of APC delivery
relative to wait completion. A1 will not encode Win32 wait rules in the kernel.

### 4.5 Handle inheritance (D-028)

A1 provides an inherit flag on handles and an explicit inherit list at process creation.
A2 chooses whether `CreateProcess` inherits all inheritable handles or only a list. The
handle value remains opaque and generation-tagged.

### 4.6 Clock and timers (D-029)

A1 provides a monotonic nanosecond clock. Resolution is whatever the platform provides and
is measured in the Stage 1 test envelope; A1 does not promise sub-millisecond resolution
before measuring it. A2 maps the clock to `QueryPerformanceCounter` and wait timeouts.

### 4.7 Syscall ABI (D-018)

A1 owns a numbered syscall table and the return path. The first implementation uses a trap
gate; the `SYSCALL` fast path follows. A2 owns its `ntdll` equivalent and must not depend on
host syscall numbers or on a specific instruction. Argument validation and user-pointer
checks are A1's.

### 4.8 32-bit and WOW64 (D-030)

A1 Stage 1 is x86-64 only. A2's x86 timing question is deferred until the x86 user-mode
milestone. A1 makes no 32-bit compatibility claim.

## 5. What A1 will not accept

- Windows structures or TEB layout in public kernel headers.
- A specific kernel object layout, scheduler or allocator in the A2 contract.
- Hardcoded user ASLR ranges or image-base policy in the kernel.
- A specific syscall instruction as part of the ABI.
- Registry, token, console or ALPC semantics implemented in the kernel.
- Any claim that A2's static evidence is runtime compatibility.

## 6. Open items for A2

| Item | Needed from A2 | Effect |
|---|---|---|
| Exception shape | Accept the neutral record and own the translation, or escalate D-024 | Blocks the exception ABI |
| TEB layout | Publish the TEB layout and TLS slot rules | Blocks A1's GS/FS programming tests |
| Address-space policy | Publish ASLR ranges, image-base rules and stack/heap placement | Blocks A1 user address-space constants |
| APC points | Confirm return-to-user and alertable-wait are sufficient | Blocks the APC queue interface |
| Handle inheritance | Choose inherit-all or an explicit list | Blocks process-creation parameters |
| Clock resolution | State the minimum acceptable resolution | Blocks the timer acceptance test |
| 32-bit scope | Confirm deferral, or escalate D-030 | Blocks any x86 ABI work |

## 7. Sign-off

A1 has reviewed A2's requirements against the Stage 0 sketch and accepts them as input with
the conditions above. This is not an ABI freeze and not an implementation commitment.
A2's reply should reference the global decision IDs, not A1's local D1–D14 numbers.

## 8. Sources

- A2, `a2/docs/03-a1-interface-requirements.md`, branch `agent/a2-stage0`, commit `f324b28`.
- A2, `a2/docs/11-stage0-review-response.md`, same commit.
- A1, `include/ozero/thread.h`, `ipc.h`, `mm.h`, `driver.h`, `irq.h`, `cpu.h`.
- A1, `docs/interface-draft.md` and `docs/kernel-architecture.md`.
