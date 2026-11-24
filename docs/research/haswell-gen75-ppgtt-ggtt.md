# Haswell Gen7.5 PPGTT/GGTT Research Dossier

Status: **Research only. No implementation. No hardware validation.**
Last updated: 2026-11-24
Primary source: Intel Open Source Graphics PRM, Volume 5: Memory Views (Haswell), 2013-12-18.

This document is the working reference for the Haswell Gen7.5 backend in Stage 5. It separates
what the primary specification states, what is derived from it, and what remains unknown. Do not
promote a derived or unknown item to a verified fact without new evidence.

## 0. How to use this document

- Section 12 is the authority list: verified, derived, unknown. Read it before writing code.
- Section 1.1 states the verification boundary: every fact here is specification-verified, and
  none is hardware-verified.
- Section 13 maps the hardware contract onto Ozero-owned types and invariants.
- Section 14 lists host tests that do not require hardware.
- Section 15 traces the Stage 5 Phase 3 checklist to this evidence.
- Short PRM quotes are included for traceability. The PRM is CC-BY-ND 3.0 US; keep quotations
  short and attributed. Do not paste whole sections into the repository.

## 1. Sources and verified device data

| Item | Value |
|---|---|
| Document | Intel Open Source Graphics Programmer's Reference Manual for the 2013 Intel Core Processor Family (Haswell), Volume 5: Memory Views |
| Revision date | 2013-12-18 |
| Pages | 164 |
| Official URL | https://cdrdv2-public.intel.com/690966/intel-gfx-prm-osrc-hsw-memory-views.pdf |
| SHA-256 | `e72dbaa1eb2d06c75e5b00d97fa07bae053591245ee969f68ee474d034760f30` |
| Archive mirror | https://github.com/Igalia/intel-osrc-gfx-prm (directory `005_hsw_-_2013`) |
| Mirror integrity | The mirror copy has the same size and SHA-256 as the official download (checked 2026-11-24) |
| Licence | CC-BY-ND 3.0 US, per the archive README |

Supporting volumes used for register and command definitions (same 2013 Haswell release, same
archive directory):

| Volume | File | Used for |
|---|---|---|
| 2b, Command Reference: Instructions | `vol02b_intel-gfx-prm-osrc-hsw-commandreference-instructions_0.pdf` | `PIPE_CONTROL`, `MI_FLUSH_DW`, `MI_FLUSH`, `MI_UPDATE_GTT`, `MI_LOAD_REGISTER_IMM` |
| 2c, Command Reference: Registers | `vol02c_intel-gfx-prm-osrc-hsw-commandreference-registers_0.pdf` | `GFX_MODE`, `PP_DCLV`, per-engine `PP_DCLV`, `ECOCHK`, `RING_BUFFER_CTL` |
| 2d, Command Reference: Structures | `vol02d_intel-gfx-prm-osrc-hsw-commandreference-structures.pdf` | Context/fault structure cross-checks |
| 3, GPU Overview | `vol03_intel-gfx-prm-osrc-hsw-gpu-overview_0.pdf` | Engine inventory, GT configurations, MMIO range map |
| 4, Configurations | `vol04_intel-gfx-prm-osrc-hsw-configurations.pdf` | GT1/GT2/GT3 slice, subslice and EU differences |
| 12, PCIe Config Registers | `vol12_intel-gfx-prm-osrc-hsw-pcie-config-registers.pdf` | BAR cross-checks |

Reference implementation used only to cross-check arithmetic and register offsets: Linux i915
(`drivers/gpu/drm/i915/gt/gen6_ppgtt.c`, `intel_gtt.h`, `intel_engine_regs.h`,
`intel_ring_submission.c`). It is GPL-2.0, is not a specification, and must not be copied into
Ozero. Where i915 disagrees with the PRM, the PRM wins and the disagreement is recorded in
Section 12.

Page numbers below are PDF page numbers of Volume 5 unless stated otherwise.

### 1.1 Verification boundary

Every claim in this document is specification-verified against the PRM text. None is
hardware-verified. No Ozero code has run on a Haswell GPU, no register has been read back from
silicon, and no GGTT or PPGTT has been programmed on real hardware. Statements about hardware
behaviour are therefore statements about what the PRM specifies, not observations.

These items specifically require hardware confirmation before they can be called verified:
the `PP_DIR_BASE` offset and the `PGTBL_CTL`/`PGTBL_CTL2` layouts (Section 12.3 items 1-2), the
0x4094/0x40A0 fault registers (item 3), the real GGTT aperture and GSM base (item 6), the actual
PCI device ID and revision (item 6), and cache-maintenance behaviour for CPU page-table writes
(item 8).

### 1.2 Device and register map (verified)

Vol 3 p.18 documents these graphics MMIO ranges for Haswell, described there as a subset of the
complete MMIO space:

