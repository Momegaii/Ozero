# Ozero Upgrade Plan — vs Windows: Resources, UI, Kernel

**Purpose:** Recommendations for making Ozero use fewer resources than Windows, render an advanced native UI, and advance the kernel — before proceeding past Stage 6.

**Constraint (architecture baseline):** Ozero is its own operating system. Windows is a reference for metrics to beat (resource footprint, boot time, UI responsiveness) and for the compatibility subsystem (Stage 7–10). Windows conventions are NOT foundational to the native kernel, ABI, or UI. Do not import ELF-as-native, Win32-as-native-ABI, or Windows driver semantics into the native path.

---

## 1. Resource usage: Ozero vs Windows

Target metrics (per boot, idle desktop, no apps):

| Resource | Windows 10/11 (typical) | Ozero target (native) | Rationale |
|----------|-------------------------|-----------------------|-----------|
| RAM idle | 2–4 GB | < 64 MB | No telemetry, no WinSxS, no legacy subsystems, no page-file dependency |
| Boot to desktop | 15–60 s | < 3 s | Minimal init, no service dependency graph, direct framebuffer draw |
| Disk image | 20–40 GB | < 50 MB | No component store, no recovery partitions, no driver cache |
| CPU at idle | 2–8 % | < 0.5 % | Tickless or low-frequency timer, no background indexing/telemetry |
| Power (idle) | 6–12 W | < 2 W | Minimal devices polled, aggressive CPU idle states |

### How Ozero beats Windows on resources

1. **No compatibility tax.** Windows carries 30+ years of APIs, the WinSxS side-by-side store, the registry, COM, and countless services that run at boot. Ozero ships only what Stage 0–6 implement: PMM, paging, scheduler, block layer, OzeroFS draft, software surfaces, PS/2 input, desktop shell, PE loader (compat layer only).

2. **Monolithic-but-modular kernel, no service boundary crossings.** Windows crosses kernel/user boundaries for every graphics, audio, and printing operation. Ozero keeps the software compositor and surfaces inside the kernel address space until a driver model is approved (ADR pending: driver model and user/kernel boundary).

3. **Fixed or simple virtual memory.** No working-set manager, no page-file, no memory compression, no SuperFetch. PMM + 4-level page tables with W^X. Memory is reclaimed deterministically, not by a background scavenger.

4. **No telemetry or updating services.** No Windows Update, no Defender real-time scan, no telemetry upload. The only background work is the scheduler timer and optional IPC.

5. **Tiny user-mode runtime.** The PE loader (Stage 7) maps a PE32+ image and jumps to entry. There is no CRT, no .NET CLR, no WinSxS activation context. A native Ozero application is a flat image with a syscall interface, not a DLL graph.

### Measurable acceptance

Add to `tools/qemu-smoke.sh` or a new `tools/resource-check.py`:
- `OZERO: boot-to-desktop-ms=N` (serial marker printed when `stage6 desktop active` prints)
- `OZERO: free-pages-after-boot=N` (PMM free count right after desktop init)
- `OZERO: kernel-image-bytes=N` (size of `build/kernel.elf`)

Compare against the same machine running Windows; Ozero must be strictly lower on every row.

---

## 2. Advanced UI suggestions (native Ozero, not Windows copy)

Current Stage 6.7 desktop: black background, neon-green spheres (launcher + assistant), particle trails, node network, bottom taskbar, right panel, tray, visible mouse cursor, focus ring.

### 2.1 Immediate polish (Stage 6.x, software renderer)

- **Drop shadows behind spheres and windows.** Draw a blurred dark halo (offset filled circles) beneath each sphere/window before the body. Gives depth without a GPU.
- **Rounded taskbar and panel corners.** Replace the sharp rect fill with a rounded-rect primitive (filled circle corners + rect body).
- **Translucent panel/panel glass.** Blend the panel color at ~60–80% alpha over the background so particles show through faintly.
- **Sphere icons without pictograms.** Keep solid color (ADR 0020 invariant) but add a thin contrasting ring inside each sphere to distinguish launcher vs assistant at a glance — no glyphs, no text.
- **Active-window border.** When a demo window is focused, draw a bright neon border around it. The shell already has `pointer_target`; extend to a focus concept.
- **Smooth sphere transitions.** Ease-in/ease-out on hover radius and color instead of linear steps (fixed-point sine approximation).
- **Cursor themes.** The cursor is already drawn; add a "busy" spinning variant and an "I-beam" variant over text areas (when text rendering exists).

### 2.2 Mid-term UI (Stage 7–8, needs text/font + compositor)

- **Bitmap font renderer.** A minimal 8×8 or 16×16 fixed bitmap font packed into the kernel image. Enables labels under spheres, clock digits, window titles, menu text. No external font format dependency.
- **Alpha compositing.** Surfaces with an alpha channel; the compositor blends windows over the desktop. Requires extending `o0_surface` to RGBA8888 or a separate alpha plane.
- **Drop shadows on windows.** Per-window blurred shadow surface composited below the window.
- **Virtual desktops / workspaces.** 2–4 workspaces, switchable with a key combo. Each workspace has its own window list and sphere layout; spheres are workspace-scoped.
- **Global search / launcher overlay.** Type to search spheres, windows, and commands; overlay covers the screen, results as spheres.
- **Window tiling option.** Alongside floating windows, a tiling mode that splits the screen between windows (no title-bar dragging needed).
- **Theme engine.** Colors (bg, accent, sphere green/purple) loaded from a small config file at boot, compiled into a theme struct. Dark/light/high-contrast themes.

