# NT / Win32 dependency map

- Status: draft ready (mapping only; no API implemented)
- Evidence: import measurements in [01-pe-loader-requirements.md](01-pe-loader-requirements.md); `python3 tools/pe_inspect.py` output in [evidence/pe-inspect.txt](evidence/pe-inspect.txt); corpus entries in [data/test-corpus.json](../data/test-corpus.json)
- Decisions needed: syscall strategy (Q3), own `ntdll` vs Windows `ntdll` (Q4), baseline API set schema (Q1), 32-bit/WOW64 (Q2)
- Risks: syscall numbers change per Windows build; API set schema is undocumented; kernel32 alone is 162 imported symbols in the measured sample; see [05-risk-report.md](05-risk-report.md)
- Next step: agree the P0 family list below with A1 and turn each row into an A1 interface test in [03-a1-interface-requirements.md](03-a1-interface-requirements.md)

## Two boundaries, not one

Win32 compatibility is often drawn as "applications call Win32". That hides the
boundary that decides the architecture.

```
Windows application
        |
        |  Win32 / CRT / COM / Winsock / .NET  (user-mode DLLs)
        v
kernel32.dll  kernelbase.dll  user32.dll  gdi32.dll  advapi32.dll  ws2_32.dll ...
        |            (documented Win32 API)
        v
ntdll.dll   (Nt* / Zw* native API, loader, heap, RTL, exception dispatch)
        |
        |  syscall boundary (numbers vary per Windows build)
        v
NT kernel: object manager, memory manager, I/O manager, process manager,
           security reference monitor, configuration manager, cache manager
```

Two consequences:

1. **Win32 is a user-mode library layer.** `kernel32`, `user32` and `gdi32` are
   not kernel interfaces. They are ordinary DLLs whose implementation calls
   `ntdll`. ReactOS describes the Win32 API as "the lowest layer of user mode",
   which is the same architecture.
2. **The kernel boundary is `ntdll` to the kernel.** Everything below that is A1's
   problem; everything above it is A2's problem.

A compatibility layer must therefore decide what it does at the `ntdll` boundary
before it writes a single `CreateFile` implementation.

## Win32 API families and what they need

Priority is derived from the measured import graph of the sample set, not from
intuition: `kernel32` and the UCRT contracts dominate.

| Priority | Family | Representative DLLs and exports | Required A1 capability | Corpus entries |
| --- | --- | --- | --- | --- |
| P0 | Process and thread | `kernel32`: `CreateProcessW`, `ExitProcess`, `CreateThread`, `GetCurrentProcessId`, `GetModuleHandleW` | process/thread objects, address space, TEB/PEB, handle table | console, file-process |
| P0 | Memory | `kernel32`: `VirtualAlloc`, `VirtualFree`, `HeapAlloc`, `VirtualProtect` | reserve/commit/protect, section objects | all fixtures |
| P0 | File I/O | `kernel32`: `CreateFileW`, `ReadFile`, `WriteFile`, `SetFilePointerEx`, `GetFileAttributesW` | file objects, async I/O, share modes | file-process, 7-Zip, curl |
| P0 | Directory and path | `kernel32`: `FindFirstFileW`, `FindNextFileW`, `GetFullPathNameW`, `CreateDirectoryW` | directory query, path translation | file-process, 7-Zip |
| P0 | Synchronization | `kernel32`: `WaitForSingleObject`, `CreateEventW`, `CreateMutexW`, `InitializeCriticalSection` | dispatcher objects, keyed events, waits | file-process, 7-Zip |
| P0 | CRT | `api-ms-win-crt-*`: 8 contracts, 127 symbols in the fixture set; `msvcrt.dll` in 7-Zip | nothing kernel-specific beyond memory/file/thread; the API set resolution itself is the hard part | all fixtures |
| P0 | Error and strings | `kernel32`: `GetLastError`, `FormatMessageW`; `ntdll`: `RtlNtStatusToDosError` | error code mapping, message tables | all |
| P1 | Registry | `advapi32`: `RegOpenKeyExW`, `RegQueryValueExW`, `RegSetValueExW` | configuration manager equivalent | 7-Zip, Notepad++ |
| P1 | Security | `advapi32`: tokens, SIDs, ACLs, `GetUserNameW` | token objects, access checks | 7-Zip, installers |
| P1 | Console | `kernel32`: console handles, `WriteConsoleW`, `SetConsoleMode` | console device and session | 7-Zip, curl |
| P1 | Dynamic loading | `kernel32`: `LoadLibraryExW`, `GetProcAddress`; API sets; SxS | loader, module list, activation contexts | all |
| P1 | Time | `kernel32`: `QueryPerformanceCounter`, `GetSystemTimeAsFileTime` | monotonic clock, timers | all |
| P1 | COM / OLE | `ole32`, `oleaut32`: `CoInitializeEx`, `CoCreateInstance`, `SysAllocString` | ALPC or in-process-only COM | 7-Zip (OLEAUT32) |
| P1 | Networking | `ws2_32`, `mswsock`: sockets, `getaddrinfo`; `secur32`/`schannel` for TLS | socket objects, IOCP, time | curl |
| P2 | USER / GDI | `user32`, `gdi32`, `comctl32`, `win32u` | kernel window manager equivalent or user-mode compositor | GUI, Notepad++, Paint.NET |
| P2 | Graphics | `d3d11`, `dxgi`, `d3d12`, `dxgi` 1.4+ | display driver model or a translation layer | DirectX entries |
| P2 | Audio | `mmdevapi`, `audioses`, `dsound`, `xaudio2_9` | audio endpoint objects and a host audio API | not yet in corpus |
| P2 | .NET | `mscoree`, CLR | CLR host plus loader transformation | .NET entries |
| P2 | Services / SCM | `advapi32`: `OpenSCManagerW`, `CreateServiceW` | service control manager, session model | installers |