| Range | Owner |
|---|---|
| 0x00002000-0x00002FFF | Render/Generic Media Engine |
| 0x00004000-0x00004FFF | Render/Generic Media Graphics Memory Arbiter |
| 0x00012000-0x000123FF | MFX Control Engine (Video Command Streamer) |
| 0x00012400-0x00012FFF | Media Units (VIN unit) |
| 0x00014000-0x00014FFF | MFX Memory Arbiter |
| 0x00022000-0x00022FFF | Blitter Engine |
| 0x00024000-0x00024FFF | Blitter Memory Arbiter |
| 0x00100000-0x00107FFF | Fence Registers |
| 0x00140000-0x0017FFFF | MCHBAR (SA) |

Vol 3 p.18 also states that 0x8800-0x88FF is reserved for the GT and that IA accesses to that
range have no effect. This is consistent with the `MI_LOAD_REGISTER_IMM` restriction in
Volume 2b.

GT configurations (Vol 4 p.8, with EU counts also in Vol 3 p.11):

| Config | Slices | Subslices | EUs | Threads | L3 (URB + cache) |
|---|---|---|---|---|---|
| GT3 | 2 | 4 | 40 | 280 | 1024 KiB |
| GT2 | 1 | 2 | 20 | 140 | 512 KiB |
| GT1.5 | 1 | 2 | 12 | 84 | 512 KiB |
| GT1 | 1 | 1 | 10 | 70 | 256 KiB |

Each EU supports 7 threads (Vol 3 p.11). The PRM does not list PCI device IDs, so the HD
Graphics 4600 device ID and revision must be read from PCI configuration space on the target
machine; no PRM table can replace that. Vol 3 p.15 describes the fixed-function Video Codec
Engine (VCE/MFX) and its internal units.

Consequence for the backend: the documented engine ranges confirm that the reference
implementation's `PP_DIR_BASE` offsets fall inside real engine windows (0x2228 inside
0x2000-0x2FFF, 0x12228 inside 0x12000-0x123FF, 0x22228 inside 0x22000-0x22FFF). The VECS offset
0x1A228 lies outside the ranges published in this volume and stays unverified.

## 2. Terminology and address spaces

Three kinds of address must not be conflated:

1. **GGTT (global graphics translation table).** Single-level table. Maps graphics virtual
   addresses to physical memory for global clients, the aperture, display and cursor.
2. **PPGTT (per-process graphics translation table).** Two-level table. Maps per-process
   graphics virtual addresses for rendering and blitter work when enabled.
3. **CPU physical / CPU virtual.** Unrelated to either table. GPU virtual addresses are a
   separate namespace in Ozero (Section 13).

Verified statements (Vol 5, "Graphics Translation Tables", p.33):

- GTT and PPGTT are memory-resident page tables containing 32-bit page translation entries.
- They map logical graphics addresses to physical memory, and sometimes to snooped system
  memory "PCI" addresses.
- The tables must reside in unsnooped system memory.
- Table base addresses are programmed through `PGTBL_CTL` (GGTT) and `PGTBL_CTL2` (PPGTT) and
  must be 4 KiB aligned.
- The GGTT is programmed only through the `GTTADR` aperture alias. The PPGTT is programmed
  directly in memory.
- The GGTT size can be 128 KiB, 256 KiB or 512 KiB, mapping 128 MiB, 256 MiB or 512 MiB of
  aperture respectively, and is physically contiguous.
- PPGTT size is controlled by `PGTBL_CTL2` and may additionally be 64 KiB, mapping a 64 MiB
  aperture.

Verified selection rule (Vol 5, "Global Virtual Memory", p.35 and "PPGTT vs GGTT Limitation",
p.46):

- Global virtual memory is the default target when no PPGTT is enabled.
- Display and the aperture path always use the GGTT, even when the GT is mapped through a PPGTT.
- High-priority clients such as display and cursor always access global virtual memory.

Consequence for Ozero: a framebuffer or scanout surface cannot be validated only against the
PPGTT. Scanout paths need GGTT mappings regardless of the render address space.

## 3. GGTT: structure and entry format

Verified (Vol 5, "Global GTT", p.46):

- The GGTT walk is always single level.
- The PTE format is identical to the PPGTT PTE format.
- Software programs the GSM (GTT Stolen Memory) with page pointers; hardware indexes the GSM by
  the virtual address and reads the physical page pointer from it.
- For pre-Gen8 products the GSM is limited to 2 MiB, which indexes 2 GiB of physical memory.

Verified (Vol 5, "GTT Page Table Entries (PTEs)", p.35-36):

- One 32-bit PTE per 4 KiB graphics page.
- Two formats exist. The older format carries a 4-bit GFDT/cacheability field in bits 3:1.
  The new Haswell format moves cacheability to bits 3:1 plus bit 11.
- New-format bit layout:

| Bits | Field |
|---|---|
| 31:12 | Physical page address 31:12 |
| 11 | Cacheability control [3] |
| 10:4 | Physical page address 38:32 (must be zero for 32-bit addresses) |
| 3:1 | Cacheability control [2:0] |
| 0 | Valid (1 = valid, 0 = invalid) |

- An access through an invalid PTE, other than a CPU read, produces a Page Table Error.
- Programming recommendation (p.35): map all graphics memory pages in the GGTT to some physical
  page, if only a dummy page.

## 4. PPGTT: two-level structure

Verified (Vol 5, "Per Process GTT", p.38):

- Gen6 through Gen7.5 use a two-level mapping scheme: a second-level page directory containing
  page-table base addresses, and first-level page tables containing page addresses.
- The purpose is to let the collection of page tables live in discontinuous memory.
- The directory and each page table fit within a single 4 KiB page that can be located anywhere
  in physical memory.
- Both levels use 32-bit entries, described as allowing 1024 entries per page, each entry
  mapping 2^10 pages.

Verified (Vol 5, "Page Walk", p.39):

1. Start from the page-directory base.
2. Use the page-directory index to fetch the PDE.
3. Use the PDE and the page-table index to fetch the PTE.
4. The PTE points to the physical page; the page offset selects the byte.

Verified (Vol 5, "PPGTT Directory Entries (PDEs)", p.42):

- One 32-bit PDE per 4 KiB page table, and each page table covers 4 MiB of graphics memory.
- Page directories must be located entirely within the GGTT.
- Directory entries should be updated only for non-active contexts. If a directory entry is
  updated for a running context, the time at which the update takes effect is unknown because
  the device caches directory entries.
- Directory entries can only be modified using `GTTADDR`.

Note the tension between p.38, which says the directory may live anywhere in physical memory, and
p.42, which requires the directory itself to be inside the GGTT. Section 12.3 records this as
unresolved; do not pick one silently.

PDE bit layout (p.42):

| Bits | Field |
|---|---|
| 31:12 | Physical page address 31:12 of the page table |
| 11:4 | Physical page address 39:32 of the page table |
| 3:2 | Reserved, must be zero |
| 1 | Page size: 0 = 4 KiB, 1 = 32 KiB |
| 0 | Valid; an access through an invalid PDE raises a page fault |

Verified (Vol 5, "PPGTT Table Entries (PTEs)", p.43):

- One 32-bit PTE per 4 KiB graphics page.
- Page tables must be located in main memory and must not be snooped.
- Page tables may be updated directly in memory if proper precautions are taken, or from the
  command stream using `MI_UPDATE_GTT`.
- Haswell re-purposes physical address bit 39 as part of the cacheability controls, because
  client systems are only required to support 39-bit physical addressing. The former GFDT bits
  are also re-assigned to cacheability control.

PTE bit layout (p.43):

| Bits | Field |
|---|---|
| 31:12 | Physical page address 31:12 |
| 11 | Cacheability control [3] |
| 10:4 | Physical page address 38:32 |
| 3:1 | Cacheability control [2:0] |
| 0 | Valid; an invalid PTE produces a Page Table Error on non-CPU access |

Note the asymmetry: the PDE carries page-table physical bits 39:32 in bits 11:4, while the PTE
carries page physical bits 38:32 in bits 10:4 because PTE bit 11 is used for cacheability.

## 5. 32 KiB big pages

Verified (Vol 5, "Two-Level Per-Process Virtual Memory", p.40-41):

- The starting address of a 32 KiB page must be natively 32 KiB aligned in memory.
- Hardware uses bits 15 and up to check TLB lookups.
- On Haswell the 32 KiB big-page implementation is expanded to all surfaces. Eight consecutive
  PTEs within the 32 KiB page must be programmed to point at their respective 4 KiB pages.
- If a PDE is marked as 32 KiB:
  1. the entire page table pointed to by that PDE must use 32 KiB pages;
  2. PTEs for each 4 KiB must still be programmed, with no difference from a 4 KiB PDE;
  3. the physical addresses of each 32 KiB page must be natively 32 KiB aligned.
- `GFX_MODE` bit 9 notes that enabling the PPGTT also enables support for big pages (32 KiB)
  (Vol 2c, p.92).

Implementation consequence: 32 KiB pages are a property of the whole page table, not a single
entry, and they impose physical alignment on the backing pages. Ozero should treat 32 KiB pages
as an explicitly unsupported capability in the first backend revision rather than silently
setting PDE bit 1 (Section 13).

## 6. Cacheability encoding

Verified (Vol 5, p.36 and p.43-45): cacheability is a 4-bit field split across PTE bits 3:1
(low three bits) and bit 11 (high bit). The PRM supplies this table:

| Bits [3:0] | Meaning |
|---|---|
| 0000 | UC (LLC/eLLC), age don't care |
| 0010 | WB in LLC only, aged 3 |
| 0011 | WB in LLC only, aged 0 |
| 0100 | WT in eLLC (UC in LLC), aged 0 |
| 0101 | WT in eLLC (UC in LLC), aged 3 |
| 0110 | WT in LLC/eLLC, aged 0 |
| 0111 | WT in LLC/eLLC, aged 3 |
| 1000 | WB in LLC/eLLC, aged 3 |
| 1001 | WB in LLC/eLLC, aged 2 |
| 1010 | WB in LLC/eLLC, aged 1 |
| 1011 | WB in LLC/eLLC, aged 0 |
| 1100 | WB in eLLC (UC in LLC), aged 3 |
| 1101 | WB in eLLC (UC in LLC), aged 2 |
| 1110 | WB in eLLC (UC in LLC), aged 1 |
| 1111 | WB in eLLC (UC in LLC), aged 0 |

Encoding 0001 is not listed in the PRM table and must be treated as undefined. Ozero must never
emit it.

Verified guidance (p.37, p.45):

- Surface-state control bits can override PTE cacheability for non-L3 clients; for L3 clients
  the driver is strongly advised to use the PTE and leave the override bit clear.
- GFDT is passed through surface state and is not controllable through the PTE; Haswell GFDT
  support is not guaranteed.
- The PRM recommends page-table controls as the primary memory-type mechanism, with
  `memory_object_control_state` used as an override when needed.
- eDRAM should be used only when present.

Ozero decision required: define an Ozero memory-type enum (UC, WB, WT, plus LLC/eDRAM target and
age) and map it to this table in the backend. Do not expose Intel bit patterns in the generic
core. The default for ordinary GPU-visible buffers is an explicit policy decision, not a PRM
fact; record it in an ADR before implementing.

## 7. Enabling and programming the PPGTT

Verified (Vol 2c, "Graphics Mode Register", `GFX_MODE` 0x229C, p.91-92):

- Bit 9, Per-Process GTT Enable: 0 = PPGTT disabled, 1 = PPGTT enabled.
- When enabling, "the PD Offset and PD Cacheline Valid registers must be set in all pipes
  (blitter, MFX, render) before any workload is submitted to hardware."
- The mode also enables support for 32 KiB big pages.
- Bit 13, Flush TLB invalidation Mode, is described in Section 8.

Verified (Vol 2c, `PP_DCLV`, 0x2220, 64-bit, p.88):

- The register controls which groups of 16 directory entries are loaded into the on-chip PPGTT
  directory cache during a context restore. A set bit enables the corresponding group.
- It is restored with the context, before the directory cache itself.
- It can limit the size of a process virtual address space: an access that needs a PD entry in a
  group that is not enabled causes a fatal error, and no PD fetch is attempted.
- The context image of the register is maintained by software; software should not normally need
  to read it.
- Programming note: the Page Directory Base register is a Global Context Register (power
  context) and is not maintained per context in ring-buffer submission mode. Software must
  explicitly load `PP_DCLV` followed by `PP_DIR_BASE` through Load Register Immediate commands
  in the ring buffer before submitting a context, and must do so after ensuring the pipe is
  completely flushed with TLBs invalidated.

Verified per-engine `PP_DCLV` addresses (Vol 2c):

| Engine | Register | Address | Page |
|---|---|---|---|
| Render | `PP_DCLV` | 0x2220 | 88 |
| Video (VCS) | `VCS_PP_DCLV` | 0x12220 | 323 |
| Video enhancement (VECS) | `VECS_PP_DCLV` | 0x1A220 | 412 |
| Blitter (BCS) | `BCS_PP_DCLV` | 0x22220 | 446 |

Verified ring-buffer initialization requirement (Vol 2c, `RING_BUFFER_CTL`, p.33):

- `PP_DCLV` followed by `PP_DIR_BASE` must be programmed as part of the initialization workload
  if the PPGTT is enabled in `GFX_MODE`.
- All CPU MMIO initialization must happen in this block or later so that the state is
  saved/restored across context switches.

Register gap: Volume 2c in this release documents `PP_DCLV` but does not contain a
`PP_DIR_BASE` register section with an MMIO offset. The PRM text refers to the register and to
loading it with LRI, but the offset must be confirmed before use (Section 12).

## 8. Translation invalidation and ordering

Verified (Vol 2c, `GFX_MODE` bit 13, p.91):

- When set, TLB invalidation is limited to batch-buffer boundaries, `PIPE_CONTROL` commands with
  the TLB-invalidate bit set, and sync flushes.
- When clear, TLB caches are flushed on every full pipeline flush.

Verified (Vol 2b, `PIPE_CONTROL`, p.962-963):

- DW1 bit 18, TLB Invalidate, invalidates all Render Engine TLBs once the flush completes.
- It requires the stall bit (DW1 bit 20).
- If the flush TLB invalidation mode is clear, a TLB invalidate happens irrespective of this bit.

