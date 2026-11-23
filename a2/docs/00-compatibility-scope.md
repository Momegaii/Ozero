# Compatibility scope map

- Status: draft ready (research and design only; nothing executed)
- Evidence: this file; [data/test-corpus.json](../data/test-corpus.json); measured image statistics in [01-pe-loader-requirements.md](01-pe-loader-requirements.md); `python3 tools/docs_check.py` and `python3 tools/corpus_lint.py` both exit 0
- Decisions needed: baseline Windows version and architecture set (Q1, Q2 in [09-open-questions.md](09-open-questions.md)); whether kernel-mode drivers are in scope at all (Q14)
- Risks: "full compatibility" is unbounded; graphics, .NET and anti-cheat can each sink a user-mode-only design; see [05-risk-report.md](05-risk-report.md)
- Next step: review the three-class split with A1 and A3, then freeze the Stage 7 milestone in [07-stage7-milestone.md](07-stage7-milestone.md)

## Why this map exists

The briefing asks what "full" compatibility could encompass. The honest answer is
that "full" has no single meaning: running a console program, running a DirectX 12
game and loading a kernel anti-cheat driver are three different engineering
problems with three different failure modes. This document separates them so that
progress can be measured per class instead of asserted globally.

Two statements are treated as load-bearing throughout this repository:

1. Parsing or mapping a PE file is not running a Windows application.
2. A user-mode compatibility layer does not load Windows kernel drivers.

Both are restated as acceptance tests in [07-stage7-milestone.md](07-stage7-milestone.md).

## The three compatibility classes

### Class U — user-mode PE and Win32/NT

The part that can, in principle, be implemented entirely in user mode on top of
A1's kernel primitives.

| Area | What it includes | Observable test |
| --- | --- | --- |
| PE loading | PE32/PE32+ headers, sections, imports, exports, base relocations, TLS, delay imports, load config, resources, exception tables | image maps and imports resolve (L1) |
| Process and thread semantics | creation, exit, handles, jobs, affinity, priorities, TLS slots, APCs | process starts and exits with the expected code (L2) |
| File and directory APIs | CreateFile, ReadFile/WriteFile, memory-mapped files, FindFirstFile, attributes, rename, delete | byte-exact round trip |
| Registry | key/value create, open, query, enumerate, delete | key tree matches expected values |
| Synchronization and IPC | events, mutants, semaphores, critical sections, pipes, mailslots, waits, completion ports | wait ordering and timeout behaviour |
| Security | tokens, SIDs, ACL checks, integrity levels, impersonation | access denied where Windows denies it |
| Console | console handles, screen buffer, code pages, Ctrl+C | output and exit code |
| Error and locale behaviour | GetLastError, FormatMessage, NLS | error codes match documented values |
| Networking | Winsock, name resolution, TLS via schannel | HTTP request completes |
| Dynamic loading | LoadLibrary, GetProcAddress, search order, API sets, SxS manifests, KnownDLLs | correct module is loaded from the correct path |
| COM/RPC (basic) | CoInitialize, class factories, IDispatch, RPC over ALPC | in-process COM object activates |

### Class G — graphics, audio and managed runtime

User-mode code that depends on Windows-only services or on a kernel graphics
driver model. This is where a compatibility layer usually stops being a loader
project and becomes a platform project.

| Area | What it includes | Why it is separate |
| --- | --- | --- |
| USER/GDI | windows, messages, controls, drawing, clipboard, DPI | On Windows, USER/GDI syscalls go through win32u.dll to win32k in kernel mode |
| Direct3D 9/10/11/12 and DXGI | device creation, swapchains, shaders, fences | The runtime talks to a user-mode driver, which talks to dxgkrnl and a kernel-mode display miniport driver |
| Composition | DWM, layered windows, HDR, multi-monitor | Depends on the display driver model, not only on APIs |
| Audio | WASAPI, DirectSound, XAudio2 | Audio endpoints are kernel audio drivers; user-mode APIs are thin clients |
| .NET / CLR | COR20 header, metadata, JIT, BCL, WinForms, WPF | The Windows loader hands managed images to mscoree.dll, which rewrites the entry point |
| Media | Media Foundation, DXVA, codecs | Hardware decode paths are driver-bound; codec licensing is separate |
| Browsers and WebView2 | Chromium sandbox, GPU process, COM | Sandboxing and GPU process behaviour depend on kernel objects and job objects |

### Class K — kernel-mode

The class a user-mode layer cannot provide, regardless of how complete its Win32
implementation is.

