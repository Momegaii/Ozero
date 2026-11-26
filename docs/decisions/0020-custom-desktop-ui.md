# ADR 0020: Custom O0 desktop UI (Stage 6.7)

- **Status:** Accepted
- **Date:** 2026-11-26
- **Stage:** 6.7 (native graphical desktop)
- **Related:** `kernel/src/desktop_ui.c`, `include/ozero/desktop_ui.h`,
  `kernel/src/surface.c`, `kernel/src/compositor.c`, `kernel/src/shell.c`,
  `kernel/src/main.c` (desktop_main), `tests/desktop_ui_test.c`

## 1. Context

Stage 6 requires Ozero to boot into a native graphical desktop inside QEMU.
Sub-items 6.1–6.6 and 6.8 provide the display, input, surface, compositor,
window manager, and QEMU acceptance tooling. The missing piece is the custom
O0 desktop UI itself: a dark, minimal 2D aesthetic with neon-green sphere
launchers, purple assistant spheres, green particles, a node network, a
right-side panel, a bottom taskbar, a system tray, and an integrated O0
assistant interface.

Ozero defines its own native execution architecture and does not default to
Linux conventions. No ELF, ELF64, X11, Wayland, or any external GUI toolkit
assumption appears in this code. The desktop UI is Ozero's own design,
rendered entirely in software onto the backbuffer and scanout through the
existing framebuffer path.

## 2. Alternatives

| Alternative | How it works | Trade-off |
|---|---|---|
| **Custom O0 desktop UI (implemented)** | Software-rendered spheres, particles, lines, panels drawn onto the backbuffer each frame; existing compositor blits to the framebuffer. Spheres are solid color (no pictograms). Neon green for launchers, purple for AI/special. | Full control over aesthetic; no external dependencies; runs in the existing desktop kernel thread. Limited to software rendering performance (acceptable for 800x600@60fps emulator target). |
| **Reuse an existing GUI toolkit** | Port a toolkit (e.g. a minimal widget set) and style it to match. | Introduces external code, licensing, and complexity; violates the Ozero-native principle of defining its own UI architecture. |
| **Text-only / serial desktop** | No graphical desktop; all interaction via serial. | Does not satisfy Stage 6 acceptance: visible framebuffer output, sphere interactions, QEMU screendump check. |

## 3. Decision

### 3.1 Visual design

- **Background:** solid black (`0x000000`) across the full backbuffer.
- **Spheres:** solid filled circles, no pictograms or glyphs inside.
  - Neon green (`0x00FF00`) for application launchers.
  - Purple (`0x800080`) reserved for AI and special functions (O0 assistant).
- **Particles:** small green dots (`0x004000`–`0x00FF00`), subtle motion, fade over lifetime.
- **Node network:** thin lines between sphere centers; illuminate (brighter green) when a sphere is hovered or active.
- **Right-side panel:** a vertical panel on the right edge showing status information.
- **Bottom taskbar:** a horizontal strip at the bottom with spherical launchers.
- **System tray:** bottom-right corner with clock and status indicators.
- **O0 assistant interface:** a purple sphere launcher plus an assistant panel that opens on activation.

### 3.2 Sphere interaction state machine

Each sphere cycles through states driven by pointer proximity:

| State | Visual | Trigger |
|---|---|---|
| Idle | Solid sphere at base radius | Default |
| Hover | Slightly enlarged, brighter | Pointer within hover radius |
| Active | Expanding neon circular halo | Click / activation |
| Feedback | Connected lines illuminate + tooltip | Activation complete |
| Mouse exit | Smooth return to idle | Pointer leaves hover radius |

Animation is frame-based (16ms frame budget from `desktop_main`), using integer interpolation of radius and brightness. No floating point.

### 3.3 Rendering layer

All drawing uses existing `o0_surface_*` primitives on the backbuffer:
- `o0_surface_fill` for the black background and panel/taskbar blocks.
- New `o0_surface_fill_circle` for solid spheres.
- New `o0_surface_draw_circle` for halos and outlines.
- `o0_surface_fill_rect` for panel, taskbar, tray blocks.
- A new alpha-blend helper for translucent halos and particles (software alpha over RGBX8888).

Draw order per frame:
1. Background (black fill)
2. Particles (faint green dots)
3. Node network (lines between spheres)
4. Spheres (solid fill + halo if active)
5. Taskbar strip + spherical launchers
6. Right-side panel
7. System tray (clock + status)
8. Assistant panel (if open)

### 3.4 Input integration

Pointer position comes from `o0_shell_pointer_x/y` (already updated by the input router from PS/2 mouse events). Hover testing uses circle hit-test: `(px - cx)^2 + (py - cy)^2 <= (radius + hover_margin)^2`.

### 3.5 Performance budget

The desktop thread runs at ~60fps (16ms sleep). All drawing is software; sphere count is bounded (16 max), particles bounded (64 max). No dynamic allocation during the render loop — all structures are static.

## 4. Consequences

- The desktop UI adds no new kernel dependencies beyond `surface.h`, `shell.h`, `compositor.h`.
- Framebuffer pixel markers (existing 8-bar checksum) remain valid; the UI draws over the boot pattern after boot completes.
- QEMU screendump check can verify dark background and neon-green sphere pixels.
- No GPU acceleration is required; software fallback is the only path (consistent with Stage 6.6).
- Sphere positions and counts are fixed at init; dynamic launcher registration is deferred.

## 5. Test plan

Host (`make test`, `make test-sanitize`):

- `tests/desktop_ui_test.c`: sphere state transitions, hover hit-test, animation radius interpolation, color constants (neon green vs purple), particle update/fade, circle drawing correctness.
- Existing `tests/surface_test.c`, `tests/compositor_test.c`, `tests/shell_test.c`, `tests/window_test.c` continue to pass.

QEMU/CI:

- `tools/qemu-smoke.sh`: add `OZERO: stage6 desktop active` marker (already logged).
- `tools/framebuffer-check.py`: extend to verify dark background and at least one neon-green sphere pixel at a known location.

## 6. Open questions

- Dynamic launcher registration (adding spheres at runtime).
- Tooltip text rendering (requires a font/glyph renderer; deferred).
- Clock source for the system tray (deferred; static placeholder).
- Assistant panel interactivity beyond sphere activation.