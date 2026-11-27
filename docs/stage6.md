# Stage 6 — Native Graphical Application Environment

Status: **resumed by the Project Director on 2026-11-27.** Phases 6.1–6.7 are implemented; the desktop now activates in QEMU using PMM-backed large surfaces. CI passed the BIOS/UEFI × qemu64/Haswell-v1/max × SMP 1/2 boot matrix. The independent screendump check verifies the green launcher, purple O0 assistant, right panel and taskbar. Full Stage 6 acceptance remains open until keyboard/pointer interaction and interactive-window behavior are evidenced end to end.
Last reviewed: 2026-11-27
Repository: `Momegaii/Ozero`
Working branch: `main` — the only branch of record; Phases 6.4–6.8 merged at `7e26d26`

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
| 6.7 | Custom O0 desktop UI | Dark minimal desktop with neon-green sphere launchers, purple assistant spheres, green particles, node network, bottom taskbar, right panel, system tray, O0 assistant interface; sphere state machine (idle/hover/active/feedback/exit); host-tested; wired into desktop_main |
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
- Confirmed the kernel declares a Limine framebuffer request and resolves its physical address before replacing the bootloader page tables.
- The kernel page-table builder now carves framebuffer-containing 2 MiB direct-map chunks into 4 KiB UC mappings; the old Limine mapping is replaced at the CR3 switch.
- The display API verifies that the pixel extent is covered by a framebuffer memory-map region and writes only through the new direct-map mapping.

### Actual implementation status

**Verified.** The kernel resolves the Limine framebuffer virtual address to a physical address before switching page tables. The new direct-map builder detects memory-map regions typed as framebuffer and maps every intersecting 2 MiB chunk with 4 KiB UC entries; ordinary chunks retain 2 MiB WB mappings. This avoids a simultaneous WB direct-map alias of framebuffer bytes. `o0_display_init` checks that the complete pixel extent lies inside a framebuffer memory-map region, obtains the kernel direct-map address, validates the descriptor, and draws an eight-band colored boot pattern. The 2 MiB chunk containing the framebuffer is conservatively UC in full.

Verification evidence:
- Host tests pass: `make test` builds and runs `tests/display_test.c` (descriptor validation, overflow, unsupported formats, valid RGB layouts).
- QEMU BIOS boot reaches `OZERO: framebuffer discovery valid` and `OZERO: framebuffer presentation active physical=0xfd000000 bytes=4096000` (1280×800×32bpp).
- Memory-map entry `[16] type=framebuffer` confirms the extent is covered.
- A QEMU screenshot of the eight-band boot pattern was captured (`/artifacts/stage6/screenshot.png`).
- Exact commands: `make doctor`, `make test`, `make iso`, `bash tools/qemu-smoke.sh`; all pass.

Phase 6.1 is complete.

### Phase 6.1 acceptance checklist

- [x] Limine framebuffer request declared; response metadata is validated.
- [x] Framebuffer dimensions, pitch, format and non-overlapping channel masks validated in the descriptor API.
- [x] Framebuffer extent arithmetic checked for overflow and the full extent checked against a framebuffer memory-map region (verified: QEMU memory-map entry `[16] type=framebuffer`).
- [x] Direct-map framebuffer chunks are mapped UC at 4 KiB granularity, avoiding a WB alias in the new kernel page tables (verified).
- [x] Minimal eight-band software boot pattern renderer authored (visible output confirmed via QEMU screenshot).
- [x] Host tests cover valid and invalid framebuffer descriptors (`tests/display_test.c`, passes under `make test`).
- [x] QEMU boot log confirms the selected display path (`OZERO: framebuffer presentation active`).
- [x] QEMU screenshot demonstrates stable visible output (eight-band boot pattern).
- [x] Exact build/test/QEMU commands and results recorded (`make doctor`, `make test`, `make iso`, `bash tools/qemu-smoke.sh`).

Phase 6.1 is complete only when all applicable checklist items above have evidence.

## Phase 6.2 — Keyboard and pointer input

### Actual implementation status

**Implemented.** `include/ozero/input_ps2.h` and `kernel/src/input_ps2.c` add a PS/2 driver for QEMU bring-up (keyboard on IRQ1, mouse on IRQ12) built on top of `include/ozero/input.h` and `kernel/src/input.c`. The driver:

- Initializes the i8042 controller: disables both devices, runs controller self-test, selects scan code set 2, enables keyboard streaming and mouse streaming mode.
- Registers IRQ1 (keyboard) and IRQ12 (mouse) handlers via `o0_irq_register`; handlers read the data port and publish decoded events into the bounded input queue. Interrupt context performs no allocation, blocking, or rendering.
- Decodes scan code set 2 make/break, extended (E0) prefixes, modifier state (shift/ctrl/alt), and caps/num lock toggles; modifier state is carried in `event.buttons`.
- Decodes 3-byte mouse packets: button state and signed x/y deltas with sign extension.
- Provides `o0_input_poll` for event consumption and `o0_input_dropped` for overflow accounting.

