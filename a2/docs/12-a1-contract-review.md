# A1 review request: process, address-space, handle and syscall contracts

- Status: A1 answered at `74b1471` (`a17d126`); A2 accepts all 12 at `6d305b8`/`64b3458`; XB-002 awaits A4 register update
- Evidence: A1 branch `a1/stage0-review`, answers in `docs/a2-contract-review-response.md` at `74b1471` (review `a17d126`, reconciled to A2 `d02598a`/`3f78477`); headers `include/ozero/{ipc,thread,mm}.h`
- Decisions needed: XB-002 (contract match), A2-Q3 (syscall strategy; related D-018), A2-Q4 (own `ntdll`; related D-011), A2-Q12 (security model; related D-011, D-019)
- Risks: the loader ABI can freeze before the process, address-space and handle contracts are agreed; see [05-risk-report.md](05-risk-report.md)
- Next step: A1 answers A1-REV-01…12 with Accept / Change / Defer, names a commit for any header change, and records sign-off in XB-002

## Why this review is needed

A2's loader requirements in [03-a1-interface-requirements.md](03-a1-interface-requirements.md)
assume kernel services that A1's Stage 0 draft deliberately leaves open. The draft is
explicit that the syscall ABI, handle encoding and Windows semantics are not defined:

> "The syscall ABI is explicitly not defined here." — A1 `docs/interface-draft.md`

That is the correct Stage 0 position. This document asks A1 to confirm which of A2's
requirements the kernel will own, which A2 must build above the kernel, and which are
deferred. It does **not** ask for an ABI freeze and does **not** import NT semantics into
the kernel.

## Non-goals

- No Windows syscall numbers, SSDT layout or `ntdll` stub patching.
- No kernel-side tokens, APC model or NT object manager.
- No frozen handle encoding, PEB/TEB layout or address-space layout.
- No change to A1's "trusted ring-0 only" Stage 1 DMA restriction.

## Requirements under review

The full text is in [03-a1-interface-requirements.md](03-a1-interface-requirements.md).
The groups A1 is asked to rule on:

| Group | A2 IDs | What A2 needs |
| --- | --- | --- |
| Process | A1-PROC-1…3 | create from an image section; terminate with exit code; query image path, command line, environment |
| Thread | A1-THR-1…5 | create with start address and stack; suspend/resume; context get/set; user APCs; TEB and GS/FS base |
| Memory | A1-MEM-1…7 | reserve/commit split; protection and guard pages; sections; map view; deterministic layout; stack growth |
| Objects | A1-OBJ-1…4 | typed handles, access masks, inheritance, duplication; wait; names; query |
| Sync | A1-SYNC-1…4 | events/mutants/semaphores/timers; wait-any/all; keyed events; condition variables |
| Exceptions | A1-EXC-1…2 | Windows-compatible exception record and context; `RtlVirtualUnwind`-compatible unwind |
| Cross-cutting | X1…X10 | stable boundary, NTSTATUS mapping, timeouts, cancellability, no POSIX leakage |

## What A1's draft currently says

| Topic | A1 draft position | Where |
| --- | --- | --- |
| Process | `o0_process_t` owns an address space and a handle table; `o0_thread_t` is a schedulable context in exactly one process | `interface-draft.md` §6 |
| Process creation | No image-section-to-process primitive is described; `o0_thread_create` takes an entry function and stack size | §6 |
| Handles | Opaque 64-bit, process-scoped, rights plus generation counter; stale handle returns `O0_ENOENT`; never a kernel pointer | §7, `ipc.h` |
| Handle operations | open, close, dup, wait; channel create/send/recv; cross-process transfer is an open question | §7, `ipc.h` |
| Waits | `o0_handle_wait` only; wait-for-multiple and alertable waits deferred | §7 |
| Address space | `o0_as_create` shares the kernel half; map/unmap/protect exist; no section object or image mapping described | §5, `mm.h` |
| APCs and TEB | "No Windows thread/process semantics, tokens, or APC model is implied" | §6 |
| Exceptions | Not covered in the interface draft | — |
| Syscall boundary | "The syscall ABI is explicitly not defined here"; final ABI is A2/A4 work | `interface-draft.md`, `kernel-architecture.md` |

