# Risk report

- Status: draft ready (register only; no mitigation implemented)
- Evidence: risk IDs referenced from [00](00-compatibility-scope.md)–[09](09-open-questions.md); public sources listed at the end; measured image data in [01-pe-loader-requirements.md](01-pe-loader-requirements.md)
- Decisions needed: which risks are accepted (R4 anti-cheat, R5 DRM, R11 32-bit, R21 kernel drivers) and who owns each acceptance
- Risks: this document is about risks; the top five are undocumented behaviour, kernel-mode requirements, graphics scope, .NET hosting and overclaiming
- Next step: assign owners and review dates, then convert R1–R6 into detection tests in the Stage 7 plan

## Scoring

- **Likelihood**: low / medium / high.
- **Impact**: low / medium / high / critical (critical = invalidates the current architecture).
- **Status**: open, mitigated by design, accepted, or closed by evidence.

A risk is only closed when a test in the corpus demonstrates the mitigation.

## Register

| ID | Risk | Class | Likelihood | Impact | Detection | Mitigation | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| R1 | Undocumented Windows behaviour is required for real applications | technical | high | high | corpus failures that cannot be explained from documentation | treat Windows binaries and headers as oracles; record each deviation; prefer the documented behaviour and note the gap | open |
| R2 | The PE specification is incomplete by its own admission | technical | high | medium | structure checks that disagree with real binaries | validate against real images (done for 8); follow `winnt.h` where prose is wrong; see gaps G1–G9 | mitigated by design |
| R3 | Kernel-mode drivers are required by many applications | architecture | high | critical | application refuses to start without a driver | state the limitation; support only user-mode paths; consider UMDF as the sole driver framework | accepted (limitation) |
| R4 | Kernel anti-cheat rejects any non-Windows environment | architecture / legal | high | critical | game refuses to launch or bans | do not target; document per product; never claim support | accepted (exclusion) |
| R5 | DRM and tamper protection reject the layer | architecture / legal | high | high | licence or integrity check failure | do not target; record per product; avoid patching signed modules | accepted (exclusion) |
| R6 | Graphics compatibility is much larger than Win32 compatibility | scope | high | critical | D3D device creation fails; frames do not present | split Class G from Class U; start with D3D11 via a translation layer; define feature levels explicitly | open |
| R7 | .NET hosting is a runtime project, not an API project | scope | high | high | managed entry point never reached | decide host-the-runtime vs reimplement (Q5); reproduce the loader's `_CorValidateImage` behaviour | open |
| R8 | API set schema is undocumented and changes per Windows build | technical | high | medium | import resolution failures for `api-ms-win-*` | ship a versioned schema; test resolution of the 7 CRT contracts in the fixtures | open |
| R9 | Syscall numbers change between Windows builds | technical | high | high | calls land on the wrong service after an update | own the `ntdll` boundary (option B in [02](02-nt-win32-dependency-map.md)) | mitigated by design (proposed) |
| R10 | 32-bit support is a second architecture | scope | medium | high | PE32 images rejected | decide timing (Q2); keep the loader format-agnostic; measure with `7zr.exe` | open |
| R11 | SxS activation contexts and common controls | technical | medium | medium | wrong-looking UI or hard failure to load `comctl32` v6 | implement manifests and activation contexts before the GUI corpus | open |
| R12 | COM/RPC and ALPC are large subsystems | scope | medium | high | `CoCreateInstance` or RPC failures | start with in-process COM; defer cross-process RPC | open |
| R13 | Security model parity (tokens, integrity levels, UAC) | technical / security | medium | high | applications behave differently under access checks | implement access checks early; do not silently grant access | open |
| R14 | Malformed PE images become an attack surface | security | high | high | crashes, hangs or memory corruption in the loader | bounds checks, scan caps, fuzzing before loader work | mitigated by design (parser) |
| R15 | Signature and integrity policy conflicts with compatibility | security / legal | medium | medium | signed-module verification failures | decide policy (Q7); never modify signed images in memory | open |
| R16 | Anti-cheat and DRM may treat the layer as tampering | legal / security | medium | critical | account bans or refusals | exclusion policy; no `ntdll` patching; clear documentation | accepted (exclusion) |
| R17 | Performance is far below native | technical | medium | medium | startup time and frame time measurements | defer until correctness; design for zero-copy where possible | open |
| R18 | Debuggability: failures are hard to attribute | engineering | high | medium | time spent per failure | structured loader traces; API traces; per-entry artifacts | open |
| R19 | Overclaiming in reports and demos | process | high | high | claims without corpus evidence | honesty rules in the corpus lint; L0–L4 levels; docs checker | mitigated by design |
| R20 | Scope creep into kernel drivers, games and DRM | process | high | high | roadmap growth without tests | explicit non-goals; milestone ladder in [07](07-stage7-milestone.md) | open |
| R21 | Windows updates break assumptions | technical | high | medium | corpus regressions after host updates | version-pinned corpus; re-run on update; no reliance on syscall numbers | open |
| R22 | Test corpus legality and redistribution | legal | medium | high | takedown or licence violation | no binary redistribution; hash pinning; proprietary entries analysis-only | mitigated by design |
| R23 | Supply-chain risk in downloaded test binaries | security | medium | high | tampered test binary | https-only downloads, SHA-256 pinning, no execution of unpinned files (lint-enforced) | mitigated by design |
| R24 | Licence contamination from reference implementations | legal | medium | high | GPL code entering a non-GPL product | read-only reference use; separate repositories; legal review before copying | open |
| R25 | 16-bit and legacy installers | scope | low | low | old installers fail | declare out of scope | accepted |
| R26 | Kernel-mode driver *support* would change the threat model | security | low | critical | a compat layer that loads arbitrary drivers | never load third-party kernel drivers; UMDF only | accepted |