`tests/input_ps2_test.c` covers key make/break, extended codes, modifiers, toggle locks, mouse packet decoding, and queue overflow; it is registered in the host-test target. No input-to-window routing is implemented yet (that belongs to the window manager / compositor phases).

- [x] Bounded event queue API and overflow behavior authored (`kernel/src/input.c`, `tests/input_test.c`).
- [x] Keyboard hardware driver (PS/2, IRQ1, scan code set 2).
- [x] Pointer hardware driver (PS/2 mouse, IRQ12, stream mode).
- [x] Key state/modifier and pointer packet decoding.
- [x] IRQ-safe event production (interrupt context only pushes to the queue).
- [ ] Consumer integration with window manager (input-to-window routing).
- [ ] QEMU interaction evidence from actual key/pointer input (driver initialized at boot; no events generated without a VNC/USB input device attached).

Driver initialization is confirmed in the QEMU boot log: `OZERO: input: PS/2 keyboard+mouse initialized`.

## Phase 6.3 — Surfaces and graphics primitives

### Implemented in the repository (not yet verified)

- `include/ozero/surface.h`: non-owning surface descriptor and RGBX8888/BGRX8888 API.
- `kernel/src/surface.c`: descriptor validation, full-surface fill, clipped rectangle fill, clipped blit and pixel-format conversion.
- `tests/surface_test.c`: host test source covering descriptor errors, fill, clipping, format conversion and overlapping self-blits.
- `Makefile`: links the surface implementation into the kernel and adds the host surface test to `make test`.

The surface layer does not allocate memory, map physical memory, or access a boot framebuffer. It operates only on caller-supplied pixel buffers. The display layer owns framebuffer mapping and presentation; the compositor will later present its output through that boundary.

**Verification state:** Host-tested and passing. `make test` builds and runs `tests/surface_test.c` (descriptor errors, fill, clipping, format conversion, overlapping self-blits). The kernel build (`make iso`) links `surface.o` cleanly with `-Werror`. No QEMU display test has been run against the surface layer directly (the framebuffer is exercised by Phase 6.1); treat clipping and large-dimension edge cases as covered by `surface_test.c`.

## Phase 6.7 — Custom O0 desktop UI

### Intended implementation

A dark, minimal 2D desktop rendered in software onto the backbuffer, replacing the previous solid-color shell. The design is specified in [ADR 0020](decisions/0020-custom-desktop-ui.md).

1. **Black background** (`0x000000`) across the full backbuffer, drawn first each frame.
2. **Neon-green sphere launchers** (`0x00FF00`) — solid filled circles, no pictograms inside.
3. **Purple spheres** (`0x800080`) reserved for AI/special functions (O0 assistant).
4. **Green particles** drifting on the black background, fading over lifetime.
5. **Node network** — thin lines between sphere centers, brightening on interaction.
6. **Bottom taskbar** — horizontal strip with spherical launchers.
7. **Right-side information panel** — vertical panel with status area.
8. **System tray** — bottom-right with clock placeholder and status indicators.
9. **O0 assistant interface** — purple assistant sphere that opens an assistant panel on activation.

### Sphere interaction state machine

| State | Visual | Trigger |
|---|---|---|
| Idle | Solid sphere at base radius | Default |
| Hover | Slightly enlarged, brighter | Pointer within hover radius |
| Active | Expanding neon circular halo | Click / activation |
| Feedback | Connected lines illuminate + ring | Activation complete |
| Mouse exit | Smooth return to idle | Pointer leaves hover radius |

### Rendering layer

All drawing uses `o0_surface_*` primitives on the backbuffer:
- `o0_surface_fill` for black background and panel/taskbar blocks.
- `o0_surface_fill_circle` for solid spheres.
- `o0_surface_draw_circle` for halos and rings.
- `o0_surface_fill_rect` for panel, taskbar, tray blocks.
- `o0_surface_blend_rgb` for particle alpha blending.
- `o0_surface_put_pixel` for node lines and borders.

Draw order per frame: background → particles → node network → spheres → taskbar → panel → tray → assistant panel. Windows are blitted on top after the desktop UI render.

### Input integration

Pointer position comes from the shell (`o0_shell_pointer_x/y`), updated by the input router from PS/2 mouse events. Hover testing uses circle hit-test: `(px - cx)^2 + (py - cy)^2 <= (radius + margin)^2`. Activation (`o0_desktop_ui_activate`) transitions a sphere to Active state and, for assistant spheres, opens the assistant panel.

### Actual implementation status

**Implemented.** `kernel/src/desktop_ui.c` and `include/ozero/desktop_ui.h` implement the full desktop UI subsystem. `kernel/src/main.c` `desktop_main()` calls `o0_desktop_ui_init`, `o0_desktop_ui_pointer`, `o0_desktop_ui_update`, and `o0_desktop_ui_render` each frame before blitting windows to the backbuffer and scanout.