## A1 response (`74b1471`, review `a17d126`)

A1 answered against A2 `50b7853`/`807c447`, then reconciled the answers to A2 `d02598a`/`3f78477` without changing the decisions. A2 accepts at `6d305b8`. The disposition column is A2's acceptance.

| ID | A1 decision | Canonical ID | A2 disposition |
| --- | --- | --- | --- |
| A1-REV-01 | **Change required** — add `o0_process_create_from_section` at Stage 7; A2 composes today | D-011 | Accept; A2 owns PEB/TEB and loader policy |
| A1-REV-02 | **Change required** — per-thread APC queue and alertable-wait hooks at Stage 7 | D-027 | Accept; return-to-user and alertable wait are sufficient |
| A1-REV-03 | **Accept** — `thread.h` has user pointer and GS/FS primitives | D-025 | Accept; A2 allocates the TEB |
| A1-REV-04 | **Change required** — section objects and map-view at Stage 7; `o0_as_map` today | D-011 | Accept; A2 owns image mapping policy |
| A1-REV-05 | **Defer** — A2 publishes the user layout; A1 may add `O0_USER_*` later | D-026 | Accept; A2 publishes before A1 freezes constants |
| A1-REV-06 | **Accept** — A2 keeps its NT handle table; A1 handles stay opaque | D-019 | Accept |
| A1-REV-07 | **Change required** — `o0_handle_wait_many` and alertable wait at Stage 7 | D-011 | Accept; A2 loops over single waits until then |
| A1-REV-08 | **Change required** — neutral `o0_exception_record`/`o0_context`; A2 translates | D-024 | Accept |
| A1-REV-09 | **Defer** — A2 implements unwind and keeps `.pdata` mapped | none | Accept |
| A1-REV-10 | **Accept** — A1 owns the syscall table and entry; A2 owns `ntdll` | D-018 | Accept |
| A1-REV-11 | **Change required** — `o0_clock_now_ns` and absolute timers at Stage 7 | D-029 | Accept; 1 µs minimum for wait timeouts |
| A1-REV-12 | **Accept** — A2 owns tokens, SIDs and NT access checks | D-011, D-019 | Accept |

Counts: **4 Accept, 6 Change required, 2 Defer.** A1 does not close XB-002 on its own;
A2 acceptance in this revision is the other half.

A1's condition 3 asked A2 to replace `90b004f`/`include/o0` references and the colliding
D-009…D-023 IDs. Both are resolved at `6d305b8`: references cite `74b1471` and
`include/ozero/`, and A2-local questions are `A2-Q1…A2-Q15`.

## A2 answers to A1's open items

| A1 open item | A2 answer |
| --- | --- |
| Exception shape | Accept D-024: neutral kernel record, A2 translates to Windows shapes |
| TEB layout | A2 publishes the TEB/PEB layout before Stage 7; Stage 0 does not freeze it |
| Address-space policy | A2 publishes ASLR ranges, image range and stack/heap placement before A1 freezes constants |
| APC points | Return-to-user and alertable wait are sufficient; A2 owns dispatch order |
| Handle inheritance | Explicit list (`STARTUPINFOEX`) preferred; inherit-all only when the caller requests it |
| Clock resolution | 1 µs minimum for wait timeouts; `QueryPerformanceCounter` uses the same clock and is measured |
| 32-bit scope | Deferral confirmed (A2-Q2 option B); no x86 ABI work in Stage 1 |

## Questions for A1

Each question names the A2 requirement, the gap in the draft, and the answer A2 can use.
"Accept" means the draft position already satisfies A2; "Change" means A1 will expose or
change a primitive; "Defer" means A2 must build it above the kernel or the requirement
moves to a later milestone.

### A1-REV-01 — Who creates a process from an image section?

- **Requirement:** A1-PROC-1.
- **Gap:** the draft has `o0_process_t`, `o0_as_create` and `o0_thread_create`, but no
  operation that takes a mapped image and returns a process plus initial thread.
- **Question:** does A1 expose `process_create_from_section(section, params)` (or
  equivalent), or does A2 compose it from `o0_as_create` + map + `o0_thread_create`?
  Who allocates the PEB and the initial TEB?
