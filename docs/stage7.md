# Stage 7 — PE loader and Windows user-mode ABI

**Status:** Planning and architecture review. Implementation is not accepted as complete.  
**Last updated:** 2026-11-27  
**Architecture record:** [ADR 0021 — PE loader and Windows user-mode ABI](decisions/0021-pe-loader-windows-user-mode-abi.md)  
**Prerequisites:** Native ring-3 execution and syscall ABI (ADR 0007), capability objects (ADR 0008), VFS/file syscalls (ADR 0019), and the relevant SMP/user-thread limitations recorded in the project status.

## 1. Objective

Run a deliberately small, measurable subset of Windows x86-64 console applications as isolated user processes on Ozero. Stage 7 is not a Windows clone and does not make PE the native Ozero executable format. It introduces a PE32+ image-loading path and a user-mode compatibility runtime that translates a bounded Win32/NT API surface into Ozero-native capabilities and syscalls.

The native Ozero ABI remains authoritative for kernel entry, memory ownership, process isolation, scheduling and I/O. Windows semantics stay in user space.

## 2. Architecture gate

ADR 0021 records the proposed choice: **user-mode translation layer**, not a kernel NT layer. The ADR remains proposed until explicitly approved by the Project Director.

| Concern | Proposed ownership |
|---|---|
| PE32+ parsing and image mapping | Kernel image-loader boundary; parser is pure and host-testable |
| Process, address space, native syscalls | Ozero kernel; native ABI remains unchanged |
| Win32 API surface and NTSTATUS translation | User-mode `ntdll` / `kernel32` compatibility libraries |
| Windows HANDLE values | User-mode wrappers over Ozero capability handles; no kernel NT handle table |
| Files | Win32 wrappers over capability-backed VFS file handles |
| Synchronization | Win32 wrappers over Ozero kernel synchronization/capability objects |
| Registry | Explicitly limited user-mode stub; no kernel registry subsystem |
| SEH | x64 PE metadata validation first; bounded user-mode exception/unwind support, with unsupported cases reported |
| DLL loading | Static linking for Stage 7; dynamic imports and LoadLibrary deferred |

**Do not implement a kernel NT object manager, NT syscall-number space, kernel NTSTATUS contract, or Win32-specific kernel structures.** If the ADR is not approved, stop before implementing the compatibility ABI and request a decision.

## 3. Existing baseline — verified from the repository tree

- Native ring-3 execution, syscall entry and a static ELF64 loader are already part of the Wave 2 baseline (ADR 0007).
- Capability handles and object primitives are documented in ADR 0008.
- The capability-backed VFS and file syscalls are recorded in ADR 0019 / Wave 4.6.
- `include/ozero/pe.h` and `kernel/src/pe.c` exist. The parser accepts a bounded PE32+ AMD64 subset and exposes section, import, base-relocation and exception-directory metadata.
- `tests/pe_test.c` and `tests/pe_fuzz.c` exist and are included by the host-test Makefile recipes. The fuzz test currently uses deterministic random buffers; it is not evidence of coverage-guided fuzzing.
- **Not found in the inspected main tree:** `kernel/src/pe_loader.c`, PE address-space mapping/relocation execution, `user/compat/ntdll`, `user/compat/kernel32`, a Windows API runtime, or a QEMU PE hello-world acceptance program.

Therefore the current evidence is a PE parser and host tests, not an executable Windows compatibility environment. Reconfirm file existence and test results on the exact implementation branch before changing this status.

## 4. Milestones and exit gates

### 7.0 — Architecture and ABI freeze

- [ ] Project Director approves or revises ADR 0021.
- [ ] Freeze PE loader/kernel boundary, user-mode library boundary, supported PE subset and error translation.
- [ ] Specify Windows x64 calling convention: RCX, RDX, R8, R9; 32-byte shadow space; stack alignment; volatile/nonvolatile register rules.
- [ ] Define process startup contract, console standard handles, termination status and unsupported-API behavior.
- [ ] Specify how Windows HANDLE values wrap capability handles, including duplication, inheritance, close, generation/stale-handle behavior and access rights.
- [ ] Record the user-thread prerequisite and whether Stage 7 may proceed with BSP-pinned user threads.

**Gate:** no compatibility ABI implementation until the ADR and public headers agree.

### 7.1 — PE32+ validation and loading

- [ ] Keep parsing pure: no allocation, page-table mutation or device access inside `o0_pe_parse`.
- [ ] Validate DOS/NT/COFF/optional headers, AMD64 machine, PE32+ magic, section table, alignments, raw ranges, virtual ranges, entry point and data directories with overflow-safe arithmetic.
- [ ] Enforce image-size limits, user-window bounds, non-overlap, null/stack guards and W^X.
- [ ] Implement address-space mapping, zero-fill, permissions and load-time base relocation using only supported relocation types.
- [ ] Reject unsupported TLS, delay imports, dynamic imports, unsupported relocation types and malformed exception metadata with explicit status codes.
- [ ] Add parser and loader negative tests; run ASan/UBSan and coverage-guided fuzzing in addition to deterministic mutation tests.
- [ ] Add QEMU BIOS/UEFI smoke coverage for a minimal PE console image and malformed-image rejection.

**Gate:** a minimal PE32+ image reaches its entry point in ring 3; malformed inputs fail without kernel fault, leak or partial process publication.