Verified (Vol 2b, `MI_FLUSH_DW`, p.267-268 and the VCS/VECS variants):

- DW0 bit 18, TLB Invalidate, invalidates all TLBs belonging to the executing engine.
- The bit is valid only when the Post-Sync Operation field is 1h or 3h.
- If `GFX_MODE` (0x229C) bit 13 is set, the command causes a config write to MMIO address
  0x4F100.

Verified (Vol 2b, `MI_FLUSH`, render engine):

- `MI_FLUSH` is "no longer validated or supported" on Haswell and must be used at the caller's
  risk. New code should use `PIPE_CONTROL` on the render engine and `MI_FLUSH_DW` on the other
  engines.

Verified (Vol 5, p.42): directory-entry changes are only defined for non-active contexts, because
the device caches directory entries. There is no PRM statement of a bounded time for a live PDE
update to take effect.

Implementation consequence: a PDE edit is only safe when no engine can be using that address
space. A PTE edit inside a live page table requires a TLB invalidation command before the engine
consumes the new mapping. Neither command can be emitted before Stage 5 Phase 4 provides ring
buffers.

## 9. Page faulting

Verified (Vol 5, "Faulting", p.47-48):

- Page faulting via fault halt is disabled by default and is enabled by a global bit at
  0x4090[14]. Volume 2c confirms 0x4090 as `ECOCHK`, where bit 14 is "Global Page Fault Enable"
  (p.221).
- A counter of faulted responses from system memory is visible through MMIO; it increments on
  faults and decrements as faults are serviced.
- On a halt, software writes the repaired physical address, in PTE format, into the Engine Fault
  register (0x4094) and clears bit 0 to signal that the fault was repaired.
- The fault count is reported in the Main GFX Arbiter Error register, 0x40A0 bits 15:10.
- Gen7.5 cannot handle command buffers that take page faults.
- Gen7.5 page faulting applies only to the Render Engine.
- Gen7.5 page faulting interacts badly with power-management transitions; documented cases with
  Geyserville transitions can lock up hardware until a system reset.
- Gen7.5 cannot handle faults on the GGTT.

Implementation consequence: do not enable fault repair in the first backend revision. Treat GPU
page faults as fatal, record diagnostics, and never rely on fault-and-repair for correctness.
The PRM's 0x4094 and 0x40A0 addresses are not present in the extracted Volume 2c register text
and remain unverified as register definitions (Section 12).

## 10. Privileged command-stream helpers

Verified (Vol 2b, `MI_UPDATE_GTT`, p.256):

- Opcode 0x23. It updates GTT page-table entries in a coherent manner at a predictable point in
  the command stream.
- An `MI_FLUSH` should precede it so that earlier in-flight work no longer references the entries
  being changed; the flush also invalidates TLBs and read caches. Note that the same volume says
  `MI_FLUSH` is no longer validated on Haswell; if a flush equivalent is used instead, record the
  deviation and confirm the ordering is still sufficient.
- It is a privileged command.
- The "Use Global GTT" field must be 1h; updating per-process graphics addresses is not
  supported.
- With execlists enabled it becomes a no-op and raises an error if executed from a non-secure
  (PPGTT) batch buffer. With execlists disabled it may be executed from a PPGTT batch buffer.

Verified (Vol 2b, `MI_LOAD_REGISTER_IMM`, p.248-250):

- Opcode 0x22. Writes up to one dword from the command stream to a register offset carried in
  bits 22:2, before the next command executes.
- Bits 11:8 are byte write disables; 1111b suppresses the write.
- Addresses 0x8800-0x88FF and any address at or above 0x40000 must not be used for LRI.
- The command is converted to a no-op if register access is disabled by `RINGBUF` DW3 bit 8, or
  if executed from a non-secure batch buffer.

These commands matter because the PPGTT base and DCLV registers are loaded through LRI, and page
table updates can be requested through `MI_UPDATE_GTT`. Both belong to the backend's command
encoder, not to the generic core.

## 11. Register and command summary

