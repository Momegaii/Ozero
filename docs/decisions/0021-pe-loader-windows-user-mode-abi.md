# ADR 0021: PE loader and Windows user-mode ABI

- **Status:** Proposed — implementation record pending Director approval.
- **Date:** 2026-11-26
- **Stage:** 7 (PE loader and Windows user-mode ABI)
- **Supersedes:** nothing. Relates to ADR 0007 (native user ABI/syscall/executable), ADR 0008 (capability handles/budgets/IPC) and AGENTS.md architecture rules.
- **Blocks:** Stage 7 milestones (console hello-world, threads and files, real small program).

## 1. Context

Stage 7 adds the ability to run Windows-originated user programs (PE32+ images) on Ozero. The tree today has:

- A frozen native Ozero user ABI (ADR 0007): `syscall` entry, register convention, native error codes, fixed user address-space layout, static ET_EXEC ELF64 loader. `include/ozero/uapi.h` is the frozen contract and explicitly states a future Win32/PE layer performs its own translation in user space and must not reuse these numbers.
- No kernel NT layer, no NTSTATUS, no Win32 syscall numbers, no dynamic linker, no DLL loader, no PE support.
- File syscalls (open/read/write/close/stat/readdir, Wave 4.6) exist on the native dispatcher, backed by OzeroFS and the block layer.
- Handle, budget, memory-object and IPC objects exist (ADR 0008), single CPU, emulator scope only.

The open question this ADR resolves: where does the Windows compatibility layer live — inside the kernel (an "NT layer") or in user mode (a `ntdll`/`kernel32` translation library)?

## 2. Decision

**Windows compatibility is a user-mode translation layer.** Ozero keeps its own native executable format, loader, runtime, process model, ABI and syscall interface (AGENTS.md Rule 1). PE32+ is accepted only as a *container format* for Windows-compatible user programs — like ELF64 is a container for native Ozero programs — and never as the native executable format or as a kernel contract.

| Element | Decision |
|---|---|
| Compatibility layer location | User mode only. A minimal `ntdll` + `kernel32` library translates Win32/NT API calls into Ozero native syscalls. |
| Kernel changes | None beyond loading PE32+ images into the native user address space. The kernel never sees NTSTATUS, Win32 handles or NT syscall numbers. |
| Executable container | PE32+ (x86-64, optional-header magic 0x20b) loaded by an Ozero PE loader into the native user image window. |
| Native executable format | Unchanged: Ozero's own, decided separately; ELF64 remains the Wave 2 container. PE32+ does not replace or constrain it. |
| Entry contract | PE programs enter through Ozero's native ring-3 frame (`syscall`/`iretq`, RDI/RSI/RDX args). The PE loader fixes the entry point and stack. `ntdll` is the first user-mode code that runs. |
| DLL / dynamic linking | Not implemented in Stage 7. `ntdll`/`kernel32` are linked statically into each PE image (one compilation unit per program). A real import resolver / DLL loader is deferred to Stage 8+. |
| NT objects | Handles, mutexes, events, SEH and registry are minimal user-mode stubs backed by ADR 0008 kernel objects where they map cleanly; unsupported calls return `ERROR_NOT_SUPPORTED`. No kernel NT object manager. |

## 3. Alternatives considered

| Alternative | Why it was rejected |
|---|---|
| **Kernel NT layer** (kernel implements NT syscalls, NT object manager, ntdll as a kernel component) | Violates AGENTS.md Rule 7: the Ozero-native loader/runtime/ABI and the Windows compatibility layer must be distinct components with a clear boundary. A kernel NT layer would couple the native ABI to NT semantics, make NTSTATUS foundational, and risk making Windows compatibility the kernel's model. It also conflicts with ADR 0007 §4 (the PE/NT layer must not reuse native numbers or assume native structures). |
| **PE32+ as the native Ozero executable format** | Violates AGENTS.md Rule 6: the Limine/ELF64 boot image does not decide the user executable format. Adopting PE as native would import Microsoft's section/alignment/relocation contracts into the kernel. |
| **Reuse the native syscall numbers/error codes for Win32** | Rejected: ADR 0007 freezes the native ABI; Stage 7 gets its own user-space translation table (Win32 error codes ⇄ Ozero native codes) so the two number spaces never collide. |
| **ELF wrapper + Win32 personality** | Rejected: adds an unnecessary translation step. PE32+ programs run as PE32+; the loader validates the PE container directly. |

