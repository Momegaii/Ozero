# A1 interface requirements (A2 → A1 contract)

- Status: draft ready (requirements; not agreed with A1)
- Evidence: derived from the loader pipeline in [01-pe-loader-requirements.md](01-pe-loader-requirements.md) and the family map in [02-nt-win32-dependency-map.md](02-nt-win32-dependency-map.md); no A1 implementation exists yet
- Decisions needed: syscall/upcall ABI (Q3), exception-delivery ABI (Q3), address-space layout and ASLR policy, whether A1 or A2 owns the TEB allocator
- Risks: semantics drift between the contract and A1 internals; error-code mapping; alertable waits and APC delivery are easy to get subtly wrong
- Next step: review this table with A1, convert P0 rows into tests that run without a Windows application, and freeze them for Stage 7

## How to read this document

A2 specifies **requirements and observable behaviour**. A1 owns the internal design.
Each row names a capability, the semantics a Windows-compatible user-mode layer
needs, the error behaviour, and a test that can be run before any Windows
application exists.

Status values: `proposed` (not discussed with A1), `agreed`, `rejected`,
`changed`. All rows are currently `proposed`.

## Cross-cutting requirements

| ID | Requirement | Rationale |
| --- | --- | --- |
| X1 | Stable ABI across host kernel updates | Applications and A2 binaries must not be rebuilt per host release |
| X2 | Explicit error values, mapped to NTSTATUS and then to Win32 codes | `GetLastError` correctness is part of compatibility |
| X3 | All waits accept a timeout, including infinite and zero | Documented Win32 semantics depend on it |
| X4 | Alertable waits and APC delivery are supported | `SleepEx`, `WaitForSingleObjectEx`, `ReadFileEx`, thread pool |
| X5 | Handles are typed, reference-counted and inheritable on request | Duplicate/inherit semantics and error codes depend on it |
| X6 | 64-bit clean interfaces; no pointer truncation in 32-bit mode | PE32 processes need a 32-bit view without leaking host pointers |
| X7 | Timeouts use a monotonic clock with sub-millisecond resolution | `QueryPerformanceCounter`, wait timeouts |
| X8 | User-mode-visible state is not readable/writable by other processes without an explicit handle | Security model parity |
| X9 | No POSIX concepts leak into the A2-visible interface | `fork`, signals and paths would create untranslatable semantics |
| X10 | Every operation is cancellable or bounded | A stuck loader must not hang the system |

## Process and thread

| ID | Capability | Required semantics | Failure behaviour | Test | Priority |
| --- | --- | --- | --- | --- | --- |
| A1-PROC-1 | Create a process from an image section | new address space, parameters block, handle to process and first thread, suspended or running | status code; no partially created process | create and exit a fixture process | P0 |
| A1-PROC-2 | Terminate a process | exit code visible to waiters; handles closed; threads stopped | idempotent | terminate and observe exit code | P0 |
| A1-PROC-3 | Query/set process information | image path, command line, environment, affinity, priority, mitigation flags | unsupported info class returns the documented error | read back the command line | P1 |
| A1-THR-1 | Create a thread in a process | start address, argument, stack reserve/commit, TEB attached | status code | thread increments a shared counter | P0 |
| A1-THR-2 | Terminate/suspend/resume a thread | documented interaction with waits and APCs | status code | suspend then resume, no lost wakeups | P1 |
| A1-THR-3 | Thread context get/set | full register set including segment bases | status code | set a register, read it back | P1 |
| A1-THR-4 | APC queueing | user APC and special APC; runs at alertable wait or on return to user mode | status code | queue APC, alertable wait runs it | P0 |
| A1-THR-5 | TEB allocation and segment base | per-thread TEB with self-pointer and TLS slots; correct GS/FS base on switch | creation fails cleanly | TLS fixture reads its own TEB | P0 |

## Memory and sections