| Name | Address / opcode | Width | Purpose | Evidence |
|---|---|---|---|---|
| `GFX_MODE` | 0x229C | 32 | Bit 9 PPGTT enable, bit 13 flush TLB invalidation mode | Vol 2c p.91-92 |
| `PP_DCLV` | 0x2220 | 64 | Render directory-cache valid groups | Vol 2c p.88 |
| `VCS_PP_DCLV` | 0x12220 | 64 | Video directory-cache valid groups | Vol 2c p.323 |
| `VECS_PP_DCLV` | 0x1A220 | 64 | Video-enhance directory-cache valid groups | Vol 2c p.412 |
| `BCS_PP_DCLV` | 0x22220 | 64 | Blitter directory-cache valid groups | Vol 2c p.446 |
| `PP_DIR_BASE` | Not documented in Vol 2c | 32 (inferred) | Page-directory base, loaded by LRI; engine offsets fall inside Vol 3 p.18 ranges for render/VCS/BCS | Vol 2c p.88, Vol 3 p.18 |
| `ECOCHK` | 0x4090 | 32 | Bit 14 global page-fault enable | Vol 2c p.221 |
| `PIPE_CONTROL` | GFXPIPE type 3h, 3D opcode 2h | variable | DW1 bit 18 TLB invalidate, bit 20 stall | Vol 2b p.962-963 |
| `MI_FLUSH_DW` | opcode 0x26 | variable | DW0 bit 18 TLB invalidate, post-sync 1h/3h | Vol 2b p.267-268 |
| `MI_UPDATE_GTT` | opcode 0x23 | variable | Privileged GTT entry update | Vol 2b p.256 |
| `MI_LOAD_REGISTER_IMM` | opcode 0x22 | variable | MMIO write from the ring | Vol 2b p.248-250 |
| `GTTADR` | PCI BAR alias | - | GGTT programming window | Vol 5 p.35 |

Command opcodes above are read from the volume's own opcode fields (`MI_FLUSH_DW` 26h,
`MI_UPDATE_GTT` 23h, `MI_LOAD_REGISTER_IMM` 22h) and `PIPE_CONTROL` is a GFXPIPE packet with 3D
opcode 2h. Re-check them against Volume 2b when encoding.

## 12. Verified, derived, unknown

### 12.1 Verified against the primary specification

1. GGTT is single-level; PPGTT is two-level for Gen6-Gen7.5.
2. PTE is 32 bits per 4 KiB page; PDE is 32 bits per 4 MiB of graphics memory.
3. Exact PTE and PDE bit layouts, including the split 4-bit cacheability field.
4. Page directories must be located entirely within the GGTT; page tables must reside in
   unsnooped main memory (see 12.3 item 7 for the GSM question).
5. Table base addresses are 4 KiB aligned.
6. PPGTT directory entries are only defined to change for non-active contexts.
7. `GFX_MODE` bit 9 enables the PPGTT and requires PD base and DCLV registers in all pipes
   before workload submission.
8. `PP_DCLV` selects 16-entry directory-cache groups, can cap a process address space, and
   raises a fatal error for disabled groups.
9. `PP_DIR_BASE` is a global power-context register and must be reloaded per context via LRI
   after a flush with TLBs invalidated.
10. `PIPE_CONTROL` bit 18 and `MI_FLUSH_DW` bit 18 invalidate engine TLBs, with the stated
    conditions.
11. Legacy `MI_FLUSH` is not supported on Haswell.
12. Haswell page faulting is render-only, cannot handle command-buffer or GGTT faults, and can
    interact with power management to lock up hardware.
13. Display and aperture paths always use the GGTT.

### 12.2 Derived (arithmetic or consistency conclusions, not direct quotes)

1. **Aperture size rule.** The PRM pairs 64/128/256/512 KiB tables with 64/128/256/512 MiB
   apertures. That is a factor of 1024, which follows from 4-byte entries covering 4 KiB pages.
   So `aperture_bytes = table_bytes * 1024`.
2. **PPGTT virtual-address ceiling is 2 GiB.** `PP_DCLV` has 32 bits and each bit covers 16
   directory entries, so it covers 512 PDEs. At 4 MiB per PDE that is 2 GiB. i915's constant
   `PP_DIR_DCLV_2G = 0xffffffff` and its 512-entry page directory agree with this reading.
3. **PD entry count conflict.** Volume 5 p.38 says both levels allow 1024 entries per page, which
   would give 4 GiB for the directory. `PP_DCLV` coverage and i915 both give 512 entries and
   2 GiB. Treat 2 GiB as the enforceable ceiling and record the conflict as open.
4. **Configured aperture versus architectural ceiling.** The `PGTBL_CTL2` sizes of 64-512 MiB
   are below the 2 GiB DCLV ceiling. A 512 MiB aperture needs 128 PDEs, i.e. eight DCLV bits.
   The PRM does not state how the two limits interact; verify on hardware.
5. **PD alignment.** The PRM requires 4 KiB base alignment. i915 uses 64 KiB alignment for the
   page directory (`GEN6_PD_ALIGN = PAGE_SIZE * 16`). 64 KiB satisfies the PRM but is a
   reference-implementation choice, not a specification requirement.
6. **PTE physical width.** A PTE can express physical bits 38:0 (bit 39 is cacheability), so
   mapped pages must be below 512 GiB. A PDE can express page-table bits 39:0, so page tables
   must be below 1 TiB.
7. **Directory cache is a cache of PDEs.** Since PDE changes have undefined effect timing for a
   running context, any design that reuses one address space across contexts must reload DCLV and
   the base register, or quiesce the engines first.

### 12.3 Unknown or unverified