## 4. Consequences

- **ABI freeze preserved.** The native syscall numbers in `include/ozero/uapi.h` are unchanged. Win32 API numbers and NTSTATUS live only in the user-mode compat library (`user/compat/`).
- **PE loader lives in the kernel only as an image loader.** `kernel/src/pe.c` (pure parser over a byte buffer, host-fuzzable) and `kernel/src/pe_loader.c` (address-space mapping, base relocation, entry setup) are Ozero code, but they implement the PE *container* contract, not an NT runtime.
- **No DLL loader yet.** Imports are resolved statically at link time for Stage 7 programs; `ntdll`/`kernel32` source is compiled into each PE image. Dynamic loading (LoadLibrary, a real import address table resolver) is a Stage 8+ item with its own ADR.
- **Single CPU.** User threads remain BSP-pinned (Wave 3 limitation). `CreateThread` is a stub returning `ERROR_NOT_SUPPORTED` until per-CPU TSS/IST and a user thread-creation syscall exist.
- **Error model.** The compat library translates `O0_UE_*` native codes to Win32 `ERROR_*` codes and `NTSTATUS` values entirely in user space. The kernel emits neither.
- **Security boundary unchanged.** PE images are loaded into the same native user image window, subject to the same W^X, guard-page, null-guard and segment-bounds rules as ELF images. The PE parser rejects malformed images before any mapping.

## 5. Verification plan

Host tests (no hardware required):

- `tests/pe_test.c`: PE32+ acceptance cases (valid console image, imports, relocations, section bounds) and one rejection case per validation rule (bad magic, bad machine, truncated headers, overlapping sections, entry outside executable section, W+X, oversized image, bad DataDirectory).
- `tests/pe_fuzz.c`: the pure PE parser driven by random/mutated byte buffers; it must never fault and must return a distinct status for each rule.

Emulator evidence (QEMU BIOS and UEFI, `qemu64` and `Haswell-v1`), reported separately:

- PE32+ console hello-world reaches ring 3 and prints `OZERO: stage7 pe hello OK` to serial through `kernel32.WriteFile` → `ntdll` → native `debug_write`.
- A malformed PE image is rejected at load time without crashing the kernel.

Physical hardware: none claimed.

## 6. Open questions for the Director

1. **Import resolver.** Stage 7 links `ntdll`/`kernel32` statically. Approve a Stage 8 DLL loader + import-address-table resolver before dynamic imports are added.
2. **PE image base.** Default PE32+ image base `0x140000000` lies outside the Ozero user image window (`0x40000000`–`0x7ffe0000`). Approve load-time rebasing (base relocations applied by `pe_loader.c`) plus linking Stage 7 programs with a custom in-window base.
3. **SEH scope.** Windows x86-64 SEH is table-based (`.pdata`/`.xdata`, `RUNTIME_FUNCTION`). Stage 7 ships a minimal vectored-handler stub in `ntdll`; full unwind dispatch is deferred.
4. **File/thread semantics.** `CreateFile`/`ReadFile` map onto the Wave 4.6 file syscalls; `CreateThread` is unsupported until a user thread-creation syscall exists. Confirm these stubs are acceptable as the Stage 7 surface.

## 7. References

1. Microsoft, *Microsoft Portable Executable and Common Object File Format Specification* (PE/COFF), x86-64 (PE32+).
2. Intel, *Intel 64 and IA-32 Architectures Software Developer's Manual*, Vol. 2A (`SYSCALL`, `SYSRET`, `SWAPGS`), Vol. 3A (paging, GDT/TSS, IDT).
3. Ozero `AGENTS.md` (architecture rules), `docs/decisions/0007-user-abi-syscall-and-executable.md`, `docs/decisions/0008-capability-handles-budgets-and-ipc.md`.
4. Ozero `include/ozero/uapi.h` (frozen native ABI contract).
