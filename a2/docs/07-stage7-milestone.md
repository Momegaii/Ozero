# Stage 7 milestone: earliest testable target

- Status: draft ready (recommendation; no milestone attempted)
- Evidence: prerequisites derived from [01](01-pe-loader-requirements.md) and [03](03-a1-interface-requirements.md); corpus entry `a2-console-hello` with measured L0 facts; no L1/L2 result exists
- Decisions needed: accept M0 (loader dry run) as a milestone distinct from "running", and accept M1 as the Stage 7 exit test (Q3, Q15)
- Risks: a GUI or .NET first milestone would hide loader defects behind two more layers; a "dry run" could be mistaken for running; see [05-risk-report.md](05-risk-report.md)
- Next step: A1 confirms the P0 interface list, then A2 writes the M0 test harness and the loader trace format

## Implementation gate (director review, 23 November 2026)

**M0 and M1 are unimplemented.** No loader, trace harness, `ntdll`-equivalent or
process-creation code exists in this repository. The milestone definitions below are
specifications, not status.

They stay unimplemented until all of the following hold:

- **D-006** (Stage 0 acceptance) is approved by the Project Director;
- **XB-002** (A1 review of the process, address-space, handle and syscall contracts)
  is Signed;
- **XB-003** (A1/A3 review of the PE fixture contract, IF-009) is Signed;
- the corpus execution gate allows a sandboxed run; every entry is currently `not-run`.

Writing M0 code before these gates would freeze an interface A1 has not reviewed and
would make static evidence look like a loader result.

## Recommendation

**The earliest testable milestone is M1: load and run the console fixture to
exit code 0 with the expected stdout, using a minimal `ntdll`-equivalent over A1.**

Before M1 there must be **M0: a loader dry run** that maps the same image, applies
relocations and resolves imports against stub modules, without transferring
control. M0 is the earliest *useful* test; M1 is the earliest *honest* claim that
something ran.

A GUI or .NET milestone is the wrong first target: both add subsystems (USER/GDI,
CLR hosting) on top of a loader that is still unproven, so a failure cannot be
attributed.

## Why the console fixture

`a2-console-hello` (measured at L0) is small but not trivial:

| Property | Measured value | Why it matters |
| --- | --- | --- |
| Format | PE32+ amd64, subsystem 3 | exercises the primary format |
| Optional header | 240 bytes, 16 directories | validates header parsing |
| Sections | 7, including `.pdata`, `.tls`, `.reloc` | forces relocation and TLS handling |
| Imports | 7 DLLs / 38 symbols, including 6 `api-ms-win-crt-*` contracts | forces API set resolution |
| Relocations | 32 `DIR64` | forces base relocation if ASLR is on |
| TLS | 8-byte template, 2 callbacks | forces TLS before entry |
| `.pdata` | 31 entries, unwind version 1 | forces table registration even for C code |
| CLR | absent | keeps the CLR out of the first milestone |

## Milestone ladder

| Milestone | Name | Definition | Pass criteria | Kernel prerequisites |
| --- | --- | --- | --- | --- |
| M0 | Loader dry run | map image, relocate, resolve imports against stubs, do not run | mapped bytes equal file bytes; 32 relocations applied; 38 imports resolved to stub addresses; trace shows every step | address space, section objects, map view, protect |
| M1 | Console hello | run the fixture to completion | stdout equals `a2 console fixture: compute=22 module=<non-null>`; exit code 0; no loader error | M0 plus process/thread creation, TEB, stack, exception delivery, file/console object for stdout |
| M2 | File and process APIs | run `a2-fileproc` and a host that loads `a2-tls-dll` | `read=4 written=4`; exit 0; DLL exports resolve; TLS counter 8 | M1 plus pipes, handle table, `CreateProcess`, waits, virtual memory, directory query |
| M3 | GUI message box | run `a2-gui-messagebox` | window appears with the expected title/text; dismiss returns 0 | M2 plus USER32 message box, window station, desktop, event loop |
| M4 | Managed hello | run `a2-dotnet-hello` (framework-dependent and self-contained) | stdout equals `a2 dotnet fixture`; exit 0 in both variants | M3 plus CLR host or a hosted runtime |
| M5 | D3D11 triangle | run `imgui-d3d11` | frames present; clean exit; no device-removed error | M4 plus a graphics translation layer or driver model |

M2 is where the A1 contract becomes real: file objects, handles, process creation
and waits cannot be faked with stubs.

## M0 in detail

**Goal:** prove the loader without running any Windows code.

Steps:

1. Open the image through A1 (section object over the file).
2. Map headers and sections at a chosen base.
3. Apply `DIR64` relocations and verify a known pointer changed by the delta.
4. Walk imports and resolve each symbol to a stub thunk in the loader's own
   address space. Record every resolved and unresolved symbol.
5. Parse TLS and `.pdata`; do not run callbacks.
6. Emit a JSON trace: image hash, chosen base, section map, relocation count,
   import table with resolution results, TLS template size, `.pdata` entry count.

**Pass criteria:** the trace matches the expected values from
`tools/pe_inspect.py` for the same file hash. A mismatch is a loader bug or a
parser bug, and either is worth finding before process creation exists.

**Explicitly not a run:** no entry point, no CRT, no stdout, no TLS callback
invocation. M0 must never be reported as "the application runs". The trace is the
only artifact; if stdout appears, the run is not M0.

## M1 in detail

**Goal:** run one console image end to end.

Required path:

1. Process object with an address space; image mapped as in M0.
2. Initial thread with TEB, stack (reserve/commit, guard page) and a client ID.
3. `ntdll`-equivalent that implements at least: `RtlInitializeCriticalSection`
   and the memory/CRT imports actually used by the fixture, `NtWriteFile` to
   stdout, `NtTerminateProcess`, `NtClose`, `NtWaitForSingleObject`.
4. Exception delivery, because `.pdata` exists even in C code and a fault must
   terminate with the right code rather than hang.
5. TLS: allocate the index, copy the 8-byte template, call the two callbacks in
   order before the entry point.
6. API set resolution for the five CRT contracts.

**Pass criteria:**

- process exit code is 0;
- stdout is byte-exact, including the computed value 22;
- the module handle printed is non-null and points inside the mapped image;
- no error dialog, no hang, no host crash;
- the trace records which imports were resolved and from where.

**Evidence to archive:** command line, stdout, exit code, loader trace, image
hash, host and layer versions, date. The corpus entry moves from `not-run` to
`pass` only with those fields present (the linter enforces it).

## What must exist in kernel stages first

| Capability | Why M0 needs it | Why M1 needs it | A1 interface |
| --- | --- | --- | --- |
| Address space with reserve/commit/protect | map image | stack, heap, CRT | A1-MEM-1/2 |
| Section object and map view | map file-backed image | map DLLs later | A1-MEM-4/5 |
| Image base selection / ASLR | choose a base | same | A1-MEM-6 |
| Process object | — | create process | A1-PROC-1 |
| Thread object with TEB and GS/FS base | — | run code | A1-THR-1/5, A1-CPU-1 |
| Stable call ABI | — | every API call | A1-SYS-1/2 |
| Exception delivery | — | faults and `.pdata` | A1-EXC-1/2 |
| Handle table | — | stdout, process, thread | A1-OBJ-1 |
| Dispatcher objects and waits | — | CRT init and exit | A1-SYNC-1/2 |
| File object for stdout | — | capture output | A1-IO-1/2, A1-CON-1 |
| Monotonic clock | — | CRT and waits | A1-TIME-1 |

M0 needs the first three rows only. That is the point of splitting the milestone:
A1 can deliver memory management and section mapping first, and A2 can prove the
loader before process creation exists.

## Failure attribution rules

| Symptom | First suspect | Where to look |
| --- | --- | --- |
| image rejected before mapping | header validation | M0 trace step 1 |
| mapped bytes differ | section mapping | M0 trace step 2 |
| pointer not relocated | relocation application | M0 trace step 3 |
| import unresolved | API set resolution or stub table | M0 trace step 4 |
| crash before `main` | TLS callbacks or CRT init | M1 trace |
| crash in `main` | API semantics | API trace |
| hang | wait or console read | API trace |
| wrong exit code | process termination | M1 trace |

## Anti-goals for Stage 7

- No GUI, no .NET, no graphics in the exit criterion.
- No 32-bit image in the exit criterion (it is M2+ at the earliest). The single
  32-bit corpus entry (`7zr.exe`) is analysis-only and is not evidence of WOW64
  support; Q2 decides when x86 enters.
- No performance threshold.
- No claim about any application outside the corpus.
- No "compatibility percentage".

## Evidence

No milestone has been attempted. The prerequisites and pass criteria above are
proposals. The only measured facts are the L0 image properties from
`tools/pe_inspect.py`, recorded in [evidence/pe-inspect.txt](evidence/pe-inspect.txt).

## Sources

- Microsoft, PE format specification (headers, sections, TLS, relocations): <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, Unwind data structures for x64: <https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64>
- Microsoft, Windows API sets: <https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets>
- Microsoft, Dynamic-link library search order: <https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order>
- Intel 64 and IA-32 Architectures Software Developer Manuals (GS base, paging, protection): <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