Verification evidence:
- Host tests pass: `tests/desktop_ui_test.c` covers init, sphere colors, hover state machine, activation (idle→active→feedback→idle), assistant panel open, particle bounds, black background, neon-green sphere rendering, taskbar/panel/tray rendering, feedback ring, `o0_surface_fill_circle`, `o0_surface_draw_circle`, and `o0_surface_blend_rgb`. Passes under `make test` and `make test-sanitize`.
- `kernel/src/surface.c` adds `o0_surface_fill_circle`, `o0_surface_draw_circle`, `o0_surface_blend_rgb`, and `o0_surface_put_pixel`. `tests/surface_test.c` continues to pass.
- Kernel builds cleanly with `-Werror` (`make kernel`).
- No ELF, Linux, X11, Wayland, or external GUI assumptions; Ozero-native software rendering only.

Phase 6.7 is complete.

### Phase 6.7 acceptance checklist

- [x] Dark black background rendered each frame.
- [x] Neon-green sphere launchers, solid color, no pictograms.
- [x] Purple spheres for AI/special functions (O0 assistant).
- [x] Green particles drifting and fading on black background.
- [x] Node network lines between spheres, brightening on interaction.
- [x] Bottom taskbar with spherical launchers.
- [x] Right-side information panel.
- [x] System tray with clock placeholder and status indicators.
- [x] Integrated O0 assistant interface (assistant panel opens on assistant sphere activation).
- [x] Sphere state machine: idle → hover → active → feedback → idle.
- [x] Host tests pass (`tests/desktop_ui_test.c`, `make test` and `make test-sanitize`).
- [x] Kernel builds with `-Werror` (`make kernel`).

## Remaining phases and agent handoff

The desktop implementation is integrated. CI now runs host tests, sanitizer tests, the BIOS/UEFI boot matrix and an independent QEMU screendump check. The desktop activation marker and visible launcher/assistant/panel/taskbar pixels pass. Full Stage 6 acceptance is still open: keyboard/pointer input must be exercised end to end, routed into windows, and demonstrated with interactive windows. The agent roles below are retained to close those remaining gaps.

### Agent A — Display and input (Phases 6.1 and 6.2)

**Phase 6.1 display — COMPLETE.**
- Add and validate the Limine framebuffer request.
- Validate address, width, height, pitch, bpp, memory model and channel masks.
- Check byte-extent arithmetic and verify the range against the boot memory map.
- UC direct-map carveout for framebuffer chunks; no WB alias.
- Minimal eight-band fallback status screen; no GPU acceleration.
- Verified: host tests pass, QEMU BIOS boot reaches `OZERO: framebuffer presentation active`, screenshot captured.

**Phase 6.2 input — IMPLEMENTED.**
- Define a bounded input-event ABI and queue with overflow behavior (`kernel/src/input.c`, `tests/input_test.c`).
- Implement keyboard and pointer drivers for QEMU (`kernel/src/input_ps2.c`): PS/2 keyboard on IRQ1 (scan code set 2), PS/2 mouse on IRQ12 (stream mode).
- Decode keyboard make/break and modifier state; decode pointer movement/buttons and signed deltas.
- Route IRQ data into a safe event path (interrupt context only pushes to the queue; no allocation, blocking, or rendering).
- Provide polling/event-consumption APIs (`o0_input_poll`, `o0_input_dropped`).

**Deliverables:** source, public headers (`include/ozero/input_ps2.h`), boot diagnostics (`OZERO: input: PS/2 keyboard+mouse initialized`), host tests (`tests/input_ps2_test.c`), QEMU-visible fallback screen (Phase 6.1).

**Remaining for Phase 6.2:** consumer integration with the window manager (input-to-window routing) and end-to-end QEMU interaction evidence from actual key/pointer input.

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
- Custom O0 desktop UI implemented per [ADR 0020](decisions/0020-custom-desktop-ui.md).
- Neon-green sphere launchers (solid color, no pictograms), purple assistant spheres,
  green particles, node network, bottom taskbar, right panel, system tray, O0 assistant.
- Sphere state machine: idle → hover → active → feedback → idle.
- See Phase 6.7 section below for full specification and acceptance checklist.

**Phase 6.8 integration**
- Integrate Agent A and B APIs without bypassing their ownership boundaries.
- Add repeatable QEMU configuration and serial diagnostics.
- Capture a screenshot of the desktop.
- Record exact commands, toolchain versions, test output, boot logs and known limitations.

**Deliverables:** a coherent QEMU desktop path and an evidence bundle. This agent owns integration only after the display/input and window/compositor contracts stabilize.

## Remaining Stage 6 acceptance campaign

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

Close the remaining input and application-interaction gaps: drive real QEMU keyboard/pointer events, verify routing and focus, and demonstrate at least two interactive windows. Preserve the passing BIOS/UEFI boot matrix and screendump checks as regression gates. Stage 6 remains open until those interaction results and their serial/screenshot evidence are recorded.