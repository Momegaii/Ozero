# A3 graphics and runtime requirements

- Status: draft ready (requirements; no graphics code exists)
- Evidence: WDDM architecture documentation; corpus entries `imgui-d3d11`, `d3d12-miniengine`, `vkd3d-proton-tests`; measured import facts show only one `user32` import so far, so nothing graphics-related has been exercised
- Decisions needed: translation layer vs driver model (Q6); D3D11 before D3D12; Vulkan vs another host API; shader compiler strategy
- Risks: graphics is the largest scope item; shader model coverage, performance and anti-cheat interaction; see [05-risk-report.md](05-risk-report.md)
- Next step: A3 confirms the surface/present interface and the capability-query contract, then A2 turns `imgui-d3d11` into an L3 test plan

## Windows graphics architecture (what has to be replaced)

WDDM is the display driver architecture introduced in Windows Vista and evolved
with every release. The relevant layering is:

```
application
   |  D3D11 / D3D12 / DXGI / OpenGL / Vulkan calls
   v
user-mode runtime DLLs        d3d11.dll, d3d12.dll, dxgi.dll
   v
user-mode driver (UMD)        vendor DLL, per GPU
   v
kernel-mode driver (KMD)      dxgkrnl.sys + vendor display miniport driver
   v
GPU
```

Two consequences:

1. A Windows D3D application expects a UMD and a KMD. A user-mode-only
   compatibility layer cannot supply a KMD, so it must either translate the API
   to a host graphics API, or implement a driver model. These are different
   projects with different risks.
2. `user32`/`gdi32` are not the same as graphics. Window management, composition
   and presentation are separate from 3D rendering, and DWM composition is part
   of the display driver model.

The public prior art splits the same way:

| Approach | Example | What it replaces | What it cannot do |
| --- | --- | --- | --- |
| API translation | DXVK, vkd3d-proton, WineD3D | D3D/DXGI user-mode runtime, mapping to Vulkan/OpenGL | kernel drivers, anti-cheat, vendor-specific extensions |
| Driver model | a WDDM-like driver | UMD/KMD boundary | not feasible in user mode |

## Requirements by layer

### DXGI (first, because everything uses it)

| Requirement | Observable test | Priority |
| --- | --- | --- |
| Factory creation and adapter enumeration | `CreateDXGIFactory1` returns at least one adapter with a name and memory budget | P0 |
| Output enumeration and modes | at least one output with a current mode | P0 |
| Swapchain creation (flip model and bitblt) | swapchain with 2–3 buffers at a supported format | P0 |
| Present with vsync and tearing flags | frames appear; `Present(0, 0)` does not block indefinitely | P0 |
| Resize and fullscreen transitions | resize does not lose the device | P1 |
| Format support queries | answers match the host API's actual support | P0 |
| Frame latency and occlusion status | occlusion reported, not fabricated | P2 |
| Multi-monitor and DPI | window on a second output renders correctly | P2 |

### Direct3D 11

| Requirement | Observable test | Priority |
| --- | --- | --- |
| Device and immediate context creation | `D3D11CreateDeviceAndSwapChain` succeeds at feature level 11_0 | P0 |
| Buffers, textures, views | create and read back a 1×1 texture with a known value | P0 |
| Input layout, vertex/pixel shaders | a triangle renders with the expected colour | P0 |
| Constant buffers and states | uniform colour changes per frame | P0 |
| Render targets and depth | depth test produces the expected occlusion | P1 |
| Queries and timestamps | occlusion query returns plausible counts | P2 |
| Multithreaded command lists | deferred contexts work or are rejected explicitly | P2 |
| Feature levels 10_0/10_1/9_x | either supported or reported as unsupported | P1 |

### Direct3D 12 (later)

| Requirement | Observable test | Priority |
| --- | --- | --- |
| Device, queues, fences | submit and wait on a fence | P2 |
| Descriptor heaps and root signatures | a triangle renders | P2 |
| Pipeline state objects | PSO creation and use | P2 |
| Shader model 6.x | shader compiles and runs | P2 |

D3D12 is deliberately last: it needs descriptor management, explicit
synchronisation and a shader compiler, and it does not make D3D11 easier.

### USER/GDI and composition (shared with Class U)