### 2.3 What NOT to copy from Windows

- No Start menu (a single-button menu is a Windows idiom; Ozero's sphere launcher is the native equivalent).
- No taskbar clock with date string until a font renderer exists (the pulsing dot is fine as a placeholder).
- No window minimize/maximize/close buttons in the title bar unless the compositor supports title bars natively.
- No ribbon UI, no Aero glass, no Fluent design — these are Microsoft IP and design language; Ozero has its own (spheres, neon, particles, node network).

---

## 3. Kernel advancement suggestions

Current kernel: PMM, 4-level paging, slab, preemptive priority scheduler, spinlocks/mutexes, block layer, RAM disk, OzeroFS draft, PS/2 input, software surfaces, ELF64 user loader, PE32+ loader (Stage 7), capability handles/budgets/IPC (Wave 5.0), SMP bring-up (Wave 3), SMEP/SMAP/KASLR.

### 3.1 Short-term (before / during Stage 6 completion)

- **Driver model ADR.** Decide the user/kernel driver boundary before adding storage/graphics/audio drivers. Options: (a) all drivers in kernel, (b) microkernel-style userspace drivers with IPC, (c) hybrid. Record alternatives and trade-offs; get Director approval.
- **Storage drivers.** AHCI and NVMe block drivers on top of the existing block layer. Partition table parsing (MBR/GPT already probed). Move off the RAM disk for real boot.
- **Filesystem format decision.** OzeroFS draft exists (docs/o0fs-format.md) but is a proposal. Approve or replace with a format decision ADR. Journaling or copy-on-write for crash consistency.
- **Text output to framebuffer.** Bitmap font + `o0_console_write_to_fb` so boot messages and faults appear on screen, not just serial (required for physical-boot-gate item 1).
- **Keyboard handling in desktop.** PS/2 scan codes → key events → focused window/sphere. Currently `o0_shell_poll_input` exists; wire it to sphere activation and window focus.

### 3.2 Mid-term (Stage 7–8)

- **Native executable format.** Stage 7 uses ELF64 as a container + PE32+ loader for Windows compat. Decide Ozero's native format (not ELF, not PE by default — see architecture baseline: ELF64 kernel is a boot/build detail, not the user format). Options: a simple Ozero-specific container with sections, relocations, and capability manifest.
- **Syscall stabilization.** Freeze the syscall table (ADR 0007 set the first subset). Add: `open`, `read`, `write`, `close`, `mmap` (demand paging), `spawn`, `wait`, IPC send/receive. Error model and return conventions fixed.
- **Demand paging / paging file.** User pages backed by PMM with copy-on-write on fork/spawn. Swap to disk only if a paging file device is approved.
- **Audio stack.** Minimal PCM mixer: software mix multiple streams into one output buffer, delivered to an AC'97/HD-Audio or virtual audio device.
- **Network stack.** Ethernet driver + TCP/IPv4 (or IPv6) socket layer. Needed before any networked apps.

### 3.3 Long-term (Stage 9–10)

- **SMP user threads.** User threads currently BSP-pinned. Per-CPU TSS/IST (ADR 0015) → user threads run on APs, work stealing between runqueues.
- **GPU acceleration backends.** Haswell Gen7.5 software model is host-tested. Real MMIO programming, engine submission, fences, interrupts, engine reset. Then AMD/NVIDIA backends as separate, verified components.
- **Windows compatibility layers.** PE loader is done (Stage 7). Next: Win32 API shims (kernel32, ntdll exist as drafts), CreateProcess, CreateFile, window messages (user32), GDI/DirectDraw subset (gdi32). Each API needs a compatibility matrix and tests — never claim "Windows compatible" without measured results.
- **Driver compatibility.** Kernel-mode Windows drivers is the hardest layer; evaluate feasibility against Ozero's security model (capability handles, W^X, isolation). May remain out of scope.

### 3.4 Performance measurement discipline (architecture baseline §5)

Every performance claim needs reproducible numbers on the same hardware:
- Boot time: serial timestamp from `kmain` start to `stage6 desktop active`.
- Context-switch latency: kernel thread ping-pong, measured via cycle counter (RDTSC/TSC).
- Syscall latency: empty syscall round-trip.
- IPC latency: capability handle send/receive pair.
- Frame time: desktop render loop per-frame µs.

Publish these in `docs/` alongside each release. Do not compare against Windows unless the same hardware and workload are used.

---

## 4. Recommended sequence before leaving Stage 6

1. **Merge current desktop UI improvements** (cursor, centered spheres, glow, trails, focus ring) — done, committed `ef5262b`.
2. **Add framebuffer text output** (bitmap font) so faults and boot messages are visible on screen.
3. **Wire PS/2 input to sphere activation and window focus** — the remote's `f7bb306` already connects PS/2 to desktop interactions; verify it works with the visible cursor.
4. **Write the resource comparison baseline** (boot-to-desktop ms, free pages, image bytes) and add it to `tools/qemu-smoke.sh`.
5. **Driver model ADR** before adding AHCI/NVMe or audio.
6. **Then** proceed to Stage 7 (PE loader already done) → Stage 8 (graphics/compositing) → Stage 9–10.

Save this file: `/workspace/ozero/docs/UPGRADE-PLAN.md`
