# Ozero A3 — Haswell Gen7.5 GPU Research Note

Status: **Stage 0 desk artifact; no implementation or hardware validation**

Scope: Intel Haswell Gen7.5 (GT1/GT2/GT3/GT3e), memory management, and
command submission prerequisites
Related: [`01-hardware-support-matrix.md`](01-hardware-support-matrix.md),
[`04-driver-interface-draft.md`](04-driver-interface-draft.md),
[`05-hardware-test-plan.md`](05-hardware-test-plan.md)

## 1. Why Haswell is separate

Haswell is **Gen7.5**, not Gen8. Broadwell changed the page-table format,
relocation width, and context-switch mechanism; later generations also
changed the memory-region model. Code or design text that says "Intel GPU"
without a generation is not precise enough for this project.

The Linux i915 feature table makes the distinction explicit
(`drivers/gpu/drm/i915/i915_pci.c`, v6.6):

- `GEN7_FEATURES` sets `ppgtt_type = INTEL_PPGTT_ALIASING`,
  `ppgtt_size = 31`, `dma_mask_size = 40`, and
  `GEN_DEFAULT_PAGE_SIZES` (4 KB only).
- `G75_FEATURES` adds the VECS engine and Haswell runtime power management.
- `GEN8_FEATURES` sets `has_logical_ring_contexts = 1`,
  `ppgtt_type = INTEL_PPGTT_FULL`, `ppgtt_size = 48`, and
  `has_64bit_reloc = 1`.

Those four differences are architectural, not tuning knobs.

## 2. SKU and engine model

Haswell integrated graphics ships as GT1, GT2, GT3, and GT3e. GT3e is the
Crystal Well multi-chip package with 128 MB of on-package eDRAM. The exact EU
count and engine instance count are SKU-dependent; the programming model is
the same.

The engine classes are:

- **RCS** — render/compute.
- **BCS** — blit.
- **VCS** — video decode.
- **VECS** — video encode.

The i915 `G75_FEATURES` mask includes `RCS0`, `VCS0`, `BCS0`, and `VECS0`.
Do not infer engine counts from that mask; enumerate the engines the platform
actually exposes.

Representative PCI IDs (confirm the exact SKU with `lspci -nn`):

| SKU example | PCI ID |
|---|---|
| Haswell GT2 desktop | `8086:0412` |
| Haswell ULT GT3 mobile | `8086:0a26` |
| Haswell CRW GT3 mobile | `8086:0d26` |

## 3. Memory topology

Haswell is a unified-memory architecture. There is no separate VRAM heap.
The GPU accesses system DRAM through a graphics translation table, with the
last-level cache (LLC) and, on GT3e, eDRAM acting as caches.

```
 CPU / GPU
    |
   LLC  (shared last-level cache)
    |
  DRAM  <---- eDRAM (GT3e only, memory-side cache)
```

eDRAM is **not** a CPU-visible VRAM region and **not** a separate GPU memory
region in the i915 memory-region model. i915's Haswell memory regions are
`REGION_SMEM | REGION_STOLEN_SMEM` (`GEN_DEFAULT_REGIONS`). eDRAM is a cache
that the page-table cacheability fields can target. Treating it as VRAM will
produce a wrong allocator and wrong eviction policy.

## 4. Global GTT (GGTT)

The GGTT is the global graphics translation table:

- Single-level translation from graphics virtual address to physical address.
- One 4-byte PTE per 4 KB page.
- GTT PTE storage is 128, 256, or 512 KB, mapping a 128, 256, or 512 MB
  aperture. i915 reads the size from the GMCH control register
  (`gen6_get_total_gtt_size`, `drivers/gpu/drm/i915/gt/intel_ggtt.c`).
- The aperture is programmed through `PGTBL_CTL`; the GTT itself is visible
  through the GTTADR BAR alias.
- Before Gen8, the graphics memory range (GSM) is limited to 2 MB, which
  allows only 2 GB of physical memory to be indexed. The Haswell PRM calls
  this out in the memory-views volume.

The GGTT is the authoritative mapping for:

- display surfaces and cursor;
- the ring buffer;
- logical context state;
- privileged command-stream structures;
- objects that must be visible to display or to the command streamer before a
  PPGTT is active.

Display and cursor accesses are always through the GGTT. A PPGTT does not
replace the GGTT for display.

## 5. Per-process GTT (PPGTT) on Gen7.5

Haswell has a PPGTT, but it is not the Gen8+ full PPGTT. The important
properties are:

- **Two-level walk**: PDE → PTE. A PDE selects a 4 MB region; a PTE selects a
  4 KB page. Each page table holds 1024 PTEs; a 2 GB virtual space uses 512
  PDEs.
- **32-bit page-table entries**: 4-byte PDEs and PTEs.
- **Page directories live in the GGTT**. i915 pins the PD VMA into the GGTT
  and writes PDEs through the GGTT mapping (`gen6_ppgtt.c`,
  `pd_vma_bind`, `gen6_write_pde`).
- **Page tables live in system memory**. They must be in memory the GPU can
  read; the PRM requires non-snooped system memory for page tables.
- **2 GB virtual address limit in i915**. `ppgtt_size = 31` makes
  `vm.total = BIT_ULL(31)`. The hardware register `RING_PP_DIR_DCLV` is
  programmed with `PP_DIR_DCLV_2G` (`0xffffffff`), and `RING_PP_DIR_BASE`
  supplies the PD base (`intel_ring_submission.c`, `set_pp_dir`).
- **PPGTT enable**: `RING_MODE_GEN7` bit `GFX_PPGTT_ENABLE` (bit 9) is set
  after the PD base is programmed. The PRM describes the PPGTT base through
  `PGTBL_CTL2`; i915 uses the engine-relative `RING_PP_DIR_BASE` on this
  path.
- **Aliasing PPGTT**: i915 creates a 2 GB PPGTT and mirrors global bindings
  into the GGTT (`init_aliasing_ppgtt`, `intel_ggtt.c`). This is why the
  global GTT remains authoritative for display and for objects that must be
  visible before the PPGTT is active.
- **4 KB pages in i915**: `GEN_DEFAULT_PAGE_SIZES` sets
  `I915_GTT_PAGE_SIZE_4K`. The PRM also describes 32 KB big pages in the
  PPGTT; i915's Haswell configuration does not use them. Stage 5 should not
  assume big pages.

The PRM describes PPGTT PDE/PTE formats and the cacheability fields in the
memory-views volume. The key point for Ozero is that **Haswell PPGTT is a
2-level, 2 GB, 4 KB-page address space with PDEs in the GGTT**, not a 48-bit
4-level address space.

## 6. Address translation and cacheability

### 6.1 GGTT walk

```
graphics VA -> GTT index -> 4-byte PTE -> physical page
```

The Haswell PTE is not a simple linear address plus a valid bit. i915's
`HSW_GTT_ADDR_ENCODE` folds physical address bits 32:38 into PTE bits 10:4
(`intel_gtt.h`):

```
HSW_GTT_ADDR_ENCODE(addr) = addr | ((addr >> 28) & 0x7f0)
```

The cacheability fields are outside the low address field. `HSW_PTE_ADDR_ENCODE`
is the Haswell-specific encoder; do not copy the Gen8 encoder.

### 6.2 PPGTT walk

```
graphics VA -> PDE (from GGTT-resident PD) -> PT (system memory) -> PTE -> page
```

The PDE is written with `GEN6_PDE_ADDR_ENCODE(addr) | GEN6_PDE_VALID`. The PTE
is written with the Haswell PTE encoder and a valid bit. The PD VMA's GGTT
offset determines `pp_dir` and the memory-mapped `pd_addr` used to write PDEs.

### 6.3 Cacheability and eDRAM

Haswell PTE cacheability is encoded with `HSW_CACHEABILITY_CONTROL` fields.
i915 has two encoders:

- `hsw_pte_encode` for Haswell without eDRAM: `HSW_WB_LLC_AGE3` for cached
  mappings.
- `iris_pte_encode` for Haswell/Broadwell with eDRAM: `HSW_WT_ELLC_LLC_AGE3`
  for write-through, `HSW_WB_ELLC_LLC_AGE3` for write-back, and uncached for
  `I915_CACHE_NONE`.

eDRAM presence is detected by reading `HSW_EDRAM_CAP` at MMIO `0x120010` and
testing bit 0 (`EDRAM_ENABLED`) in `intel_dram_edram_detect`
(`drivers/gpu/drm/i915/soc/intel_dram.c`). For Haswell and Broadwell, i915
sets `edram_size_mb = 128`; Gen9 computes the size from capability bits.

The Haswell PRM's eDRAM enabling requirement also changes the RING HASH
superpage concept. Setting a PTE cacheability bit is not, by itself, a
complete eDRAM enable sequence. Treat eDRAM enable as a platform sequence
with PRM-defined register programming, not as a single PTE attribute.

