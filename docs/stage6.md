# Stage 6 — Native Graphical Application Environment

Status: **active; Phase 6.1 safe direct-map framebuffer path and boot pattern authored (unverified); Phase 6.3 surface primitives authored; Phases 6.2 and 6.4–6.8 remain open; no Stage 6 tests have been run**
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
- Confirmed the kernel entry point declares a Limine framebuffer request and validates the returned metadata after the paging switch. Pixel access remains disabled.
- Confirmed `include/ozero/display.h` and `kernel/src/display.c` provide a metadata descriptor and validator, but no framebuffer mapping or pixel presentation API exists yet.
- Identified an important safety constraint: the current kernel uses a write-back direct map. A framebuffer must not be written through that alias until a safe framebuffer mapping/cache policy is implemented.

### Actual implementation status

**Implementation authored; not verified; phase remains open.** The kernel resolves the Limine framebuffer virtual address to a physical address before switching page tables. The new direct-map builder detects memory-map regions typed as framebuffer and maps every intersecting 2 MiB chunk with 4 KiB UC entries; ordinary chunks retain 2 MiB WB mappings. This avoids a simultaneous WB direct-map alias of framebuffer bytes. `o0_display_init` checks that the complete pixel extent lies inside a framebuffer memory-map region, obtains the kernel direct-map address, validates the descriptor, and draws an eight-band colored boot pattern. The 2 MiB chunk containing the framebuffer is conservatively UC in full. No build, host tests, QEMU boot, or screenshot has been run, so successful visible output is not yet confirmed. Phase 6.1 remains open.

### Phase 6.1 acceptance checklist

- [x] Limine framebuffer request declared; response metadata is validated.
- [x] Framebuffer dimensions, pitch, format and non-overlapping channel masks validated in the descriptor API.
- [ ] Framebuffer extent arithmetic checked for overflow and checked against the boot memory map.
- [x] Direct-map framebuffer chunks are mapped UC at 4 KiB granularity, avoiding a WB alias in the new kernel page tables (implementation authored; unverified).
- [x] Minimal eight-band software boot pattern renderer authored (visible output not yet confirmed).
- [ ] Host tests cover valid and invalid framebuffer descriptors (test source added; execution reserved for agent test pass).
- [ ] QEMU boot log confirms the selected display path.
- [ ] QEMU screenshot demonstrates stable visible output.
- [ ] Exact build/test/QEMU commands and results recorded.

Phase 6.1 is complete only when all applicable checklist items above have evidence. A bootloader framebuffer descriptor alone is not proof that Ozero can safely render to it.

## Phase 6.2 — Keyboard and pointer input

### Actual implementation status

**Partial; not complete.** `include/ozero/input.h` and `kernel/src/input.c` provide a bounded single-producer/single-consumer event queue with release/acquire publication and an explicit full-queue result. `tests/input_test.c` now covers empty/FIFO behavior, capacity, overflow accounting and invalid event types; it is registered in the host-test target. No PS/2 or USB HID discovery, keyboard scan-code decoder, pointer packet decoder, IRQ driver, or input-to-window routing is implemented. The queue is infrastructure, not a working keyboard or mouse.

- [x] Bounded event queue API and overflow behavior authored.
- [ ] Keyboard hardware discovery and driver.
- [ ] Pointer hardware discovery and driver.
- [ ] Key state/modifier and pointer packet decoding.
- [ ] IRQ-safe event production and consumer integration.
- [ ] QEMU interaction evidence.

## Phase 6.3 — Surfaces and graphics primitives

### Implemented in the repository (not yet verified)

- `include/ozero/surface.h`: non-owning surface descriptor and RGBX8888/BGRX8888 API.
- `kernel/src/surface.c`: descriptor validation, full-surface fill, clipped rectangle fill, clipped blit and pixel-format conversion.
- `tests/surface_test.c`: host test source covering descriptor errors, fill, clipping, format conversion and overlapping self-blits.
- `Makefile`: links the surface implementation into the kernel and adds the host surface test to `make test`.

The surface layer does not allocate memory, map physical memory, or access a boot framebuffer. It operates only on caller-supplied pixel buffers. This deliberate boundary prevents Phase 6.3 from bypassing the still-open display mapping/cache-safety work in Phase 6.1.

**Verification state:** These changes were committed to the working branch, but no build, host test, QEMU boot, or display test was run after the changes, as requested. Treat the test source as authored—not as passing evidence. Review overlap behavior and large-dimension edge cases during the agent test pass.

## Remaining phases and agent handoff

The Project Director requested that implementation/documentation proceed without running the Stage 6 test campaign. The following work is not claimed complete. Three agents should implement/review in parallel where dependencies permit, then run the consolidated test plan below.

### Agent A — Display and input (Phases 6.1 and 6.2)

**Phase 6.1 display**
- Add and validate the Limine framebuffer request.
- Validate address, width, height, pitch, bpp, memory model and channel masks.
- Check byte-extent arithmetic and verify the range against the boot memory map.
- Implement an explicit safe framebuffer mapping/cache policy. Do not write through the existing write-back HHDM alias without proving compatible memory type and alias handling.
- Connect the validated display descriptor to the Phase 6.3 surface API.
- Implement a minimal fallback status screen; no GPU acceleration is required.