### 7.2 — Console hello-world

- [ ] Build a minimal x86-64 PE32+ console executable with statically linked compatibility stubs.
- [ ] Implement the minimum startup path and `kernel32.WriteFile` / `GetStdHandle` surface needed by the sample.
- [ ] Translate output through `ntdll` to Ozero's native bounded debug/console path.
- [ ] Emit the acceptance marker `OZERO: stage7 pe hello OK` only after the program actually executes and writes its expected output.
- [ ] Test normal exit, non-zero exit, invalid standard handle and bad user pointer behavior.

**Gate:** QEMU serial evidence proves a PE image ran in ring 3 and completed the expected console write. Parser-only tests do not satisfy this gate.

### 7.3 — Handles, files and synchronization

- [ ] Define a user-mode Win32 HANDLE wrapper over Ozero capability handles. Keep Win32 numeric values and access masks out of the kernel ABI.
- [ ] Implement `CloseHandle`, `CreateFileW`, `ReadFile`, `WriteFile`, `SetFilePointerEx` and a deliberately small metadata subset over capability-backed VFS operations.
- [ ] Implement a minimal synchronization subset (critical sections, mutex/event wrappers) with explicit ownership, wait, timeout and close semantics.
- [ ] Add stale-handle, access-denial, double-close, concurrent wait/signal and file-offset tests.
- [ ] Keep unsupported APIs explicit; never return success from a placeholder.

**Gate:** a PE test program creates, writes, reads, seeks and closes a file, then exercises synchronization under QEMU and host tests.

### 7.4 — Threads and process behavior

- [ ] Add or approve the native user-thread creation/join/wait primitives required by the compatibility runtime.
- [ ] Implement `CreateThread`, `ExitThread`, `WaitForSingleObject` and a minimal `GetCurrentThreadId` mapping.
- [ ] Preserve Windows x64 register/stack entry rules for each thread start routine.
- [ ] Account for the current BSP-pinned user-thread limitation; do not claim user-thread SMP until per-CPU TSS/IST and scheduler support are verified.
- [ ] Test thread startup, return/exit, wait completion, timeout, handle closure and process teardown.

**Gate:** a PE program starts at least two user threads, coordinates them with a supported synchronization primitive, and exits without leaked thread/object/PMM accounting.

### 7.5 — SEH and registry stubs

- [ ] Validate PE exception directory (`.pdata`) and associated unwind records (`.xdata`) before exposing them to user mode.
- [ ] Define a bounded first SEH milestone; do not claim full Windows x64 unwinding or language-specific exception support.
- [ ] Provide a minimal vectored-exception/SEH compatibility surface only for explicitly supported exception classes.
- [ ] Implement a deterministic registry stub with a documented, small key/value surface and explicit unsupported behavior. Persistence is out of scope unless separately approved.
- [ ] Test malformed unwind metadata, nested/unsupported exception cases, registry type/size errors and unsupported operations.

**Gate:** supported exceptions are dispatched safely; unsupported cases terminate or return a documented error without corrupting process state. Registry calls behave deterministically.

### 7.6 — Real small program

- [ ] Select a small, redistributable, source-available Windows console program with no GUI, dynamic plugin or undocumented API dependency.
- [ ] Inventory its imports and runtime requirements before selecting it.
- [ ] Add only the missing API contracts justified by that import inventory.
- [ ] Run it from a mounted Ozero filesystem image, exercise file I/O and synchronization, and capture serial output plus exit status.
- [ ] Publish a compatibility matrix listing each imported API as implemented, partial, stubbed or unsupported.

**Gate:** the selected program completes a repeatable QEMU run with no hidden host-side emulation and no unreported compatibility shortcuts.

## 5. Non-goals for Stage 7

- Full Windows NT kernel implementation or NT syscall compatibility.
- General-purpose Win32 coverage, GUI applications, DirectX, COM, .NET or UWP.
- Dynamic DLL loading, `LoadLibrary`, delay-load imports or a complete Windows loader.
- Full x64 SEH/unwind semantics, kernel-mode drivers or Windows driver compatibility.
- Registry persistence, services, security descriptors or a complete Windows object namespace.
- Physical-hardware claims based only on QEMU.

## 6. Required evidence and reporting

Every milestone report must state the exact commit, build command, host-test command, sanitizer result, QEMU version, firmware, CPU model, SMP count, serial markers, exit status and known exclusions. Keep parser, loader, runtime, QEMU and physical-hardware evidence separate.

Use [stage7-test-plan.md](stage7-test-plan.md) as the acceptance checklist. Keep this page and ADR 0021 current as implementation changes.

## 7. References

- [ADR 0021 — PE loader and Windows user-mode ABI](decisions/0021-pe-loader-windows-user-mode-abi.md)
- [ADR 0007 — native user ABI, syscall and executable](decisions/0007-user-abi-syscall-and-executable.md)
- [ADR 0008 — capability handles, budgets and IPC](decisions/0008-capability-handles-budgets-and-ipc.md)
- [ADR 0019 — VFS layer and file syscalls](decisions/0019-vfs-layer-file-syscalls.md)
- [Ozero agent architecture rules](../AGENTS.md)
- Microsoft, *PE Format* / PE-COFF specification
- Microsoft, *x64 calling convention* and *x64 exception handling*