| Area | Examples | Consequence |
| --- | --- | --- |
| Storage and filesystem drivers | storport miniports, filter drivers, filesystem drivers | Not loadable by a user-mode layer |
| Network drivers | NDIS miniports, WFP callouts | Not loadable |
| Audio drivers | PortCls/AVStream miniports | Not loadable |
| Display drivers | WDDM kernel-mode display miniports | Required for a real D3D device on Windows; a translation layer avoids it by using Vulkan instead |
| Device drivers | WDM/WDF (KMDF) drivers, USB/PCI function drivers | Not loadable; UMDF is the only user-mode driver framework |
| Anti-cheat | BattlEye `BEDaisy.sys`, EAC, Vanguard | Kernel driver plus signing, debug-state and kernel-integrity checks; explicitly unsupported on test-signing systems |
| DRM | Denuvo, Arxan, Steam stub | Kernel components and tamper checks |
| Security products | EDR/AV filter drivers | Not loadable, and they may block a compatibility layer |

The BattlEye FAQ is a concrete public example of the class-K constraints: it
documents a kernel driver, driver-load failures caused by hardware-enforced stack
protection, SHA-2 driver signing requirements, refusal to run with kernel
debugging enabled, and refusal to support test-signing mode.

## Maturity ladder

Claims must name a level. "Compatible" without a level is not a claim.

| Level | Name | Definition | Evidence required |
| --- | --- | --- | --- |
| L0 | Parsed | headers and directories read by `tools/pe_inspect.py` | tool output for the exact file hash |
| L1 | Mapped | image mapped, relocations applied, imports resolved, entry point not reached | loader trace plus resolved-symbol count |
| L2 | Ran headless | process starts, produces the expected stdout and exit code | command, stdout, exit code, environment |
| L3 | Ran interactive | window/audio/graphics behaviour matches a reference | screenshots, logs, reference comparison |
| L4 | Corpus measured | a defined corpus at L2/L3 with published pass rates | per-entry records and aggregate numbers |

The corpus in [data/test-corpus.json](../data/test-corpus.json) records an explicit
`evidence_level` per entry: eight entries are `L0` (static analysis only), the rest
are `none`. No L1–L4 claim exists, and the linter rejects a level that does not
match the recorded work.

## What "full compatibility" would have to include

A defensible use of the phrase would require all of the following, each with a
measured corpus:

1. Class U: a named Windows API baseline (for example Windows 10 22H2 plus API
   set schema of that build), with import-resolution and behavioural coverage
   measured over the corpus.
2. Class U: 32-bit and 64-bit processes, including WOW64-style thunking if the
   host is 64-bit only.
3. Class G: at least D3D11 and DXGI with a presentable swapchain, plus GDI/USER
   behaviour good enough for the GUI corpus.
4. Class G: CLR hosting for a named .NET version, both framework-dependent and
   self-contained deployments.
5. Class K: an explicit statement of what is not supported. Kernel drivers,
   anti-cheat and DRM cannot be included by writing more user-mode code.
6. A published corpus, per-entry results and a definition of "pass".

Anything less is a scoped claim, and should be written as one.

## Explicit non-goals for Stage 0

- No loader implementation, no emulation, no syscall table.
- No claim that any Windows application runs.
- No support statement for kernel drivers, anti-cheat or DRM.
- No 32-bit execution claim. One 32-bit sample is parsed for format coverage only;
  WOW64-style support is a scope decision, not an inference from that sample.
- No performance targets. Correctness first; performance is a later risk item.
- No commitment to a specific Windows build beyond the baseline question.

## Cross-agent boundaries

| Agent | Owns | A2 supplies | A2 must not do |
| --- | --- | --- | --- |
| A1 | kernel primitives: process, memory, threads, syscalls, handles | requirements with semantics and pass/fail tests | dictate kernel internals without review |
| A3 | GPU and device drivers | Windows graphics API requirements and test cases | assume a driver model |
| A4 | test harness and integration | test manifests and expected behaviour | claim results A4 has not produced |

The formal A1 contract is [03-a1-interface-requirements.md](03-a1-interface-requirements.md);
the A3 requirements are [08-a3-graphics-requirements.md](08-a3-graphics-requirements.md).

## Evidence for this document

- Structure and citation checks: `python3 tools/docs_check.py` (see
  [evidence/docs-check.txt](evidence/docs-check.txt)).
- Corpus structure, coverage and honesty rules: `python3 tools/corpus_lint.py`
  (see [evidence/corpus-lint.txt](evidence/corpus-lint.txt)).
- Image measurements quoted in this document come from
  [01-pe-loader-requirements.md](01-pe-loader-requirements.md), produced by
  `python3 tools/pe_inspect.py`.

## Sources

- Microsoft, PE format specification: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, Dynamic-link library search order: <https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order>
- Microsoft, Windows API sets: <https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets>
- Microsoft, WDDM overview: <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide>
- Microsoft, WOW64 implementation details: <https://learn.microsoft.com/en-us/windows/win32/winprog64/wow64-implementation-details>
- Microsoft, Managed execution process (mscoree and `_CorValidateImage`): <https://learn.microsoft.com/en-us/dotnet/standard/managed-execution-process>
- Microsoft, Windows Driver Frameworks (KMDF/UMDF): <https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/>
- BattlEye FAQ (kernel driver, signing, debug and test-signing constraints): <https://www.battleye.com/faq>
- ReactOS architecture: <https://reactos.org/architecture/>