- **Why it matters:** the loader must create a suspended process, map the image, apply
  relocations and only then start the first thread.
- **Acceptable answers:** Accept (name the existing primitive), Change (add a primitive),
  or Defer (A2 owns composition; A1 confirms the pieces are sufficient).

### A1-REV-02 — Are user APCs kernel or A2?

- **Requirement:** A1-THR-4, X4.
- **Gap:** the draft explicitly excludes an APC model, but `SleepEx`,
  `WaitForSingleObjectEx` and thread-pool behaviour depend on alertable waits.
- **Question:** will A1 deliver user APCs at the kernel boundary, or will A2 queue them
  and require only an "alertable wait returns to user" hook? Which component owns the
  transition back to user mode where a pending APC runs?
- **Why it matters:** without a defined owner, APC delivery will be implemented twice or
  not at all, and wait semantics will diverge.

### A1-REV-03 — TEB and segment base ownership

- **Requirement:** A1-THR-5.
- **Gap:** the draft says no Windows thread semantics are implied; it does not say who
  allocates the TEB or sets the GS/FS base on context switch.
- **Question:** does A1 allocate a per-thread TEB-sized region and program the segment
  base, or does A2 supply the base as part of thread creation?
- **Why it matters:** TLS, stack limits and the self-pointer are read by the loader and
  by every CRT start-up path.

### A1-REV-04 — Section objects and image mapping

- **Requirement:** A1-MEM-4, A1-MEM-5.
- **Gap:** `mm.h` exposes address-space map/unmap/protect but the draft does not describe
  a section object, named sections, or an image mapping primitive.
- **Question:** are sections kernel objects (A1) or an A2 abstraction over a shared
  physical range? If kernel objects, does A1 provide file-backed and pagefile-backed
  sections, and what is the naming namespace?
- **Why it matters:** DLL loading, copy-on-write data and shared memory all depend on it.

### A1-REV-05 — Address-space layout contract

- **Requirement:** A1-MEM-6.
- **Gap:** no user/kernel split, image-base range or ASLR policy is stated.
- **Question:** will A1 publish the user-space layout (kernel half, HHDM, user image
  range, stack range) as a contract A2 can rely on, including where a relocated image may
  be placed?
- **Why it matters:** relocation, `DYNAMIC_BASE` and "preferred base taken" behaviour
  need a defined range, not an allocator guess.

### A1-REV-06 — Handle table ownership and NT access masks

- **Requirement:** A1-OBJ-1, X5, X8.
- **Gap:** A1 handles are opaque 64-bit values with a small rights set; A2 needs NT
  access masks, inheritance, duplication across processes and object names.
- **Question:** can A2 maintain its own NT handle table whose values map to A1 handles,
  or does A1 want to carry NT rights and inheritance in the kernel handle? Which side
  enforces access checks?
- **Why it matters:** `DuplicateHandle`, inheritance and access-denied error codes are
  observable; a mismatch becomes a compatibility bug or a security bug.

### A1-REV-07 — Wait-for-multiple and alertable waits

- **Requirement:** A1-OBJ-2, A1-SYNC-2.
- **Gap:** the draft defers wait-for-multiple and alertable waits.
- **Question:** are these A1 primitives, A2 loops over single waits, or deferred past
  M1? If A2 loops, what guarantees fairness and lowest-index semantics?
- **Why it matters:** wait-any with lowest-index return is documented Win32 behaviour.

### A1-REV-08 — Exception delivery format

- **Requirement:** A1-EXC-1.
- **Gap:** the draft does not describe how a hardware fault reaches a user-mode handler.
- **Question:** does A1 deliver a Windows-compatible `EXCEPTION_RECORD` and `CONTEXT`
  (or a neutral form A2 translates), and who owns the user-mode dispatch loop?
- **Why it matters:** the C++ fixture must catch `throw 42`; without a contract the
  exception path is invented twice.

### A1-REV-09 — Unwind data access