## 7. Residency, pinning, and eviction

### 7.1 Objects

A GEM object is backed by system memory. It has no VRAM placement. A VMA
(virtual memory area) binds an object into a VM (GGTT or PPGTT) at a
graphics virtual address.

### 7.2 Pinning

Pinning has two separate meanings:

- **Page pinning**: the backing pages must not be reclaimed while the GPU can
  access them (`i915_gem_object_pin_pages`).
- **VMA pinning**: the object must stay bound at a stable graphics address
  (`i915_vma_pin`, `i915_vma_pin_ww`).

Display scanout, ring buffers, context state, and PPGTT page directories need
stable GGTT bindings. Batch buffers can be PPGTT-bound. The execbuffer path
pins objects and VMAs, then relocates addresses before submission
(`i915_gem_execbuffer.c`).

### 7.3 Eviction

Because everything is in system memory, the driver must be able to evict
objects from a VM and rebind them elsewhere:

- `i915_gem_shrink` reclaims pages under memory pressure.
- `i915_gem_evict_vm` evicts a VM's bindings.
- `i915_gem_evict_for_node` finds space for a new binding.
- `I915_EXEC_NO_RELOC` avoids re-relocation only when the caller can prove
  that `reloc.presumed_offset` still matches the final address and that the
  object was not written. It is an optimization, not a residency guarantee.

A design that assumes "the GPU will page-fault the object in" is not the
i915 model. The PRM describes page-fault hardware additions for Gen7.5, but
the baseline command-submission contract still requires residency and
relocation.

### 7.4 Fences

Haswell has GGTT fence registers for tiled surfaces. Fences are a GGTT
resource; they constrain the aperture address and the tiling layout. They are
not a PPGTT feature. Display and tiled scanout objects need fence-aware
allocation and pinning (`i915_vma_pin_fence`, `intel_ggtt_fencing.c`).

## 8. Relocation

Haswell uses 32-bit graphics addresses and 32-bit relocation entries. The
execbuffer path has six phases (`i915_gem_execbuffer.c`):

1. Validation — handles, flags, pointers.
2. Reservation — assign GPU address space.
3. Relocation — patch addresses to final locations.
4. Serialisation — order against dependencies.
5. Construction — build the request.
6. Submission — execute later.

Relocation entries carry `presumed_offset`. The kernel patches 4-byte or
8-byte fields in the batch. `EXEC_OBJECT_WRITE` marks objects the batch will
write; it affects cloning and relocation safety. `I915_EXEC_NO_RELOC` is
valid only when the presumed offsets are exact and the object was not written.

Haswell does **not** have `has_64bit_reloc`. Do not use Gen8+ relocation
structures, 64-bit presumed offsets, or canonical-address helpers.

## 9. Context and ring setup

Haswell uses the legacy ringbuffer submission model, not execlists:

- Each engine has a ring buffer. The driver writes commands at the tail and
  advances `RING_TAIL`; the hardware advances `RING_HEAD`.
- The logical context state is referenced by a graphics address.
  `MI_SET_CONTEXT` loads it and requires a 4 KB-aligned graphics address.
  The command is legal only when Execlist Enable in `GFX_MODE` is reset;
  otherwise execlists must be used. i915's Haswell path uses
  `MI_SET_CONTEXT`, so Execlist Enable is not the operating mode.
- `MI_SET_CONTEXT` must be followed by `MI_NOOP` for the
  `WaMiSetContext_Hang` workaround. i915 emits the NOOP unconditionally on
  this path.
- `MI_ARB_ON_OFF` with arbitration reset should be programmed before
  `MI_SET_CONTEXT`, and arbitration enabled after it, to avoid conflicting
  with PSMI context-switch flows. The PRM states this in the
  `MI_SET_CONTEXT` programming notes.
- The PPGTT base is programmed with `RING_PP_DIR_DCLV`,
  `RING_PP_DIR_BASE`, and `GFX_PPGTT_ENABLE` in `set_pp_dir`.

Do not use Gen8+ concepts here: no ELSP, no execlist queue, no 20-bit
submission ID, no logical-ring context descriptor, no context-status buffer.

## 10. Command submission

A minimal Haswell submission has these prerequisites:

1. **Address space**: GGTT and/or PPGTT page tables are valid, and the
   context's PPGTT base is programmed.
2. **Residency**: all objects the batch will read or write are pinned, and
   their pages are not reclaimable.