| Requirement | Observable test | Priority |
| --- | --- | --- |
| Window creation, message loop, painting | message box and a painted window | P0 (M3) |
| GDI drawing primitives | lines, text, bitmaps render | P1 |
| Common controls v6 activation context | correct themed controls | P1 |
| Clipboard and drag/drop | text round trip | P2 |
| DPI awareness | layout at 100% and 150% | P2 |
| DWM composition and layered windows | transparent window renders | P2 |

## What A3 must provide to A2

A2 defines Windows behaviour; A3 owns the device and driver side. The interface
between them should be small and explicit:

| Interface | Required semantics | Why A2 needs it |
| --- | --- | --- |
| Adapter enumeration | list of adapters with name, vendor, memory budget, feature support | DXGI must report something truthful |
| Surface creation and present | create a presentable surface, present with vsync/tearing, resize | swapchain implementation |
| Format support query | texture/render-target/display support per format | D3D format checks |
| Capability query | feature level, shader model, limits (texture size, constant buffer size) | D3D device creation and validation |
| Synchronisation | fence/semaphore objects and waits | D3D11/12 submission |
| Memory accounting | budget and current usage | DXGI budget APIs |
| Device loss | a defined "device removed" condition with a reason code | applications handle it; a hang is not acceptable |
| Shader compilation | compile HLSL to the host's shader format | D3D shader creation |

A2 will translate Windows error codes and validation behaviour; A3 must not
invent Windows-specific semantics.

## Test cases and pass criteria

| Test | Corpus entry | Pass criteria |
| --- | --- | --- |
| Clear a swapchain to a known colour | `imgui-d3d11` (first frame) | screenshot pixel matches the expected value |
| Render a triangle | `imgui-d3d11` | pixel positions and colours match the reference |
| Resize the window | `imgui-d3d11` | no device loss; new size renders |
| Run for 60 seconds | `imgui-d3d11` | no TDR-equivalent, no crash, stable frame count |
| D3D12 triangle | `d3d12-miniengine` (later) | renders and exits cleanly |

A screenshot hash is the evidence. "It looked fine" is not evidence.

## Non-goals for the first graphics milestone

- Kernel display drivers and hardware modesetting.
- Video decode (DXVA/Media Foundation) and hardware encoding.
- HDR, variable rate shading, ray tracing, mesh shaders.
- Multi-GPU and GPU virtualisation.
- Vendor extensions and driver control panels.
- Anti-cheat-protected titles.

## Risks specific to graphics

| Risk | Detail | Mitigation |
| --- | --- | --- |
| Translation gap | D3D behaviour is larger than the API surface: validation, defaults, precision | conformance tests, not screenshots alone |
| Shader compiler | HLSL compilation is a separate compiler project | use an existing compiler if licensing allows; decide early |
| Performance | translation costs CPU and can change timing-sensitive behaviour | measure frame time from the first milestone |
| Format coverage | many formats, sRGB variants, typeless resources | capability query with explicit unsupported answers |
| Device loss semantics | applications depend on `DXGI_ERROR_DEVICE_REMOVED` | define and test it |
| Anti-cheat interaction | overlays and anti-cheat hook D3D/DXGI | out of scope; document |
| Presentation vsync | wrong vsync causes tearing or stalls | test both present flags |

## Evidence

Nothing in this document has been executed. The only measured fact relevant to
graphics is negative: across eight parsed images, `user32.dll` accounts for two
imported symbols and no `d3d11`, `dxgi` or `d3d12` import appears, because no
graphics sample has been built yet. That is recorded so that no graphics claim can
be inferred from the current corpus.

## Sources

- Microsoft, WDDM overview: <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide>
- Microsoft, Direct3D 12 graphics: <https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-graphics>
- Microsoft, Windows Driver Frameworks: <https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/>
- Microsoft, Managed execution process (WPF depends on DirectX): <https://learn.microsoft.com/en-us/dotnet/standard/managed-execution-process>
- vkd3d-proton (prior art for D3D12 translation): <https://github.com/HansKristian-Work/vkd3d-proton>
- DirectX-Graphics-Samples (MIT test material): <https://github.com/microsoft/DirectX-Graphics-Samples>
