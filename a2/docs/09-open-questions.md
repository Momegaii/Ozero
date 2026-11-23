# Open questions and decisions needed

- Status: draft ready (questions, options and recommendations; none decided)
- Evidence: each question references the document where it was discovered; recommendations are reasoned, not measured
- Decisions needed: all fifteen below
- Risks: unanswered Q1–Q4 block the Stage 7 milestone; unanswered Q5–Q7 block scope statements; see [05-risk-report.md](05-risk-report.md)
- Next step: review Q1–Q4 with A1 and A3, record decisions in this file with dates and owners

## How to use this file

Each question lists the options, a recommendation and what changes depending on
the answer. A decision is recorded by replacing "recommendation" with "decision",
adding the date and the owner. Undecided questions stay visible; they are not
silently assumed.

---

### Q1. What Windows baseline defines compatibility?

**Why it matters:** API sets, syscall behaviour, security features and even
documented functions differ between Windows 7, 10 and 11. "Compatible with
Windows" is not a specification.

Options:

- A. Windows 10 22H2 as the API baseline, modern Windows 11 accepted as a superset.
- B. Windows 11 23H2 and later only.
- C. Windows 7 SP1 (matches ReactOS's era more closely, and many old applications).

**Recommendation: A.** It is the widest base of applications still in use, and
its API set schema is stable enough to ship as data. It also avoids pretending
that NT 5.2 behaviour is enough.

**Changes if A:** the API set schema, DLL set and error codes are pinned to one
build family; corpus expectations are recorded against it.

**Owner:** A2. **Needed by:** before M0.

---

### Q2. Is 32-bit (x86) support required, and when?

**Why it matters:** the measured corpus already contains a real 32-bit binary
(`7zr.exe`: 9 546 relocations, 139 imports). WOW64 is a second user-mode
architecture with pointer-size thunking, not a flag.

Options:

- A. x64 only forever.
- B. x64 first, x86 at M2.
- C. x86 and x64 from the start.

**Recommendation: B.** It keeps M1 small while acknowledging that most legacy
applications are 32-bit. WOW64-style thunking should be designed before the
loader ABI is frozen, even if it is implemented later.

**Changes if B:** the loader interface must carry a pointer-size flag from day
one; the TEB/PEB layout must be parameterised.

**Owner:** A1 + A2. **Needed by:** before the loader ABI is frozen.

---

### Q3. What is the syscall/upcall strategy?

**Why it matters:** Windows syscall numbers change per build. Depending on them
makes the layer break on updates and look like tampering to anti-cheat.

Options:

- A. Match Windows syscall numbers for a chosen build.
- B. Own `ntdll`-equivalent plus a stable private boundary.
- C. Patch Windows `ntdll` stubs at load.

**Recommendation: B.** See [02-nt-win32-dependency-map.md](02-nt-win32-dependency-map.md)
for the comparison. Option C should remain an experiment only.

**Changes if B:** A2 must implement or source a compatible `ntdll` surface
(loader, heap, RTL, exception dispatch). That is a significant scope item and
must appear in the roadmap.

**Owner:** A1 + A2. **Needed by:** before M1.

---

### Q4. Do we ship our own `ntdll.dll`, or consume the Windows one?

**Why it matters:** many applications import `ntdll` directly, and the loader
itself is `ntdll!LdrpInitializeProcess`.

Options:

- A. Ship a compatible `ntdll.dll`.
- B. Use the Windows `ntdll.dll` and intercept below it.
- C. Hybrid: ship `ntdll` but allow a Windows one to be loaded for specific apps.

**Recommendation: A**, following from Q3-B. Option C sounds flexible but creates
two incompatible loader states.

**Changes if A:** the `ntdll` export surface becomes a first-class deliverable
with its own tests; A1 must provide every service the loader needs.

**Owner:** A2. **Needed by:** before M1.

---

### Q5. .NET: host the Microsoft runtime, or reimplement?

**Why it matters:** the loader rewrites managed entry points through `mscoree.dll`
and `_CorValidateImage`; a COR20 parser is not a CLR.

Options:

- A. Host the Microsoft .NET runtime (framework-dependent and self-contained).
- B. Reimplement a CLR subset.
- C. Support only NativeAOT/self-contained applications.

**Recommendation: A** for the first managed milestone, with C as a fallback if
hosting proves impossible. B is a multi-year project and should not be on the
critical path.

**Changes if A:** the layer needs a Windows-compatible hosting environment
(loader, memory, threads, file mapping) and a policy for runtime versions.

**Owner:** A2. **Needed by:** before M4.

---

### Q6. Graphics: translation layer or driver model?

**Why it matters:** WDDM requires a kernel-mode display miniport driver. A
user-mode layer can translate to a host API instead, but then it is not a Windows
driver model.

Options:

- A. Translation layer (D3D/DXGI to Vulkan or OpenGL).
- B. Driver model.
- C. No graphics support; console and GUI only.

**Recommendation: A**, starting with D3D11, and explicitly not claiming D3D12
support until it exists. C is a legitimate product decision and should be
considered if the goal is business applications.

**Changes if A:** A3 owns a translation interface, not a driver; performance and
format coverage become A3's metrics.

**Owner:** A3 + A2. **Needed by:** before M5.

---

### Q7. What signature and integrity policy applies?

**Why it matters:** Authenticode, CFG, HVCI and driver signing are visible to
applications and anti-cheat. Modifying signed images in memory is detectable.

Options:

- A. Verify signatures where Windows does, and refuse to run invalid images.
- B. Ignore signatures and run anything.
- C. Verify but do not enforce.

**Recommendation: A** for images that Windows itself would verify at load time,
with an explicit, logged opt-in for unsigned development images (otherwise this
repository's own fixtures would be rejected). Never modify a signed image in
memory.

**Changes if A:** the loader needs a signature verification path and a policy for
catalog-signed files.

**Owner:** A2 + security review. **Needed by:** before M1 for the console path
(most fixtures are unsigned, so a test-signing policy is needed).

---

### Q8. How may ReactOS and Wine be used as references?

**Why it matters:** both are large, useful and licence-sensitive (GPL/LGPL).

Options:

- A. Read documentation only; no source inspection.
- B. Read source for understanding; no copying; legal review before any reuse.
- C. Reuse code where licences permit.

**Recommendation: B**, with a written record of every file examined. C requires
a licence decision that is above this project's pay grade.

**Owner:** project lead + legal. **Needed by:** before any loader design review.

---

### Q9. What is the windowing strategy?

**Why it matters:** `user32`/`gdi32` on Windows are user-mode DLLs over
kernel-mode `win32k`. A compatibility layer can implement them in user mode over
a host window system, but behaviour (input, focus, DPI, composition) differs.

Options:

- A. User-mode USER/GDI over a host toolkit.
- B. A compositor with a Windows-like window manager.
- C. No GUI.

**Recommendation: A** for M3, with an explicit list of unsupported behaviours
(theming, some DPI cases, hooks).

**Owner:** A2. **Needed by:** before M3.

---

### Q10. What is the anti-cheat stance?

**Why it matters:** kernel anti-cheat is incompatible with any non-Windows
kernel, and attempting to support it creates legal and security risk.

Options:

- A. Explicitly unsupported.
- B. Best-effort, no guarantees.
- C. Investigate a compatibility mode.

**Recommendation: A.** Document it, do not test it, do not imply support.

**Owner:** project lead. **Needed by:** before any public statement about games.

---

### Q11. Which application classes are in scope for the first release?

**Why it matters:** "Windows applications" spans console tools, office suites,
browsers, games and industrial software with kernel drivers.

Options:

- A. Developer/console tools and simple GUI utilities.
- B. Business applications including .NET and installers.
- C. Games.

**Recommendation: A**, then B. C depends on Q6 and Q10 and should be stated as
out of scope for the first release.

**Owner:** product. **Needed by:** before Stage 7 exit criteria are agreed.

---

### Q12. How faithful must the security model be?

**Why it matters:** tokens, integrity levels and UAC are observable. Granting more
access than Windows would is a security bug in the layer, not a compatibility
convenience.

Options:

- A. Enforce access checks with a Windows-like model.
- B. Run everything with full access and document it.
- C. Enforce only where applications can observe it.

**Recommendation: A** for file and registry objects; B is unacceptable for
anything multi-user.

**Owner:** A1 + security review. **Needed by:** before M2.

---

### Q13. Registry and filesystem virtualisation?

**Why it matters:** many applications assume they can write to `Program Files`
and `HKLM`. Windows redirects some of this for legacy applications.

Options:

- A. No virtualisation; applications must behave.
- B. Per-application overlay for writes.
- C. Full Windows-like redirection.

**Recommendation: B** after M2, because it is cheap and avoids polluting the host.

**Owner:** A2. **Needed by:** before business-application support.

---

### Q14. Are kernel-mode drivers in scope at all?

**Why it matters:** the answer determines whether whole product categories
(anti-cheat, DRM, hardware tools, VPN clients) are addressable.

Options:

- A. No kernel drivers; UMDF only where a host equivalent exists.
- B. A Windows driver model.
- C. Case-by-case.

**Recommendation: A.** Record B as a separate project if it is ever attempted.

**Owner:** architecture. **Needed by:** now — it shapes every scope statement.

---

### Q15. What measurement threshold allows a compatibility claim?

**Why it matters:** without a threshold, any pass rate can be presented as success.

Options:

- A. Per-category: 100% of a fixed corpus at L2/L3 before claiming that category.
- B. An aggregate percentage across the corpus.
- C. No threshold; report raw numbers only.

**Recommendation: A plus C:** claim a category only when every entry in it passes,
and always publish the raw per-entry numbers. B invites cherry-picking.

**Owner:** A2 + A4. **Needed by:** before the first public report.

## Global decision IDs

A4 owns the decision register in `docs/decisions/decision-log.md`. The IDs below are
proposed for that register and are stable in A2 text until A4 records them; if A4 has
already allocated an ID, the Q-number remains the A2 key and the register wins.

| A2 key | Proposed global ID | Decision | Status |
| --- | --- | --- | --- |
| Q1 | D-009 | Windows baseline | Proposed |
| Q2 | D-010 | 32-bit timing | Proposed |
| Q3 | D-011 | Syscall/upcall strategy | Proposed |
| Q4 | D-012 | Own vs Windows ntdll | Proposed |
| Q5 | D-013 | .NET hosting | Proposed |
| Q6 | D-014 | Graphics approach | Proposed |
| Q7 | D-015 | Signature policy | Proposed |
| Q8 | D-016 | ReactOS/Wine use | Proposed |
| Q9 | D-017 | Windowing strategy | Proposed |
| Q10 | D-018 | Anti-cheat stance | Proposed |
| Q11 | D-019 | Application classes | Proposed |
| Q12 | D-020 | Security model | Proposed |
| Q13 | D-021 | Registry/filesystem virtualisation | Proposed |
| Q14 | D-022 | Kernel drivers | Proposed |
| Q15 | D-023 | Claim threshold | Proposed |

Decisions that already have a global ID are referenced directly:

- D-001 — A1/A2/A3 domain split.
- D-005 — boot protocol (A1).
- D-006 — Stage 0 acceptance.
- D-007 — A3 DMA/platform interface (A1 + A3).
- XB-002 — A1 review of the A2 process/address-space/handle/syscall contract.
- XB-003 — A1/A3 review of the A2 PE fixture contract.
- XB-009 — A2 evidence must cite the branch head that is under review.

## Decision log

| Question | Global ID | Decision | Date | Owner | Notes |
| --- | --- | --- | --- | --- | --- |
| — | — | none recorded | — | — | this table stays empty until decisions are made |

## Sources

- Microsoft, PE format specification: <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
- Microsoft, Windows API sets: <https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets>
- Microsoft, WOW64 implementation details: <https://learn.microsoft.com/en-us/windows/win32/winprog64/wow64-implementation-details>
- Microsoft, Managed execution process: <https://learn.microsoft.com/en-us/dotnet/standard/managed-execution-process>
- Microsoft, WDDM overview: <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide>
- BattlEye FAQ: <https://www.battleye.com/faq>
- ReactOS architecture: <https://reactos.org/architecture/>