3. **Relocation**: all GPU addresses in the batch are final, or the batch is
   using `I915_EXEC_NO_RELOC` with proven offsets.
4. **Context state**: the logical context object is initialized and
   `MI_SET_CONTEXT` is emitted.
5. **Ring space**: the command stream fits in the ring, and the tail is
   advanced only after the commands are written.
6. **Completion**: a seqno write and a user interrupt are emitted after the
   batch.

A representative RCS sequence is:

```
MI_ARB_ON_OFF (arbitration reset)
MI_SET_CONTEXT (context state, GGTT address)
MI_NOOP
MI_ARB_ON_OFF (arbitration enable)
... pipeline flush / cache invalidate ...
MI_BATCH_BUFFER_START (PPGTT or GGTT, secure or non-secure)
... batch commands ...
MI_BATCH_BUFFER_END
PIPE_CONTROL (flush + QW write of seqno to global GTT)
MI_USER_INTERRUPT
MI_NOOP
```

`hsw_emit_bb_start` sets `MI_BATCH_PPGTT_HSW | MI_BATCH_NON_SECURE_HSW` for a
non-secure PPGTT batch, and clears the security bits for a secure GGTT batch
(`gen6_engine_cs.c`). `MI_BATCH_BUFFER_START` bit 8 selects the address space
when the PPGTT is enabled; bit 13 marks the batch non-secure. The command
streamer will not allow a PPGTT batch to call a GGTT batch by retaining the
PPGTT value.

The breadcrumb path (`gen7_emit_breadcrumb_rcs`) uses `PIPE_CONTROL` with a
QW write to the global GTT status page, followed by `MI_USER_INTERRUPT` and a
`MI_NOOP`. That is the completion signal the driver waits on.

## 11. eDRAM / eLLC on GT3e

- eDRAM is 128 MB on Haswell GT3e (Crystal Well).
- It is a memory-side cache, not a separate GPU memory region.
- Presence is detected at `HSW_EDRAM_CAP` (MMIO `0x120010`, bit 0).
- The PTE cacheability fields choose uncached, write-back LLC, write-back
  eLLC+LLC, or write-through eLLC+LLC.
- Display/scanout surfaces are typically mapped write-through when eDRAM is
  present; the exact policy belongs in the display/scanout path, not in the
  generic buffer allocator.
- The PRM's eDRAM enabling requirement changes the RING HASH superpage
  concept. The same section specifies the IDI hash mask at MMIO `0x9008`
  (bits 21:16), with address-bit mappings for the CBO/ring hash. A complete
  enable sequence is required; a single cache bit is not enough.
- Do not model eDRAM as VRAM. Do not allocate objects "in eDRAM". The
  allocator is system-memory based; eDRAM is a cache hint and a platform
  enable sequence.

## 12. Gen7.5 vs Gen8+ (do not transfer assumptions)

| Property | Haswell Gen7.5 | Broadwell Gen8+ |
|---|---|---|
| PPGTT type | Aliasing PPGTT | Full PPGTT |
| PPGTT virtual size | 31 bits / 2 GB in i915 | 48 bits |
| Page-table walk | 2-level PDE → PTE | Full multi-level walk |
| Page-table entries | 32-bit | 64-bit |
| Page sizes used by i915 | 4 KB | 4 KB at Gen8; larger pages added later |
| Relocation | 32-bit | 64-bit |
| Context switch | Legacy ring + `MI_SET_CONTEXT` | Logical ring context + execlists |
| Submission | `RING_TAIL` + `MI_BATCH_BUFFER_START` | ELSP/execlist queue |
| eDRAM | 128 MB on GT3e | 128 MB on GT3e; Gen9 variable |
| Memory regions | System memory + stolen | System memory + stolen; later local memory |

The table is a boundary, not a suggestion. If a design needs 48-bit VA,
64-bit relocation, or execlists, it is not a Haswell design.

## 13. Stage 5 plan and gates

Stage 5 is split into five reviewable gates. Gate 0 is a prerequisite, not a
support claim. Starting at command submission without the earlier gates
produces a driver that works only on lucky addresses and fails under memory
pressure.

### Gate 0 — Discovery and control

- Enumerate the GPU PCI function and confirm the SKU.
- Map MMIO with A1; establish register access and posting-read rules.
- Implement GPU reset, forcewake, and basic interrupt handling.
- Read `HSW_EDRAM_CAP` and record eDRAM presence.
- Exit criterion: the device can be reset and identified without hangs.

### Gate 5A — GPU objects