### Measured import distribution

Across the eight parsed images: 16 distinct DLL names, 360 imported symbols.

| DLL or contract | Symbols | Note |
| --- | --- | --- |
| `kernel32.dll` | 162 | 45% of all imports |
| `api-ms-win-crt-runtime-l1-1-0.dll` | 72 | API set contract, not a file |
| `msvcrt.dll` | 40 | 7-Zip's CRT |
| `api-ms-win-crt-stdio-l1-1-0.dll` | 33 | API set contract |
| `api-ms-win-crt-heap-l1-1-0.dll` | 16 | API set contract |
| `advapi32.dll` | 8 | registry/security |
| `oleaut32.dll` | 6 | COM automation |
| other CRT contracts | 10 | locale, environment, math, string, private |
| `user32.dll` | 2 | only one GUI entry point so far |
| `python311.dll`, `vcruntime140.dll`, `mscoree.dll` | 3 / 3 / 1 | host-specific |

The API set contracts are the important detail: even `hello world` built with
UCRT does not import a real file name. The loader must resolve
`api-ms-win-crt-runtime-l1-1-0.dll` through the API set schema to a host DLL.

## The NT native surface

These are the `ntdll` services that a Win32 implementation sits on. Names and
semantics are documented in the Windows Driver Kit headers and in public
reimplementation work, not in the PE specification.

### Loader and image services

- `NtCreateSection`, `NtOpenSection`, `NtMapViewOfSection`, `NtUnmapViewOfSection`
- `NtProtectVirtualMemory`, `NtAllocateVirtualMemory`, `NtFreeVirtualMemory`
- `NtQueryVirtualMemory`, `NtFlushInstructionCache`
- `LdrLoadDll`, `LdrGetProcedureAddress`, `LdrUnloadDll`, `LdrRegisterDllNotification`
- `LdrInitializeThunk`, `LdrpInitializeProcess` (internal; behaviour, not an ABI)

### Process and thread services

- `NtCreateUserProcess` (used by modern `CreateProcess`), `NtCreateProcessEx`,
  `NtTerminateProcess`, `NtQueryInformationProcess`, `NtSetInformationProcess`
- `NtCreateThreadEx`, `NtTerminateThread`, `NtSuspendThread`, `NtResumeThread`,
  `NtGetContextThread`, `NtSetContextThread`, `NtQueueApcThread`
- `NtWaitForSingleObject`, `NtWaitForMultipleObjects`, `NtAlertThread`
- `NtSetInformationThread` (`ThreadHideFromDebugger`, `ThreadImpersonationToken`)

### Object, handle and synchronization services