| ID | Capability | Required semantics | Failure behaviour | Test | Priority |
| --- | --- | --- | --- | --- | --- |
| A1-MEM-1 | Reserve/commit/decommit/release | page granularity, reserve/commit split, zero-fill on commit | status code; no leak on failure | reserve 1 MiB, commit one page, write | P0 |
| A1-MEM-2 | Protect | read/write/execute combinations, guard pages, no-access | status code | write to read-only page faults | P0 |
| A1-MEM-3 | Query | region boundaries, state, protection, type | status code | query matches what was set | P1 |
| A1-MEM-4 | Section objects | file-backed and pagefile-backed, named sections, share modes | status code | two processes map the same named section | P0 |
| A1-MEM-5 | Map view | map at requested base or anywhere, image vs data, copy-on-write | status code | mapped bytes equal file bytes | P0 |
| A1-MEM-6 | Address-space layout | deterministic ASLR ranges; image base randomisation when `DYNAMIC_BASE` | documented failure if preferred base is taken | relocate a fixture to a random base | P0 |
| A1-MEM-7 | Stack allocation | guard page, automatic growth, committed limits | stack overflow exception, not host crash | deep recursion raises the documented exception | P1 |

## Objects and handles

| ID | Capability | Required semantics | Failure behaviour | Test | Priority |
| --- | --- | --- | --- | --- | --- |
| A1-OBJ-1 | Handle table | typed handles, access masks, inheritance, duplication | invalid handle returns the documented status | use a closed handle | P0 |
| A1-OBJ-2 | Wait on handle | wait on process, thread, event, mutant, semaphore, timer | timeout status | wait with 0 ms and with a short timeout | P0 |
| A1-OBJ-3 | Object names | per-directory namespaces for sections, events, mutants | name collision returns the documented status | create two objects with one name | P1 |
| A1-OBJ-4 | Object query | type, name, basic information | status code | query type of a file handle | P2 |

## Synchronization

| ID | Capability | Required semantics | Failure behaviour | Test | Priority |
| --- | --- | --- | --- | --- | --- |
| A1-SYNC-1 | Event, mutant, semaphore, timer objects | manual/auto reset, ownership, recursion count, relative and absolute timeouts | status codes | ordering test with two waiters | P0 |
| A1-SYNC-2 | Wait for multiple objects | wait-all and wait-any, maximum count, abandoned-mutant status | status code | wait-any returns the lowest index | P0 |
| A1-SYNC-3 | Keyed events | used by `CRITICAL_SECTION`; key + thread semantics | status code | critical section contention test | P1 |
| A1-SYNC-4 | Condition variables | sleep/wake with critical section | status code | producer/consumer test | P2 |

## I/O

| ID | Capability | Required semantics | Failure behaviour | Test | Priority |
| --- | --- | --- | --- | --- | --- |
| A1-IO-1 | Open/create files | share modes, disposition, attributes, directory handles | documented status codes (`OBJECT_NAME_COLLISION`, `OBJECT_NAME_NOT_FOUND`) | create, open again exclusively, observe sharing violation | P0 |
| A1-IO-2 | Read/write | byte counts, file pointer, append, partial reads | short read returns the count, not an error | round trip with odd sizes | P0 |
| A1-IO-3 | Asynchronous I/O | overlapped requests, completion port delivery, cancellation | pending vs immediate completion | overlapped read completes on an IOCP | P1 |
| A1-IO-4 | Directory query | enumeration with restart, attributes, short names if supported | `NO_MORE_FILES` | enumerate a fixture directory | P1 |
| A1-IO-5 | File information | size, times, attributes, rename, delete-on-close | status codes | set and read back times | P1 |
| A1-IO-6 | Device I/O | `DeviceIoControl` passthrough where meaningful | `NOT_SUPPORTED` otherwise | not applicable at Stage 7 | P2 |

## Registry

| ID | Capability | Required semantics | Failure behaviour | Test | Priority |
| --- | --- | --- | --- | --- | --- |
| A1-REG-1 | Key and value operations | create/open/query/enumerate/set/delete; value types including `REG_SZ`, `REG_DWORD`, `REG_BINARY` | `OBJECT_NAME_NOT_FOUND`, `KEY_VALUE_PARTIAL_INFORMATION` | write then read a value | P1 |
| A1-REG-2 | Hive mapping | per-user and machine scopes; per-application overrides | documented access denied | read a machine key | P1 |

## Exceptions, unwind and debug