- Implement object allocation, handles, size/alignment, tiling, and
  cacheability metadata.
- Implement CPU mmap and object lookup.
- Do not map objects into GPU virtual address space yet.
- Exit criterion: objects can be created, mapped by the CPU, and destroyed
  without leaks.

### Gate 5B — GPU VA mapping

- Program the GGTT, map the aperture, reserve stolen memory, and allocate
  fences.
- Allocate a 2 GB aliasing PPGTT: a PD in the GGTT and page tables in system
  memory.
- Program `RING_PP_DIR_DCLV`, `RING_PP_DIR_BASE`, and `GFX_PPGTT_ENABLE`.
- Implement PDE/PTE insertion, clearing, and TLB invalidation.
- Exit criterion: the same buffer is accessible through GGTT and PPGTT; the
  2 GB boundary is enforced; a context switch changes the PPGTT base without
  a hang.

### Gate 5C — Residency and eviction

- Implement pin/unpin, VMA bind/unbind, eviction, shrinking, and rebind.
- Implement 32-bit relocation and `I915_EXEC_NO_RELOC`-style fast paths.
- Implement fence-aware tiled surfaces.
- Exit criterion: a batch with relocations runs correctly under memory
  pressure and after eviction/rebind.

### Gate 5D — Command validation and submission

- Allocate the ring and context state.
- Validate batch contents, relocation, and command lengths.
- Emit `MI_SET_CONTEXT`, `MI_NOOP`, flush, `MI_BATCH_BUFFER_START`, seqno
  write, and `MI_USER_INTERRUPT`.
- Implement request tracking, timeouts, hang detection, and reset recovery.
- Exit criterion: a no-op batch and a simple write batch complete, and a
  forced hang is recovered.

### Gate 5E — Completion fences

- Implement seqno write, user interrupt, fence signal/wait, and callbacks.
- Add cross-engine fences, suspend/resume, runtime power management, and
  eDRAM policy.
- Measure latency and throughput; validate against IGT.
- Exit criterion: the Stage 5 acceptance suite passes on GT2 and GT3e.

No GPU support claim exists before Gates 5A–5E pass on real Haswell hardware
or a clearly scoped emulator. QEMU is not that emulator.

## 14. Test and validation

QEMU does not provide a faithful Haswell i915 model. GPU validation requires
real hardware.

Test vehicles:

- Haswell GT2 desktop or mobile system.
- Haswell GT3e (Crystal Well) system with eDRAM.
- IGT GPU tools for memory management, contexts, submission, and display.
- i915 debugfs for GTT/PPGTT, fences, and error state.
- `intel_reg_read` / register dumps for eDRAM and PPGTT registers.
- `trace-cmd` / ftrace for i915 events.
- Suspend/resume and GPU hang injection.

Suggested IGT coverage (names depend on the IGT version):

- `gem_exec_basic`, `gem_exec_nop`, `gem_exec_parallel`, `gem_exec_store`;
- `gem_ctx_basic`, `gem_ctx_create`, `gem_ctx_switch`, `gem_ctx_thrash`;
- `gem_mmap`, `gem_reloc`, `gem_wait`, `gem_busy`, `gem_sync`;
- `gem_evict`, `gem_shrink`, `gem_ppgtt`;
- `gem_reset_stats`, `gem_exec_fence`;
- `kms_flip` and basic display tests for GGTT/scanout.

Pass criteria:

- GGTT and PPGTT mappings agree for a shared buffer.
- A 2 GB PPGTT boundary is enforced; an access beyond it faults or is
  rejected, not silently aliased.
- Relocation survives eviction and rebind.
- `MI_SET_CONTEXT` + `MI_NOOP` sequence does not hang.
- Seqno write and user interrupt complete a request.
- A forced GPU hang is detected and recovered without losing unrelated
  requests.
- eDRAM presence is detected and the platform enable sequence completes.

## 15. Risks and things not to assume

- **Do not assume Gen8+ PPGTT.** Haswell is 2-level, 31-bit, aliasing.
- **Do not assume eDRAM is VRAM.** It is a cache and a platform enable
  sequence.
- **Do not assume execlists.** Haswell uses the legacy ring and
  `MI_SET_CONTEXT`.
- **Do not assume 64-bit relocation.** Haswell uses 32-bit addresses.
- **Do not assume 4 KB is the only hardware page size.** The PRM describes
  32 KB big pages; i915 chooses 4 KB. Know which layer you are describing.
