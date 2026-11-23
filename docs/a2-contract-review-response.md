# A1 answers to A2's contract review (A1-REV-01…12)

| Field | Value |
|---|---|
| Reviewer | A1 (kernel engineering) |
| Subject | A2, `a2/docs/12-a1-contract-review.md`, questions A1-REV-01…12 |
| A2 code revision | `50b78534f6b52af414f4ae5bc6e190c4427beba9` |
| A2 evidence head | `807c447a175eba27624f9c50c9b2e676e95e1f20` |
| A1 header commit | `30ee4f0da3c3d4263033680d39e7937fcb59f42f` |
| Date | 23 November 2026 |
| Status | **Answered. A2 acceptance pending. XB-002 is not closed by A1 alone.** |
| Related | D-011, D-018, D-019, D-024 through D-030 |

## 1. Basis and stale references

A2's review document at `50b7853` cites A1 `90b004f` and `include/o0/`. That is
stale:

- A1's current public root is `include/ozero/` at `30ee4f0` (D-009, approved by
  A4 at `245007d`).
- A1's current header set includes the thread user-pointer and GS/FS primitives
  added at `164360e` and the DMA callback table added at `164360e`.
- A2's fixture evidence is regenerated at `807c447`, so the A2 revision pair is
  `50b7853` (code/docs) plus `807c447` (evidence).

A2 must update its review document before closing XB-002. This is an A2 action,
not an A1 answer.

A2's "Global IDs" table also assigns D-011, D-012 and D-020 to A2-local
questions (syscall strategy, own `ntdll`, security model). Those IDs are already
taken in the canonical register: D-011 is the A2 contract, D-012 is paging and
D-020 is the DMA model. A2 must rename its local questions (for example
`A2-Q3`, `A2-Q4`, `A2-Q12`) or use canonical IDs. A1 will not answer against a
colliding ID space.

Decision vocabulary:

- **Accept** — A1's current draft already satisfies the requirement.
- **Change** — A1 will add or change a primitive; the header and milestone are named.
- **Defer** — A2 builds it above the kernel, or it moves to a later milestone.

## 2. Summary

| ID | Topic | Decision | Owner | Exact consequence | Milestone |
|---|---|---|---|---|---|
| A1-REV-01 | Process from an image section | **Change** | A1 primitive; A2 PEB/TEB and loader policy | Add `o0_process_create_from_section` to `include/ozero/thread.h` (or a future `process.h`) | Stage 7 |
| A1-REV-02 | User APCs | **Change** | A1 queue/return hook; A2 dispatch and wait policy | Add per-thread APC queue and alertable-wait hooks to `thread.h` | Stage 7 |
| A1-REV-03 | TEB and segment base | **Accept** | A2 TEB; A1 GS/FS programming | No change; `thread.h` at `30ee4f0` already has the four functions | Now |
| A1-REV-04 | Section objects and image mapping | **Change** | A1 section object; A2 image policy | Add section create/map-view to `mm.h`/`ipc.h` | Stage 7 |
| A1-REV-05 | Address-space layout and ASLR | **Defer** | A2 policy; A1 kernel half | A2 publishes the layout; A1 may add `O0_USER_*` constants later | Stage 7 |
| A1-REV-06 | Handle table and NT access masks | **Accept** | A2 NT table and masks; A1 handle rights | No change; `ipc.h` handles stay opaque | Now |
| A1-REV-07 | Wait-for-multiple and alertable waits | **Change** | A1 primitive; A2 semantics | Add `o0_handle_wait_many` and alertable wait to `ipc.h` | Stage 7 |
| A1-REV-08 | Exception delivery format | **Change** | A1 neutral record; A2 Windows translation and dispatch | Add a neutral `include/ozero/exception.h` | Stage 7 |
| A1-REV-09 | Unwind data access | **Defer** | A2 implements unwind; A1 mapping lifetime | No A1 unwind API; A2 keeps `.pdata` mapped | Stage 7 |
| A1-REV-10 | Syscall boundary ownership | **Accept** | A1 entry/table; A2 `ntdll` and user ABI | No change now; A1 publishes a table version | Stage 7 |
| A1-REV-11 | Clock and timeout semantics | **Change** | A1 clock and absolute timers; A2 mapping | Add `o0_clock_now_ns` and absolute wait/timer API | Stage 7 |
| A1-REV-12 | Security model split | **Accept** | A2 tokens/SIDs; A1 handle rights and metadata | No kernel token enforcement; opaque cookie only if requested | Stage 7 |