1. `PP_DIR_BASE` MMIO offset is not documented in the extracted Volume 2c text. i915 uses
   engine MMIO base + 0x228 (render 0x2228, VCS 0x12228, BCS 0x22228, VECS 0x1A228). The render,
   VCS and BCS offsets fall inside the engine ranges published in Vol 3 p.18 (Section 1.2), which
   makes them plausible but still not documented register definitions; the VECS offset lies
   outside those ranges. Confirm all four against the full PRM or hardware before use. Do not
   commit them as facts.
2. `PGTBL_CTL` and `PGTBL_CTL2` bit layouts are not present in the extracted Volume 2c register
   text. Their existence, base-address field and size encoding are only described in prose in
   Volume 5 p.33.
3. The Engine Fault register (0x4094) and the Main GFX Arbiter Error register (0x40A0) are named
   in Volume 5 p.48 but were not found as register definitions in the extracted Volume 2c text.
4. The "Force PD Restore" bit in the context descriptor is referenced by the `PP_DCLV`
   description but its definition was not located in Volumes 2c or 2d.
5. Exact completion and wait semantics for TLB invalidation are not specified beyond the command
   bits. There is no documented status bit to poll for "invalidation complete".
6. The actual GGTT aperture size, GSM base and stolen-memory layout on an HD Graphics 4600
   machine must be read from the device at runtime. The PRM says the size is programmed through
   `PGTBL_CTL`, but does not state which agent (firmware or OS) owns it on a given platform.
7. Whether "page directory must be within the GGTT" implies the stolen-memory GSM specifically,
   or any memory mapped through the GGTT, is not stated. i915 places it in GSM.
8. Whether CPU writes to page tables require cache maintenance before the GPU reads them is not
   stated in Volume 5. The PRM only says "proper precautions".
9. The 1024-versus-512 PDE entry-count conflict in 12.2 item 3.
10. 32 KiB page support in the target SKU and the allocator changes it requires.

## 13. Ozero implementation mapping

The generic core must stay hardware-independent. Haswell encoding belongs to the backend.

### 13.1 Types and ownership

- `o0_gpu_va_t`: 64-bit GPU virtual address in a named GPU address space. Distinct from
  `o0_vaddr_t` and `o0_paddr_t`. No arithmetic with CPU addresses.
- `o0_gpu_vm`: generic address space: base, size, allocation policy, capability record supplied
  by the backend (VA width, page sizes, alignment).
- `o0_gpu_memory`: physical-page backing, reference count, pin count, residency state. The
  existing header shape is close; it must be defined once, in the public header, and never
  redefined in a `.c` file.
- `o0_gpu_mapping`: binds one memory object into one VM at one VA for one size.
- `o0_hsw_ppgtt`: backend state: GGTT offset of the page directory, page-directory storage,
  page-table list, DCLV mask, and the configured VA limit.

### 13.2 Backend invariants

1. Encode PDEs and PTEs only through named helpers with explicit bitfields. Never shift raw
   constants at call sites.
2. Reserved PDE bits 3:2 must be written as zero and validated on read.
3. The valid bit is written last when publishing a new entry.
4. PTE physical addresses must be 4 KiB aligned and below 512 GiB; PDE page-table addresses must
   be 4 KiB aligned and below 1 TiB.
5. A page directory must be allocated from GGTT-mapped memory and must not be released while any
   mapping in the address space exists.
6. Page tables must come from ordinary kernel physical memory and must not be individually freed
   while any PDE references them.
7. GPU VA must stay inside the configured aperture and below the 2 GiB DCLV ceiling; reject
   overflow and alignment errors before touching entries.
8. PDE updates are permitted only when the address space is known inactive. Encode this as an
   explicit state on `o0_gpu_vm`, not as a comment.
9. PTE updates to a live address space require an invalidation command and a completion wait.
   Until Phase 4 exists, the backend must refuse to activate such an address space rather than
   pretend the update is visible.
10. 32 KiB pages, page-fault repair and execlist submission are unsupported and must return a
    clear unsupported-status error, not a silent fallback.

### 13.3 Deferred to later phases

- Enabling `GFX_MODE` bit 9 and loading DCLV/base via LRI require ring buffers, which are
  Phase 4 work. Phase 3 can build, encode and test tables without touching hardware registers.
- TLB invalidation command emission and completion waits likewise belong with submission.
- Residency and eviction policy belong to Phase 2/5 and must not be inferred from the PRM.

## 14. Host-test plan for Phase 3

These tests need no hardware and no emulator. They validate the encoding contract.

1. PTE encode/decode round-trip for physical addresses at 0, 4 KiB, 4 GiB (the 38:32 field
   boundary) and the maximum representable page base (2^39 - 4 KiB). Assert bit 11 carries
   cacheability, not address.
2. PDE encode/decode round-trip across the 39:32 boundary; assert reserved bits stay zero and
   that PDE bit 1 is rejected until 32 KiB support exists.