- **Do not assume the GPU will page-fault missing objects.** The baseline
  requires residency and relocation.
- **Do not assume display goes through PPGTT.** Display and cursor are GGTT.
- **Do not copy Gen8 PTE encoders.** Haswell's address and cacheability
  encoding is different.
- **Do not treat a QEMU GPU as Haswell.** QEMU's `std`, `qxl`, and
  `virtio-gpu` devices are not i915.

## 16. References

Intel Haswell PRM (archived; the original 01.org pages are retired):

- *Haswell GPU Overview* (Volume 1).
- *Haswell Command Stream Programming* (Volume 2b).
- *Haswell Command Reference: Instructions* (Volume 2b) — `MI_SET_CONTEXT`,
  `MI_BATCH_BUFFER_START`, `MI_FLUSH_DW`, `MI_STORE_DATA_IMM`,
  `MI_USER_INTERRUPT`.
- *Haswell Command Reference: Structures* (Volume 2b).
- *Haswell Memory Views* (Volume 5) — GGTT, PPGTT, PTE formats, eDRAM/eLLC.
- Archived landing page:
  <https://web.archive.org/web/20230108225954/https://01.org/linuxgraphics/documentation/hardware-specification-prms>

Linux i915 (v6.6) — source references used above:

- `drivers/gpu/drm/i915/i915_pci.c` — `GEN7_FEATURES`, `G75_FEATURES`,
  `GEN8_FEATURES`, `GEN_DEFAULT_PAGE_SIZES`, `GEN_DEFAULT_REGIONS`.
- `drivers/gpu/drm/i915/i915_drv.h` — `HAS_EDRAM`, `HAS_WT`,
  `HAS_FULL_PPGTT`, `HAS_LOGICAL_RING_CONTEXTS`, `HAS_64BIT_RELOC`.
- `drivers/gpu/drm/i915/gt/intel_ppgtt.c` — `gen7_ppgtt_enable`,
  `__ppgtt_create`, `vm.total = BIT_ULL(ppgtt_size)`.
- `drivers/gpu/drm/i915/gt/gen6_ppgtt.c` — `gen6_ppgtt_create`,
  `gen6_write_pde`, `pd_vma_bind`, `gen6_ppgtt_pin`, `gen7_ppgtt_enable`.
- `drivers/gpu/drm/i915/gt/intel_ggtt.c` — `init_aliasing_ppgtt`,
  `gen6_get_total_gtt_size`, `hsw_pte_encode`, `iris_pte_encode`.
- `drivers/gpu/drm/i915/gt/intel_gtt.h` — `HSW_CACHEABILITY_CONTROL`,
  `HSW_GTT_ADDR_ENCODE`, `HSW_PTE_ADDR_ENCODE`.
- `drivers/gpu/drm/i915/gt/intel_engine_regs.h` — `RING_PP_DIR_DCLV`,
  `PP_DIR_DCLV_2G`, `RING_PP_DIR_BASE`, `GFX_PPGTT_ENABLE`.
- `drivers/gpu/drm/i915/gt/intel_ring_submission.c` — `set_pp_dir`,
  `MI_SET_CONTEXT` emission, `MI_NOOP` workaround, Haswell engine setup.
- `drivers/gpu/drm/i915/gt/gen6_engine_cs.c` — `hsw_emit_bb_start`,
  `gen7_emit_flush_rcs`, `gen7_emit_breadcrumb_rcs`.
- `drivers/gpu/drm/i915/gt/intel_gpu_commands.h` —
  `__gen6_emit_bb_start`, `MI_BATCH_PPGTT_HSW`,
  `MI_BATCH_NON_SECURE_HSW`.
- `drivers/gpu/drm/i915/gem/i915_gem_execbuffer.c` — execbuffer phases,
  relocation, `I915_EXEC_NO_RELOC`.
- `drivers/gpu/drm/i915/soc/intel_dram.c` — `intel_dram_edram_detect`.
- `drivers/gpu/drm/i915/i915_reg.h` — `HSW_EDRAM_CAP` (`0x120010`),
  `EDRAM_ENABLED`.

General Intel references:

- Intel, *Intel 64 and IA-32 Architectures Software Developer's Manual*.
  <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
- Intel, *Virtualization Technology for Directed I/O Architecture
  Specification*.
  <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
- Linux kernel, *i915* documentation.
  <https://docs.kernel.org/gpu/i915.html>
- IGT GPU Tools.
  <https://gitlab.freedesktop.org/drm/igt-gpu-tools>