- **Requirement:** A1-EXC-2.
- **Gap:** no statement on who reads `.pdata`/`UNWIND_INFO` or implements virtual unwind.
- **Question:** is `RtlVirtualUnwind`-equivalent logic A2 user-mode code reading the
  mapped image, or does A1 expose an unwind service? If A2, does A1 guarantee that
  `.pdata` stays mapped and readable for the process lifetime?
- **Why it matters:** chained unwind entries are already present in real binaries
  ([10-technical-findings.md](10-technical-findings.md) F4); the reader must know where
  the data lives.

### A1-REV-10 — Syscall boundary ownership and stability

- **Requirement:** X1, A2-Q3, A2-Q4.
- **Gap:** A1 says the syscall ABI is not defined and that final ABI is A2/A4 work.
- **Question:** does A1 agree that the A2-visible boundary is a stable, A2-owned
  interface (own `ntdll`-equivalent, private upcall mechanism), with A1 providing only
  object, memory, thread and IPC primitives? Which side owns the upcall entry and its
  versioning?
- **Why it matters:** Q3/Q4 recommendations depend on this split; anti-cheat and update
  resilience depend on not matching Windows syscall numbers.

### A1-REV-11 — Clock and timeout semantics

- **Requirement:** X3, X7.
- **Gap:** `o0_handle_wait` takes `timeout_ns`; the clock source and absolute/relative
  distinction are not stated.
- **Question:** which monotonic clock backs timeouts, what is its resolution, and can A2
  request absolute deadlines for timer objects?
- **Why it matters:** `QueryPerformanceCounter`, `GetTickCount64` and wait timeouts must
  agree with each other.

### A1-REV-12 — Security model split

- **Requirement:** X8, A1-OBJ-1, A2-Q12.
- **Gap:** the draft has no tokens, integrity levels or access-check hook.
- **Question:** is the Windows-like security model A2 user-mode policy over A1 handles,
  or does A1 enforce access? If A2 enforces, what object metadata can A1 store and
  return?
- **Why it matters:** granting more access than Windows is a security bug, not a
  compatibility convenience.

## Requested sign-off

A1 can answer in a PR comment on <https://github.com/Momegaii/Ozero/pull/1> or in a
follow-up commit to `a1/stage0-review`. For each question A2 needs one of:

- **Accept** — the draft already satisfies the requirement; cite the header or section.
- **Change** — A1 will change a primitive; name the header, the commit and the milestone.
- **Defer** — A2 must build it above the kernel or the requirement moves later; state
  which milestone.

A sign-off that closes XB-002 must include: the A1 commit hash reviewed, the A2 commit
hash reviewed, the per-question answers, and the date. A general "looks fine" comment
does not close the blocker.

## Sources

- A1 `docs/a2-contract-review-response.md`, branch `a1/stage0-review`, commit `74b1471` (`a17d126`).
- A1 `docs/interface-draft.md` §5–§8 and `docs/kernel-architecture.md` §4, commit `74b1471`.
- A1 `include/ozero/ipc.h`, `include/ozero/thread.h`, `include/ozero/mm.h`, commit `74b1471`.
- A1 `docs/if009-review.md`, commit `a17d126`.
- Microsoft, Processes and Threads: <https://learn.microsoft.com/en-us/windows/win32/procthread/processes-and-threads>
- Microsoft, Asynchronous Procedure Calls: <https://learn.microsoft.com/en-us/windows/win32/sync/asynchronous-procedure-calls>
- Microsoft, x64 exception handling: <https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64>
- Microsoft, Memory Management: <https://learn.microsoft.com/en-us/windows/win32/memory/memory-management>

## IDs

| ID | Meaning | State |
| --- | --- | --- |
| XB-002 | A1 → A2 contract match | A1 answered `30ee4f0`/`d9cdec1`; A2 accepts in this revision; A4 to record |
| XB-003 | A2 → A1/A3 PE fixture contract (IF-009) | Open; see [13](13-if-009-pe-fixture-contract.md) |
| A2-Q3 | Syscall/upcall strategy | A2-local; related D-018 |
| A2-Q4 | Own vs Windows `ntdll` | A2-local; related D-011 |
| A2-Q12 | Security model fidelity | A2-local; related D-011, D-019 |

Canonical D-xxx IDs belong to A4's register. A2 does not allocate them.