**Phase 6.2 input**
- Define a bounded input-event ABI and queue with overflow behavior.
- Implement keyboard and pointer discovery and drivers appropriate to the QEMU machine configuration.
- Decode keyboard make/break and modifier state; decode pointer movement/buttons and signed deltas.
- Route IRQ data into a safe event path; do not perform allocation, blocking, or rendering in interrupt context.
- Provide polling/event-consumption APIs for the window manager.

**Deliverables:** source, public headers, boot diagnostics, host tests, and a QEMU-visible fallback screen plus working keyboard/pointer events.

### Agent B — Window manager and compositor (Phases 6.4 and 6.5)

**Phase 6.4 window manager**
- Define window identity, ownership, lifecycle and bounded window count.
- Implement create/destroy, move/resize, visibility, z-order, focus and hit testing.
- Validate all geometry with overflow-safe clipping.
- Route pointer and keyboard events to the focused window.
- Define a simple client surface ownership/lifetime contract using the Phase 6.3 surface API.

**Phase 6.5 software compositor**
- Composite visible windows in z-order into a backbuffer.
- Implement damage rectangles and bounded redraw.
- Handle clipping, off-screen windows, overlapping windows and format conversion.
- Present through the display abstraction from Agent A; keep rendering independent of hardware acceleration.
- Define behavior for allocation failure and invalid client surfaces.

**Deliverables:** window manager and compositor APIs, implementation, deterministic host tests, and a QEMU scene with overlapping windows.

### Agent C — Acceleration boundary, desktop shell and integration (Phases 6.6–6.8)

**Phase 6.6 graphics acceleration boundary**
- Define capability discovery and a backend interface for surface allocation, blit/fill and presentation only where genuinely supported.
- Keep the software renderer as the mandatory fallback.
- Do not advertise physical GPU, virtual GPU, Intel, AMD or NVIDIA acceleration without a separately verified implementation.
- Keep unsupported operations explicit; no silent success stubs.

**Phase 6.7 desktop shell**
- Add a desktop background, pointer cursor, window decorations, focus indication and a minimal launcher.
- Provide at least two simple interactive demo applications/windows.
- Wire input to window focus and application actions.

**Phase 6.8 integration**
- Integrate Agent A and B APIs without bypassing their ownership boundaries.
- Add repeatable QEMU configuration and serial diagnostics.
- Capture a screenshot of the desktop.
- Record exact commands, toolchain versions, test output, boot logs and known limitations.

**Deliverables:** a coherent QEMU desktop path and an evidence bundle. This agent owns integration only after the display/input and window/compositor contracts stabilize.

## Consolidated test campaign (agents run this; not run in this change set)

1. Clean freestanding kernel and ISO build with warnings as errors.
2. All existing host suites plus `surface_test`; run with AddressSanitizer and UndefinedBehaviorSanitizer where supported.
3. Header self-containment and undefined-symbol/link audit.
4. QEMU BIOS and UEFI/OVMF boot where the repository supports it.
5. Framebuffer validation cases: null response, unsupported format, zero dimensions, undersized pitch, overflow, out-of-map range and valid RGB/BGR layouts.
6. Input tests: queue full/empty, overflow, key press/release, modifiers, pointer sign extension, button transitions and IRQ-safe delivery.
7. Surface tests: clipping, pitch padding, format conversion, self-overlap in all directions, invalid descriptors and arithmetic boundaries.
8. Window tests: lifecycle, z-order, focus, hit testing, geometry clipping and event routing.
9. Compositor tests: overlapping windows, damage coalescing, partial redraw, off-screen clipping and invalid surfaces.
10. End-to-end QEMU: stable fallback screen, pointer movement, keyboard interaction, launcher action and at least two interactive windows.
11. Capture serial log and screenshot; attach them to the Stage 6 acceptance report.

For every failure, report the exact command, commit SHA, environment, expected result, actual result and relevant log excerpt. Do not mark a phase complete merely because its source files exist.

## Stage 6 overall acceptance

Stage 6 is complete when Ozero boots into its own graphical environment, initializes a display, accepts keyboard and pointer input, creates and manages windows, composes multiple surfaces correctly, and runs at least two interactive windows in QEMU. Build, host-test, boot-log and screenshot evidence must be recorded. GPU acceleration is optional; the software display and compositor path must remain usable without it.

## Constraints and non-goals

- No physical GPU installation or hardware-target verification is required for the initial QEMU milestone.
- Do not claim Intel, AMD, NVIDIA or virtual GPU acceleration merely because a generic interface exists.
- Do not write to framebuffer memory until mapping and cache attributes are safe.
- Keep Ozero's native executable format, loader, runtime and ABI independently designed; the Limine/ELF64 kernel boot image does not define the user executable format.
- Keep vendor-specific acceleration behind explicit capability-driven backend interfaces.

## Current next action

Assign Agents A–C using the handoff above. Agent A must establish the safe display and input contracts; Agent B can develop against the Phase 6.3 surface API; Agent C should stabilize the desktop and integration only after the lower-level contracts are agreed. The Project Director's independent test agents then run the consolidated campaign. Stage 6 remains open until the acceptance evidence exists.