## The five risks that decide the project

### 1. Undocumented behaviour (R1, R2)

The PE specification itself says it "is not guaranteed to be a complete
specification in all respects". Applications depend on behaviour that is not in
any specification: heap layout, handle values, error ordering, timing. The
mitigation is not more reading; it is a growing corpus with recorded deviations.
The measured structure survey in [01](01-pe-loader-requirements.md) already found
one concrete discrepancy (load config layout in the specification vs `winnt.h`).

### 2. Kernel-mode requirements (R3, R4, R5, R26)

Kernel drivers, anti-cheat and DRM cannot be added by writing more user-mode
code. BattlEye's public FAQ shows the shape of the problem: a kernel driver,
SHA-2 signing requirements, refusal to run with kernel debugging enabled, refusal
to support test-signing mode, and incompatibility with hardware-enforced stack
protection until fixed. A compatibility layer that is not a signed Windows kernel
cannot satisfy those checks. The correct response is an explicit exclusion, not a
workaround.

### 3. Graphics scope (R6)

The WDDM architecture puts a user-mode driver between the D3D runtime and a
kernel-mode display miniport driver. A compatibility layer has two honest options:
implement a translation layer to a host graphics API (the Wine/DXVK approach), or
implement a driver model. These are different projects. The corpus starts with
D3D11 precisely because D3D12 raises the bar again (descriptor heaps, fences,
shader model 6.x).

### 4. .NET (R7)

A managed assembly is a PE image whose entry point is rewritten by the loader via
`mscoree.dll` and `_CorValidateImage`; on 64-bit Windows the image is transformed
from PE32 to PE32+ in memory. A layer that only parses the COR20 header has not
done .NET compatibility. Hosting the Microsoft runtime is the pragmatic path;
reimplementing the CLR is a multi-year project.

### 5. Overclaiming (R19)

The cheapest failure mode is a report that says "we run Windows applications"
after a `MessageBoxW` appears. The repository's answer is mechanical: the corpus
manifest cannot record a pass without an execution record, and the lint fails if
it tries. The L0–L4 ladder exists so that every sentence can name its level.

## Risk-to-evidence mapping

| Risk | What would close it | Where the evidence would live |
| --- | --- | --- |
| R1 | documented deviations with tests | corpus notes plus `docs/evidence/` |
| R2 | structure checks over a wider binary set | `tools/pe_structs_check.py` output |
| R3 | a documented list of unsupported applications and why | risk report appendix |
| R4/R5 | per-product exclusion list | risk report appendix |
| R6 | D3D11 L3 result | corpus entry `imgui-d3d11` |
| R7 | managed hello at L2 | corpus entry `a2-dotnet-hello` |
| R8 | API set resolution test | loader trace for the fixtures |
| R9 | stable boundary test across two host builds | A1 interface tests |
| R19 | lint + docs check in CI | `make check` |

## Sources

- Microsoft, PE format specification (incompleteness statement): <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, Managed execution process (`_CorValidateImage`, PE32→PE32+): <https://learn.microsoft.com/en-us/dotnet/standard/managed-execution-process>
- Microsoft, WDDM overview (runtime, user-mode driver, kernel-mode miniport): <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide>
- Microsoft, Windows Driver Frameworks (KMDF/UMDF): <https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/>
- Microsoft, WOW64 implementation details: <https://learn.microsoft.com/en-us/windows/win32/winprog64/wow64-implementation-details>
- BattlEye FAQ (kernel driver, signing, debug, test-signing, stack protection): <https://www.battleye.com/faq>
- ReactOS architecture (licence and scope reference): <https://reactos.org/architecture/>