Counts: **4 Accept, 6 Change, 2 Defer.**

## 3. Item-by-item answers

### A1-REV-01 — Process from an image section: Change

Current A1 (`thread.h` at `30ee4f0`) has:

```c
o0_status o0_process_create(const char *name, o0_as_t *as, o0_process_t **out);
o0_status o0_thread_create(o0_process_t *proc, const struct o0_thread_attr *attr,
                           o0_thread_t **out);
```

There is no image-section-to-process primitive. A2 can compose the pieces today:
create an address space with `o0_as_create`, map the image with `o0_as_map`, then
create the first thread. A2 allocates the PEB and the initial TEB; A1 provides
the process, address space and thread primitives.

A1 will add `o0_process_create_from_section(section, params, &process, &thread)`
at Stage 7, with the section handle and parameter block defined then. No
`30ee4f0` header changes. Owner: A1 for the primitive; A2 for PEB/TEB
allocation and loader policy. See D-011 and D-025.

### A1-REV-02 — User APCs: Change

A1's draft excludes an APC model. D-027 assigns A1 the per-thread user APC queue
and the delivery points (return-to-user and alertable wait); A2 owns the wait and
alert semantics and the user-mode dispatch loop.

A1 will add queue and alert hooks to `thread.h` at Stage 7. The exact names are
not frozen; the contract is "queue a user APC, deliver at the two defined
points." No `30ee4f0` header change. Owner: A1 kernel queue/return hook; A2
dispatch and wait policy.

### A1-REV-03 — TEB and segment base: Accept

`thread.h` at `30ee4f0` already provides:

```c
o0_status o0_thread_set_user_pointer(o0_thread_t *thread, void *ptr);
void *o0_thread_user_pointer(const o0_thread_t *thread);
o0_status o0_thread_set_gs_base(o0_thread_t *thread, uint64_t base);
o0_status o0_thread_set_fs_base(o0_thread_t *thread, uint64_t base);
```

A2 allocates the TEB and supplies the base; A1 stores the user pointer and
programs GS/FS on context switch. The kernel never parses the TEB. No header
change. Owner: A2 TEB; A1 segment-base programming. See D-025.

### A1-REV-04 — Section objects and image mapping: Change

`ipc.h` defines `O0_OBJ_SECTION` as an object type, but `mm.h` has only
`o0_as_map`/`o0_as_unmap` over physical ranges. There is no file-backed section,
named section or map-view primitive.

A1 will add section objects (file-backed and pagefile-backed) and a map-view
operation at Stage 7. A2 owns image mapping policy, naming and copy-on-write
semantics. A2 can use `o0_as_map` over a shared physical range today. Exact
consequence: new section create/map-view declarations in `mm.h` and `ipc.h`;
not in `30ee4f0`. Owner: A1 kernel section object; A2 image policy.

### A1-REV-05 — Address-space layout and ASLR: Defer

D-026 makes the user half A2's policy. A1 publishes the fixed kernel half and
the HHDM constraints; A1 will not hardcode ASLR ranges or image-base rules. A2
must publish the layout it expects (kernel/user split, image range, stack, heap,
TEB/PEB placement) before A1 freezes any user address-space constant.

A1 may add `O0_USER_MIN`/`O0_USER_MAX` or a layout query at Stage 7, after A2's
layout is published. No `30ee4f0` header change. Owner: A2 policy; A1 kernel
half.

### A1-REV-06 — Handle table and NT access masks: Accept

A2 may maintain its own NT handle table whose values map to A1's opaque handles.
A1 keeps the 64-bit generation-tagged handle and the A1 rights set in `ipc.h`;
A1 does not carry NT access masks. A2 enforces NT access checks and maps A1
status to NTSTATUS/Win32.

No `30ee4f0` header change. Owner: A2 NT handle table, masks and error
translation; A1 A1-handle rights. See D-019.

### A1-REV-07 — Wait-for-multiple and alertable waits: Change

`ipc.h` at `30ee4f0` has only `o0_handle_wait(handle, timeout_ns)`. D-011 lists
wait-multiple as a later addition. A1 will add wait-any/wait-all and an
alertable wait at Stage 7. Until then, A2 may loop over single waits, but
lowest-index return and fairness are A2's responsibility.