- `NtClose`, `NtDuplicateObject`, `NtQueryObject`
- `NtCreateEvent`, `NtSetEvent`, `NtCreateMutant`, `NtCreateSemaphore`,
  `NtCreateTimer`, `NtSignalAndWaitForSingleObject`
- Keyed events (`NtCreateKeyedEvent`) underpin `CRITICAL_SECTION`

### I/O services

- `NtCreateFile`, `NtReadFile`, `NtWriteFile`, `NtDeviceIoControlFile`,
  `NtFsControlFile`, `NtQueryInformationFile`, `NtSetInformationFile`
- `NtQueryDirectoryFile`, `NtQueryVolumeInformationFile`
- `NtCreateIoCompletion`, `NtSetIoCompletion`, `NtRemoveIoCompletion`

### Registry and configuration

- `NtCreateKey`, `NtOpenKey`, `NtQueryValueKey`, `NtSetValueKey`,
  `NtEnumerateKey`, `NtEnumerateValueKey`, `NtDeleteKey`

### Exception and debug

- `KiUserExceptionDispatcher` (user-mode entry for exceptions)
- `RtlDispatchException`, `RtlVirtualUnwind`, `RtlLookupFunctionEntry`
- `NtRaiseException`, `NtContinue`
- `DbgUiConnectToDbg`, `DbgUiWaitStateChange` (debug events)

### Security

- `NtOpenProcessToken`, `NtQueryInformationToken`, `NtAccessCheck`,
  `NtSetInformationToken`, `NtImpersonateThread`

## Syscall strategy: three options

The syscall boundary is not a stable ABI. The `Nt*` stubs in `ntdll` load a
service number into `eax` and execute `syscall` (x64), and those numbers are
generated per Windows build. Public syscall tables show the numbers changing
between builds; Wine and ReactOS both avoid depending on Windows' numbers.

| Option | How it works | Advantages | Problems |
| --- | --- | --- | --- |
| A. Match Windows syscall numbers | Implement the kernel side with the same numbers as a chosen Windows build | a real `ntdll.dll` could be dropped in | breaks on the next Windows update; requires per-build tables; invites undefined behaviour |
| B. Own `ntdll` equivalent plus a private boundary (recommended) | Ship a user-mode `ntdll`-compatible DLL whose `Nt*` functions call A1 through a stable private interface | stable, testable, no dependency on undocumented numbers | cannot use Microsoft's `ntdll`; must implement RTL, heap and loader pieces |
| C. Patch stubs at load | Rewrite `ntdll` stub bytes to jump into the layer | small code change | fragile, anti-cheat and integrity checks treat patched `ntdll` as tampering; debugging is painful |

**Recommendation: option B**, with option C reserved for experiments only. The
private boundary can be a syscall-like instruction or a direct call; that choice
belongs to A1. A2 requires only that the boundary is stable across Windows
updates and that it can deliver exceptions and APCs to user mode.

### Why not just use Microsoft's `ntdll.dll`

- Its syscall numbers target the Windows kernel, not A1.
- It depends on kernel behaviour that a compatibility layer will not reproduce
  exactly (object manager namespaces, keyed events, ALPC).
- Its integrity is checked by anti-cheat and by some DRM. Patching it converts a
  compatibility problem into a tamper-detection problem.

## PEB, TEB and thread-local state

Win32 and the CRT assume that the processor's segment base points at the TEB and
that the PEB is reachable from it. The commonly used offsets are:

| Architecture | TEB | PEB | Notes |
| --- | --- | --- | --- |
| x86 | `fs:[0x18]` | `fs:[0x30]` | also the SEH chain head at `fs:[0x00]` |
| x64 | `gs:[0x30]` | `gs:[0x60]` | no stack-based SEH chain; table-based unwinding |

These offsets are widely used but not part of the PE specification; they must be
verified against the target Windows build before being treated as fixed. The
practical requirements for A1 are:

- a per-thread TEB with a self-pointer, TLS slots, stack limits and a client ID;
- a per-process PEB with image base, loader data, process parameters and
  `BeingDebugged`/`NtGlobalFlag` fields that applications read;
- a way to set the segment base on context switch (x64 `IA32_GS_BASE`, or
  `FSGSBASE` instructions where enabled).

## DLL search order, API sets and SxS

The documented search order for unpackaged desktop applications is:

1. DLL redirection
2. API sets
3. SxS manifest redirection
4. loaded-module list
5. KnownDLLs
6. package dependency graph (Windows 11 21H2+)
7. application directory
8. system directory
9. 16-bit system directory
10. Windows directory
11. current directory
12. `PATH`

Safe DLL search mode is on by default, which moves the current directory later.
`SetDllDirectory` and `LoadLibraryEx(LOAD_WITH_ALTERED_SEARCH_PATH)` change the
order.

API sets are contracts, not files: the loader resolves `api-ms-win-*` names
through the API set schema to a host DLL. A compatibility layer needs its own
schema (a data file plus a resolver) and must be able to answer "is this contract
available" without guessing from the file name.

SxS activation contexts matter for `comctl32` v6 and for private assemblies; a
missing or wrong context produces the classic "wrong common controls" look and
hard failures in GUI applications.

## WOW64 and 32-bit images

The WOW64 documentation describes a user-mode emulator: `Wow64.dll` provides
thunks for kernel entry points, `Wow64Win.dll` for the window manager, and
`Wow64Cpu.dll` supports x86 on x64. 32-bit binaries are rebuilt to use a custom
calling sequence that WOW64 intercepts in user mode, extracts 32-bit arguments,
extends them to 64 bits and issues the native syscall.

Consequences:

- 32-bit support is a second user-mode architecture, not a flag. It needs its own
  `ntdll`, its own loader, and thunking for every structure that contains pointers.
- The measured sample already contains a 32-bit real-world binary (`7zr.exe`,
  9 546 relocations, 139 imports). Ignoring 32-bit narrows the corpus sharply.
- 16-bit applications require a separate emulator (NTVDM/WOW) and are out of
  scope.

## Dependency matrix on A1

Each row is a requirement with an observable test, not an implementation
instruction. The formal contract is in
[03-a1-interface-requirements.md](03-a1-interface-requirements.md).

| Requirement | A1 capability needed | Observable test | Priority |
| --- | --- | --- | --- |
| Map an image from a file | section object + map view | mapped image bytes equal file bytes | P0 |
| Apply relocations | writable image view + protect changes | relocated pointer equals base + delta | P0 |
| Create a process from an image | process object with address space | process starts at entry point | P0 |
| Create the first thread | thread object with TEB and stack | thread runs and exits | P0 |
| Deliver an exception | user-mode exception dispatch | `throw`/`catch` fixture reaches the handler | P0 |
| File read/write | file object with synchronous and async I/O | byte-exact round trip | P0 |
| Wait on objects | dispatcher objects with timeouts | wait returns at the expected time | P0 |
| Handle table | typed handles with inheritance | invalid handle returns the documented error | P0 |
| TLS | per-thread slots and template copy | TLS fixture reports counter 8 | P1 |
| Directory enumeration | directory query | file list matches | P1 |
| Registry | key/value store | key tree matches | P1 |
| Console | console device and handles | stdout captured by the harness | P1 |
| IOCP | completion port | overlapped read completes | P1 |
| ALPC | message port | in-process RPC round trip | P2 |
| Token and ACL | security objects | access denied where expected | P2 |
| Shared memory sections | named section objects | two processes see the same bytes | P2 |

## Evidence

- Import graph: produced by `tools/pe_inspect.py` over the eight images; the
  histogram above is reproducible with `make inspect`.
- The 8 distinct `api-ms-win-crt-*` contracts are visible in the same output and
  are the strongest argument that API set resolution belongs in the loader, not
  in a later "compatibility" phase.
- Nothing in this document has been executed. The table's tests are proposals
  until the corpus records them.

## Sources

- Microsoft, Dynamic-link library search order: <https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order>
- Microsoft, Windows API sets: <https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets>
- Microsoft, WOW64 implementation details: <https://learn.microsoft.com/en-us/windows/win32/winprog64/wow64-implementation-details>
- Microsoft, PE format specification: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, Managed execution process: <https://learn.microsoft.com/en-us/dotnet/standard/managed-execution-process>
- ReactOS architecture (Win32 as the lowest user-mode layer): <https://reactos.org/architecture/>
- Public syscall number tables (illustrating per-build churn): <https://github.com/j00ru/windows-syscalls>
- Intel 64 and IA-32 Architectures Software Developer Manuals (GS/FS base, SYSCALL): <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
