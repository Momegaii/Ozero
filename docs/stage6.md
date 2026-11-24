# Stage 6 — Native Graphical Application Environment

Status: **active; Phase 6.1 is scoped but not implemented**
Last reviewed: 2026-11-24
Repository: `Momegaii/Ozero`
Working branch: `kilo/floral-eagle-2hb`

## Purpose

Stage 6 delivers Ozero's first native graphical application environment. The target is a bootable desktop with a display path, keyboard and pointer input, window management, a compositor, and simple native applications. The initial implementation must work in QEMU without requiring a physical GPU or a vendor-specific accelerated backend.

This document records the Stage 6 scope accepted by the Project Director in the development conversation. It supersedes the older Stage 6 one-line description in the roadmap where that description conflicts with this graphical-environment scope. It does not change the independent Ozero-native ABI or executable-format requirements in AGENTS.md.

## Stage 6 phases

| Phase | Work | Exit evidence |
|---|---|---|
| 6.1 | Display and framebuffer | Validated boot framebuffer, safe mapping/cache policy, selected or accepted display mode, stable fallback screen, QEMU serial evidence and screenshot |
| 6.2 | Input | Keyboard and pointer discovery, event delivery and tests |
| 6.3 | Surfaces and graphics primitives | Pixel formats, surfaces, clipping, fills, blits and bounds tests |
| 6.4 | Window manager | Window lifecycle, stacking, focus, movement and resizing tests |
| 6.5 | Software compositor | Correct composition, damage tracking and redraw tests |
| 6.6 | Graphics acceleration boundary | Capability-driven interface; software fallback remains functional; no unsupported vendor backend claims |
| 6.7 | Desktop shell | Background, pointer, decorations, launcher and at least two interactive windows |
| 6.8 | Integration and acceptance | Repeatable QEMU boot, documented test output and screenshot; all Stage 6 acceptance criteria pass |

## Phase 6.1 — Display and framebuffer

### Intended implementation

1. Request the bootloader-provided framebuffer through the existing Limine boot protocol.
2. Validate the response pointer, framebuffer address, dimensions, pitch, bits per pixel, memory model and RGB channel masks before accepting it.
3. Check arithmetic overflow when computing the framebuffer byte extent and verify the extent against the boot memory map.
4. Keep discovery and metadata validation separate from framebuffer memory access.
5. Establish an explicit safe mapping and cache policy before writing to framebuffer memory. Do not write through the current write-back HHDM alias unless the architecture layer has established a compatible memory type and aliasing policy.
6. Expose a small display descriptor/API to later surface and compositor phases.
7. Add host tests for malformed descriptors, overflow, unsupported formats and valid RGB layouts.
8. Add QEMU boot diagnostics and capture a screenshot showing the fallback screen.

### Repository audit performed

- Inspected the active branch's README, roadmap, project status, boot entry point, Limine request declarations and paging implementation.
- Confirmed the kernel entry point currently requests the Limine memory map and HHDM, but does not declare a Limine framebuffer request.
- Confirmed there is no existing display/framebuffer subsystem in the inspected source paths.
- Identified an important safety constraint: the current kernel uses a write-back direct map. A framebuffer must not be written through that alias until a safe framebuffer mapping/cache policy is implemented.

### Actual implementation status

**Not complete.** The work performed so far is repository inspection, scope definition and implementation planning. No framebuffer request, display descriptor implementation, framebuffer mapping, pixel renderer, host tests, QEMU display validation or screenshot has been committed as part of Phase 6.1. Therefore Phase 6.1 must remain open.

### Phase 6.1 acceptance checklist

- [ ] Limine framebuffer request added and response validated.
- [ ] Framebuffer dimensions, pitch, format and channel masks validated.
- [ ] Framebuffer extent arithmetic checked for overflow and checked against the boot memory map.
- [ ] Safe mapping/cache policy implemented; no unsafe write-back alias used.
- [ ] Minimal fallback renderer writes a known test pattern or status screen.
- [ ] Host tests cover valid and invalid framebuffer descriptors.
- [ ] QEMU boot log confirms the selected display path.
- [ ] QEMU screenshot demonstrates stable visible output.
- [ ] Exact build/test/QEMU commands and results recorded.

Phase 6.1 is complete only when all applicable checklist items above have evidence. A bootloader framebuffer descriptor alone is not proof that Ozero can safely render to it.

## Stage 6 overall acceptance

Stage 6 is complete when Ozero boots into its own graphical environment, initializes a display, accepts keyboard and pointer input, creates and manages windows, composes multiple surfaces correctly, and runs at least two interactive windows in QEMU. Build, host-test, boot-log and screenshot evidence must be recorded. GPU acceleration is optional; the software display and compositor path must remain usable without it.

## Constraints and non-goals

- No physical GPU installation or hardware-target verification is required for the initial QEMU milestone.
- Do not claim Intel, AMD, NVIDIA or virtual GPU acceleration merely because a generic interface exists.
- Do not write to framebuffer memory until mapping and cache attributes are safe.
- Keep Ozero's native executable format, loader, runtime and ABI independently designed; the Limine/ELF64 kernel boot image does not define the user executable format.
- Keep vendor-specific acceleration behind explicit capability-driven backend interfaces.

## Next action

Implement Phase 6.1 in small reviewable steps: add the Limine framebuffer request and descriptor validation first, then implement the safe framebuffer mapping, minimal renderer, host tests and QEMU evidence.