Exact consequence: add `o0_handle_wait_many` and an alertable flag/entry to
`ipc.h`; not in `30ee4f0`. Owner: A1 primitive; A2 semantics.

### A1-REV-08 — Exception delivery format: Change

D-024 is explicit: A1 delivers a neutral `o0_exception_record` and
`o0_context`; A2 translates them into the Windows `EXCEPTION_RECORD` and
`CONTEXT`. A1 owns the kernel delivery path and the return-to-user transition;
A2 owns the user-mode dispatch loop and SEH semantics.

Exact consequence: add `include/ozero/exception.h` with the neutral structures
at Stage 7. No Windows structures in A1 headers at `30ee4f0`. Owner: A1 neutral
record/delivery; A2 translation and dispatch.

### A1-REV-09 — Unwind data access: Defer

A2 implements the `RtlVirtualUnwind`-equivalent in user mode, reading the mapped
image. A1 provides no unwind service. A1 does not unmap pages behind A2's back;
the address-space owner controls mapping lifetime. A2 must keep `.pdata` mapped
and readable for as long as it needs unwind data.

No A1 header change. Owner: A2 unwind implementation and `.pdata` lifetime;
A1 `mm.h` mapping primitives.

### A1-REV-10 — Syscall boundary ownership: Accept

D-018: A1 owns the numbered syscall table and the kernel entry/return path. A2
owns its `ntdll`-equivalent and the user-visible ABI. A1 does not depend on
Windows syscall numbers, and A2 must not either. A1 publishes the syscall table
version; A2 owns the user-side stub and its versioning.

No `30ee4f0` header change; the syscall ABI is deliberately unfrozen. Owner: A1
kernel entry/table; A2 user ABI/`ntdll`.

### A1-REV-11 — Clock and timeout semantics: Change

D-029: A1 provides a monotonic nanosecond clock; resolution is measured, not
promised. `o0_handle_wait(timeout_ns)` is relative only. A2 needs absolute
deadlines for timers and `QueryPerformanceCounter`/`GetTickCount64` agreement.

A1 will add `o0_clock_now_ns` and absolute wait/timer primitives at Stage 7.
Exact consequence: new declarations in `mm.h`/`thread.h`; not in `30ee4f0`.
Owner: A1 clock/timers; A2 mapping to Windows time APIs.

### A1-REV-12 — Security model split: Accept

A2 owns tokens, SIDs, integrity levels and NT access checks. A1 stores the
object type, rights and generation, and enforces A1 handle rights. A1 does not
enforce NT access masks or tokens.

If A2 needs an opaque per-object security cookie, A1 can add one at Stage 7; it
is not in `30ee4f0`. No kernel token enforcement. Owner: A2 security policy; A1
handle rights and object metadata.

## 4. Actions for A2

1. Update `a2/docs/12-a1-contract-review.md` references from A1 `90b004f` /
   `include/o0/` to A1 `30ee4f0` / `include/ozero/`.
2. Rename A2-local D-011/D-012/D-020 to `A2-Q3`/`A2-Q4`/`A2-Q12` or canonical
   register IDs.
3. Record A1's decision for each of A1-REV-01…12 and accept, contest or defer it.
4. Publish the address-space layout A2 expects (D-026) before A1 freezes user
   constants.
5. Confirm the APC delivery points (D-027), handle inheritance policy (D-028),
   clock resolution (D-029) and 32-bit deferral (D-030).
6. Publish IF-009 (see `docs/if009-review.md`).

A1 does not close XB-002 on its own. XB-002 closes when A2 accepts these answers
and records the A2 revision that does so.

## 5. Sources

- A2, `a2/docs/12-a1-contract-review.md`, `a2/docs/03-a1-interface-requirements.md`,
  `a2/docs/09-open-questions.md`, branch `agent/a2-stage0`, commit `50b7853`;
  evidence `807c447`.
- A1, `include/ozero/thread.h`, `ipc.h`, `mm.h`, `driver.h`,
  `docs/interface-draft.md` §5–§8, `docs/kernel-architecture.md` §6, commit
  `30ee4f0`.
- A4, `docs/decisions/decision-log.md`, commit `245007d`.