3. Reserved-bit and alignment rejection: unaligned physical page, unaligned page-table base,
   address above the PTE or PDE limit, VA outside the aperture, VA at or above 2 GiB, and
   arithmetic overflow in base + size.
4. Two-level software walk model: construct a directory and tables, resolve a set of VAs, and
   verify the walk matches the PRM sequence, including invalid PDE and invalid PTE outcomes.
5. Non-contiguous page-table backing: allocate tables from scattered pages and verify the walk
   and teardown.
6. DCLV mask derivation: assert the mask covers exactly the groups needed for 64, 128, 256, 512
   MiB and 2 GiB, and that a VA in a disabled group is rejected before any fetch.
7. GGTT single-level PTE encoding and aperture bounds, including the pre-Gen8 2 GiB ceiling.
8. Lifetime: destroy with outstanding mappings fails; unpin and refcount rules; no double free
   of page tables or backing pages.
9. Command encoding as data: `PIPE_CONTROL` with TLB invalidate and stall set, `MI_FLUSH_DW` with
   post-sync 1h/3h and TLB invalidate, and LRI packets for DCLV and base. Verify field positions
   and that LRI refuses forbidden address ranges.

Each test must cite the PRM rule it enforces, so a later reviewer can audit it.

## 15. Traceability to the Stage 5 Phase 3 checklist

| Checklist item (`docs/stage5-gpu.md` 6.1) | Evidence | Status |
|---|---|---|
| Review Haswell PRM memory-view material and generation-specific volumes | Vol 5, 2b, 2c, 2d, 3, 4 and 12 read; SHA-256 of the primary PDF recorded | Done |
| Confirm Gen7.5 PPGTT hierarchy and entry formats | Vol 5 p.38-43 | Done, with the 1024/512 conflict open |
| Confirm GGTT structure and programming requirements | Vol 5 p.35-36, p.46; `PGTBL_CTL` layout missing | Partial |
| Confirm address width and physical-address constraints | PTE 38:0, PDE 39:0, 2 GiB VA ceiling | Done, ceiling derived |
| Confirm reserved bits, permissions and page sizes | PDE 3:2 reserved; no permission bits; 4 KiB and 32 KiB | Done |
| Confirm directory/table alignment and allocation | 4 KiB base alignment; PD in GGTT; PT in main memory; 32 KiB page alignment | Done |
| Confirm translation invalidation and ordering | `GFX_MODE` 13, `PIPE_CONTROL` 18, `MI_FLUSH_DW` 18, PDE cache caveat | Partial: no completion semantics |
| Record ambiguities and undocumented behavior | Section 12.3 | Done |

Implementation items (6.2-6.5) remain open. They must not be marked complete until code and host
tests exist and the open questions that block them are resolved.

## 16. Required follow-up before implementation is accepted

1. Obtain a full-text or OCR copy of Volume 2c and locate `PP_DIR_BASE` and `PGTBL_CTL2`
   definitions. Until then, keep those offsets out of shipping code.
2. Resolve the 1024-versus-512 PDE count question, preferably by reading the page-directory
   section in the current PRM revision or by hardware experiment.
3. Confirm the exact GGTT aperture and GSM layout on the target HD 4600.
4. Confirm cache-maintenance requirements for CPU writes to page tables.
5. Decide the Ozero default cacheability policy for GPU-visible memory and record it in an ADR.
6. Confirm whether 32 KiB pages are needed for the first engine workload; if not, keep them
   unsupported.
7. Re-check `PIPE_CONTROL` and `MI_FLUSH_DW` opcode values in Volume 2b before encoding.

## 17. References

1. Intel, *Intel Open Source Graphics PRM, Volume 5: Memory Views (Haswell)*, 2013-12-18.
   https://cdrdv2-public.intel.com/690966/intel-gfx-prm-osrc-hsw-memory-views.pdf
   SHA-256 `e72dbaa1eb2d06c75e5b00d97fa07bae053591245ee969f68ee474d034760f30`.
2. Intel, Haswell PRM Volume 2b: Command Reference, Instructions; Volume 2c: Command Reference,
   Registers; Volume 2d: Command Reference, Structures; Volume 12: PCIe Config Registers.
   Archived in https://github.com/Igalia/intel-osrc-gfx-prm/tree/master/005_hsw_-_2013
   (CC-BY-ND 3.0 US, mirrored from Intel's 01.org PRM library).
3. Intel, Technical Library, for current PRM revisions:
   https://www.intel.com/content/www/us/en/developer/technical-library/overview.html
4. Linux i915 reference implementation, used only for arithmetic cross-checks:
   `drivers/gpu/drm/i915/gt/gen6_ppgtt.c`, `intel_gtt.h`, `intel_engine_regs.h`,
   `intel_ring_submission.c`. GPL-2.0. Not a specification and not a source to copy.