| ID | Capability | Required semantics | Failure behaviour | Test | Priority |
| --- | --- | --- | --- | --- | --- |
| A1-EXC-1 | Hardware and software exception delivery | user-mode handler entry with a Windows-compatible exception record and context; continuation | process terminates with the documented code if unhandled | divide-by-zero fixture reaches `__except` | P0 |
| A1-EXC-2 | Unwind support | `RtlVirtualUnwind`-compatible access to unwind data; chained info honoured | documented status if unwind data is missing | C++ fixture catches `throw 42` | P0 |
| A1-EXC-3 | Continue | restore context and resume at an arbitrary address | status code | resume after modifying context | P1 |
| A1-DBG-1 | Debug events | process/thread create/exit, exception, module load | status code | attach a debugger-equivalent consumer | P2 |

## System boundary and time

| ID | Capability | Required semantics | Failure behaviour | Test | Priority |
| --- | --- | --- | --- | --- | --- |
| A1-SYS-1 | Stable user→kernel call ABI | documented argument passing, return value, no reliance on host syscall numbers | defined behaviour on bad arguments | call every P0 service from a test shim | P0 |
| A1-SYS-2 | Kernel→user return path | correct GS/FS base, stack pointer, no host state leakage | process terminates cleanly | run a loop of 10^6 calls | P0 |
| A1-TIME-1 | Monotonic clock | nanosecond or better resolution, no backwards steps | documented error | measure a 100 ms sleep | P0 |
| A1-TIME-2 | Timers | relative and absolute, one-shot and periodic, coalescing policy | status code | periodic timer fires the expected count | P1 |

## Security, IPC and console

| ID | Capability | Required semantics | Failure behaviour | Test | Priority |
| --- | --- | --- | --- | --- | --- |
| A1-SEC-1 | Tokens and SIDs | user, groups, integrity level, privileges; access checks | `ACCESS_DENIED` | open a key that should be denied | P2 |
| A1-IPC-1 | ALPC-style message ports | connection, send/receive, timeouts | status code | RPC round trip inside one process | P2 |
| A1-CON-1 | Console device | screen buffer, input events, code pages, Ctrl+C | status code | capture stdout from a fixture | P1 |
| A1-CPU-1 | Segment base control | set GS/FS base per thread (`IA32_GS_BASE` or `FSGSBASE`) | documented failure | switch threads and read TEB self-pointer | P0 |

## What A2 must not require

- A specific kernel object layout or internal naming scheme.
- A specific scheduler or memory allocator.
- A specific syscall instruction. The ABI matters; the mechanism does not.
- Host filesystem layout for Windows paths. Path translation is A2's job; A1
  provides file objects and names, not drive letters.
- Kernel-mode driver loading. That is outside the user-mode contract and is
  recorded as a non-goal in [00-compatibility-scope.md](00-compatibility-scope.md).

## Open contract questions

1. **Exception ABI.** Does A1 deliver a Windows-shaped `EXCEPTION_RECORD` and
   `CONTEXT`, or a neutral form that A2 translates? A2 prefers the Windows shape
   to keep the translation layer thin, but this is A1's call.
2. **APC delivery point.** A1 must define exactly when user APCs run; Win32
   semantics depend on alertable waits and on return-to-user.
3. **TEB ownership.** Who allocates the TEB and who owns the TLS slot table?
   A2 needs a stable layout, not a specific allocator.
4. **Address-space layout.** ASLR ranges, image base selection and stack/heap
   placement must be documented, because some applications inspect them.
5. **Handle inheritance across process creation.** Inherit-by-handle-list vs
   inherit-all is observable through `CreateProcess` semantics.
6. **Clock and timer resolution.** Documented values are observable through
   `Sleep`, `QueryPerformanceCounter` and wait timeouts.

## Evidence

No A1 interface has been exercised. This document is a proposal. The only
measured evidence in the repository is the image-level data in
[01-pe-loader-requirements.md](01-pe-loader-requirements.md), which determines
which capabilities are needed first.

## Sources

- Microsoft, WOW64 implementation details (thunking model): <https://learn.microsoft.com/en-us/windows/win32/winprog64/wow64-implementation-details>
- Microsoft, PE format specification (sections, relocations, TLS): <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, Unwind data structures for x64: <https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64>
- Intel 64 and IA-32 Architectures Software Developer Manuals (GS/FS base, SYSCALL/SYSRET, paging, protection): <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
- ReactOS architecture (NT kernel component list): <https://reactos.org/architecture/>
