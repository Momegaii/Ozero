# ReactOS review: what to reuse, what to distrust

- Status: draft ready (architecture review; no code inspected beyond the public architecture page)
- Evidence: quotes and facts below from the ReactOS architecture page; no ReactOS code copied or built
- Decisions needed: whether ReactOS source may be read as a reference at all under the project's licence policy (R24, Q8); whether to reuse its test suite or Wine's
- Risks: GPL contamination; NT 5.2 baseline mismatch with modern applications; assuming ReactOS behaviour equals Windows behaviour
- Next step: legal review of reference-only use, then pick two subsystems (loader, object manager) for a read-only design comparison

## What ReactOS is, in its own words

- It describes itself as "the only complete implementation of an NT kernel outside
  of the Microsoft campus", with "complete implementations of the Object manager,
  IO manager, Executive, NT thread scheduler, and many more".
- It targets **NT 5.2** (the Windows Server 2003 / XP x64 era) and supports x86 and
  x64. It ships several HALs and its own bootloader, which can boot both ReactOS and
  Windows kernels.
- It describes the Win32 API as "the lowest layer of user mode", split into
  `kernel32.dll` (threads, processes, kernel services such as file interaction),
  `gdi32.dll` (drawing) and `user32.dll` (scroll bars, buttons), plus many other
  libraries.
- It states that compatibility means implementing Windows bugs too: "not only
  implementing the features so they operate exactly as they do on Windows, but also
  implementing their bugs too", with the exception of security-relevant quirks.
- It maintains a large test suite ("millions of tests") for this purpose.
- It implements system processes (winlogon, lsass, smss, service control manager)
  and a shell (`shell32`, `browseui`, explorer) that uses COM heavily.
- On drivers, it says it does not work on third-party drivers, only the drivers
  shipped with the operating system: storage stack, filesystem drivers, bus, class
  and miniport drivers, filter manager.

## What A2 can legitimately reuse

| Area | Why it is useful | How to use it safely |
| --- | --- | --- |
| NT layering | A working example of object manager, I/O manager, executive and scheduler boundaries | read the architecture, not the code, until legal review |
| Win32-as-user-mode | Confirms that `kernel32`/`user32`/`gdi32` are ordinary DLLs over `ntdll` | cite as architectural evidence; matches Microsoft's own layering |
| Bug-for-bug compatibility | A precise statement of the problem A2 will face | adopt the principle; do not copy specific quirks |
| Test suite | Broad API conformance coverage | decide whether to use ReactOS's suite or Wine's; both are licence-sensitive |
| System process model | Shows what `smss`, `lsass`, `winlogon` and SCM do | informs scope; none of it is needed for Stage 7 |

## What does not transfer

| Gap | Detail | Consequence for A2 |
| --- | --- | --- |
| NT 5.2 baseline | Modern applications target Windows 10/11 API sets, not NT 5.2 exports | ReactOS coverage is not evidence of modern compatibility |
| API sets | `api-ms-win-*` contracts and the schema are a Windows 7+ mechanism | A2 must implement its own schema; ReactOS is not a model for it |
| Graphics | ReactOS implements its own Win32 subsystem; D3D11/D3D12 and modern DXGI are not covered | the graphics plan must come from WDDM documentation, not ReactOS |
| .NET | Modern .NET (Core) hosting and self-contained deployment are not addressed | CLR hosting is A2's own decision (Q5) |
| Security features | CFG, HVCI, ACG, CET, modern signing and integrity levels postdate the baseline | a modern baseline needs these decisions independently |
| Kernel drivers | ReactOS implements NT drivers, but its driver set is its own | third-party kernel drivers remain out of scope |
| WOW64 / 32-bit on 64-bit | Maturity differs from Windows | do not infer WOW64 behaviour from ReactOS |
| Performance and SMP | Different scheduler and memory manager | no performance conclusions |

## Licence risk

ReactOS is distributed under the GNU GPL (version 2 for the kernel and most
components; some parts are LGPL). Any code copied into a product with a different
licence would force that product's licence. Reading for architectural
understanding is a different activity from copying, but the boundary should be
set by legal review, not by an engineer's judgement in a pull request.

Practical rules until that review exists:

1. Do not copy source, comments, identifiers or data tables.
2. Do not paste ReactOS code into design documents as "reference implementation".
3. Cite public documentation and observed behaviour instead.
4. If a specific algorithm must be studied, record the file and commit hash in the
   risk register and obtain written approval.

## The fallacy to avoid

"If ReactOS does X, Windows does X" is not sound. ReactOS targets an older
baseline and has its own bugs. The reverse is also unsound: "Windows does X, so a
compatibility layer must do X" ignores that some behaviour is unobservable or
irrelevant to applications. The corpus is the arbiter.

## Verdict

ReactOS is a **reference for architecture and test strategy**, not a blueprint and
not a source of code. Its most useful contribution to A2 is negative: it shows how
large the NT surface is, and it shows that "Win32 on top of an NT-like kernel" is
a multi-year effort even with a 2003-era target.

## Evidence

- No ReactOS code was built, run or copied for this document.
- The architectural claims above are quotations or close paraphrases of the
  ReactOS architecture page, listed under Sources.

## Sources

- ReactOS architecture: <https://reactos.org/architecture/>
- ReactOS project and source: <https://github.com/reactos/reactos>
- Wine project (the other major public NT/Win32 reimplementation): <https://www.winehq.org/>
- Microsoft, PE format specification: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, WDDM overview: <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide>
