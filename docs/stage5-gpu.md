# Stage 5: GPU memory and Haswell submission

- Status: **Active — Phase 1 GPU discovery implemented, host-tested and wired into the boot self-test (the verified Intel HD 4600, PCI 8086:0412, is now matched; all other devices stay unsupported); Phase 2 core builds, host-tested and now exercised on the boot path; Phase 3 specification review complete (two items partial) and the PPGTT/GGTT backend implemented and host-tested, with hardware installation and invalidation emission open**
- Roadmap stage: 5
- Initial hardware backend: Intel Haswell Gen7.5 integrated graphics
- Concrete bring-up target: Intel HD Graphics 4600 (device ID and revision must be confirmed on the target machine)
- Scope: GPU discovery, GPU memory/address spaces, Haswell GGTT/PPGTT, engine submission/completion, contexts/queues, verification and documentation.

## 1. Goal and scope

Stage 5 establishes Ozero's first GPU execution substrate. Haswell Gen7.5 is the first backend because it is a concrete engineering target. It is a bring-up target, not the definition of all GPUs Ozero may support.

The GPU-independent core must not inherit Intel-specific page-table, memory-domain, engine, command-stream, or preemption assumptions. Future Intel generations, AMD, NVIDIA and other devices require separately researched backends. Listing a vendor does not claim support.

Stage 5 does not deliver a complete graphics API, desktop compositor, display stack, game compatibility layer or Windows graphics compatibility implementation. Those belong to later stages.

## 2. Architectural constraints

Keep three boundaries explicit:

1. **GPU-independent Ozero core:** device identity and capability records, resource lifetime, generic GPU virtual-address allocation, memory-object accounting, synchronization, context/queue lifecycle and common error reporting.
2. **Generation/vendor backend:** hardware-specific address-space format, page-table entries, translation invalidation, memory placement/coherency, engine discovery, command validation, submission registers, interrupts/fences, reset and recovery.
3. **Future graphics services and APIs:** consumers of the GPU substrate; not part of Stage 5 hardware bring-up.

Capabilities must be explicit. Optional behavior remains optional; unsupported operations fail clearly rather than being silently emulated. Do not force every GPU into a lowest-common-denominator feature set.

Ozero's native graphics contracts are Ozero-defined. Linux DRM/KMS and Windows WDDM may be studied as external references, but neither is a required native architecture or dependency. Ozero's native execution architecture and future Windows compatibility remain independent.

## 3. Phase 0 — Agent inspection and preparation

**Phase status: COMPLETE**

Project status: **accepted complete by the Project Director**.

- [x] Read the complete repository `AGENTS.md`.
- [x] Read the repository `README.md` and relevant Stage 5 documents.
- [x] Inspect the current branch and repository status.
- [x] Review the current kernel architecture and identify GPU-related prerequisites and dependencies.
- [x] Review the GPU core/backend separation and the Haswell-first strategy.
- [x] Review the Stage 5 sequence, risks, unknowns and verification boundaries.
- [x] Confirm Stage 4 is the completed prior milestone and Stage 5 is active.
- [x] Establish that implementation changes wait until the inspection gate is accepted.

**Exit:** Phase 0 is complete. This status records the Project Director's milestone decision; it is not a claim that GPU implementation or hardware validation has occurred.

## 4. Phase 1 — GPU discovery and device initialization

### 4.1 PCI discovery prerequisites

**Current PCI integration finding (2026-11-24):**

The first PCI enumerator commit added `kernel/src/pci.c` and its public header, and added the object to the kernel build, but the kernel boot path did not include `ozero/pci.h` or call `o0_pci_init()`. The code was therefore compiled but never executed; a successful build alone could not demonstrate that PCI enumeration was happening.

This integration gap was corrected in commit [`be929464`](https://github.com/Momegaii/Ozero/commit/be9294647e767ec4ad8780c78da55be7f5e3c6a6): `kernel/src/main.c` now includes `ozero/pci.h` and calls `o0_pci_init()` from the Stage 4 boot self-test, printing the returned device count as `OZERO: pci devices=<count>`.

**Evidence boundary:** the call is present in source, but this does not yet prove successful runtime enumeration. Build and boot verification, inspection of actual discovered BDF/IDs, and the remaining PCI resource work are still required. The call currently lives in the Stage 4 self-test as an initial boot integration point; it is not a completed Stage 5 GPU discovery subsystem.

**GPU discovery implementation (2026-11-24):** `kernel/src/gpu_device.c` adds a GPU-independent discovery walk over the kernel PCI layer (`include/ozero/gpu_device.h`). It classifies display functions (PCI base class 0x03), probes each BAR, selects a validated MMIO window, matches a registered backend, and records a capability set. It is wired into the boot path by `stage5_self_test` in `kernel/src/main.c` (`OZERO: gpu pci display devices=`, one `OZERO: gpu device ...` line per display function, then `OZERO: gpu discovery complete`). Verified under QEMU (`qemu64` and `Haswell-v1`): the emulated VGA is reported with its real MMIO window and `backend=none supported=no`. Two latent PCI defects surfaced and were fixed while wiring this up: `o0_pci_probe_bar` self-deadlocked on the non-recursive config lock (it now takes the lock once via locked accessors), and 32-bit memory-BAR sizes were computed in 64 bits (now computed in 32 bits). The Haswell match now claims the verified Intel HD 4600 (PCI 8086:0412, revision 0x06) read from a real host; every other device is still reported unsupported. Under QEMU the emulated VGA is not the HD 4600, so the boot log shows `backend=none`, and the match is proven by a host unit test against the real 8086:0412 ID.

- [x] Inspect existing PCI enumeration and configuration-space access.
- [x] Implement or complete PCI enumeration if required; reuse existing kernel facilities where sound. (`kernel/src/gpu_device.c` reuses `ozero/pci.h` rather than duplicating it.)
- [x] Enumerate devices and functions deterministically. (`o0_pci_init` walk is deterministic; `o0_gpu_discover` iterates the same order.)
- [x] Read vendor ID, device ID, revision, class and subclass. (captured into `struct o0_gpu_device`; see the boot log `gpu device ... vendor=... device=... class=... rev=...`.)
- [x] Identify graphics/display functions. (`o0_gpu_is_display_class`, PCI base class 0x03.)
- [x] Parse BAR resources, including 64-bit BARs and resource sizes. (`o0_pci_probe_bar` sizes 32-bit and 64-bit memory BARs correctly after the 32-bit sizing fix; `o0_gpu_discover` probes every BAR of a display function.)
- [x] Validate MMIO ranges, alignment and address overflow. (`o0_gpu_validate_mmio_bar`: memory BAR, non-zero power-of-two size, size-aligned base, no overflow.)
- [ ] Identify interrupt capabilities and establish device interrupt routing requirements. (MSI/MSI-X offsets are parsed by the PCI layer, but GPU interrupt routing is not established and no engine/device is claimed yet.)
- [ ] Establish DMA address-width and alignment constraints. (`struct o0_pci_dma_constraints` exists but is not populated for GPUs; needs a matched device.)
- [x] Avoid creating a GPU-only PCI shortcut that duplicates or conflicts with the kernel PCI layer. (discovery is built on `ozero/pci.h`.)

### 4.2 GPU identification and safe initialization

- [x] Define a GPU device descriptor. (`struct o0_gpu_device` in `include/ozero/gpu_device.h`.)
- [x] Match only explicitly researched and supported device IDs/revisions. (`o0_gpu_match_backend` consults registered backends; a device is `supported` only when a backend returns its id.)
- [x] Add the initial Haswell Gen7.5 backend match. (`o0_gpu_hsw_backend.match` claims the verified Intel HD 4600, PCI 8086:0412, and returns `O0_GPU_BACKEND_NONE` for everything else. Host-tested.)
- [x] Confirm the actual HD Graphics 4600 device ID and revision on target hardware before device-specific bring-up. (Read from a real host: `PCI\VEN_8086&DEV_0412&SUBSYS_85341043&REV_06`, "Intel(R) HD Graphics 4600", driver 10.18.14.4264. The match table records vendor 0x8086 / device 0x0412 / observed revision 0x06. Other unresearched SKUs, e.g. 0x0416, stay unsupported until verified.)
- [ ] Map only validated MMIO resources. (A validated MMIO window is selected and recorded, but not yet mapped into a CPU/GPU address space; that needs a matched device and a backend mapping path.)
- [ ] Define initialization and shutdown/error unwind paths. (No device is initialized because none matches; the discovery record has no live device state to unwind.)
- [x] Keep unknown devices safely unsupported. (`o0_gpu_describe` sets `supported=false`; `o0_gpu_discover` records and never aborts boot. Verified under QEMU: the emulated VGA is reported `backend=none supported=no`.)
- [x] Do not probe undocumented registers or infer support from driver-binary strings. (Discovery reads standard PCI configuration only; the backend claims no device.)

### 4.3 Capability description

- [x] Record GPU generation and revision. (`struct o0_gpu_caps.generation` plus the descriptor `revision`; the Haswell capability set from `o0_gpu_hsw_backend.fill_caps` is attached to the matched HD 4600 and host-tested.)
- [x] Record GPU virtual-address width and supported page sizes. (Haswell capability set: 32-bit VA, 2 GiB ceiling, 4 KiB and 32 KiB pages; attached to the verified HD 4600 (8086:0412) and host-tested.)
- [ ] Record memory domains and DMA constraints. (Not modelled yet; `struct o0_gpu_caps` has no memory-domain field and DMA constraints are not populated for GPUs.)
- [x] Record detected engines and engine-specific capabilities. (Haswell capability set records render, blitter, video and media engines from the PRM register map.)
- [x] Record submission/completion mechanisms. (`caps.submission_supported=false` is recorded; no submission path exists until Phase 4.)
- [x] Report preemption, reset and recovery only when documented and implemented. (`caps.preemption_reported=false`, `caps.reset_reported=false`.)
- [x] Expose capabilities through a GPU-independent Ozero interface. (`struct o0_gpu_caps`/`struct o0_gpu_device` in `include/ozero/gpu_device.h`, independent of any vendor format.)
- [x] Add deterministic device-match and capability-validation tests. (`tests/gpu_test.c`: display classification, MMIO validation/selection, describe-unsupported, Haswell capability set.)

**Exit evidence:** tests for supported/unsupported IDs, descriptor validation, BAR parsing and resource bounds; safe unsupported-device behavior; successful kernel integration without breaking boot.

## 5. Phase 2 — GPU-independent memory objects and address spaces

**Phase status: PARTIAL — builds, host-tested, and exercised once on the boot path by the Stage 5 self-test; no device integration and the residency, eviction, coherency and access-control items below remain open.**

`kernel/src/gpu.c` defines memory objects, a first-fit GPU virtual-address allocator and map/unmap helpers against `include/ozero/gpu.h`. It builds, is host-tested (`tests/gpu_test.c`), and `stage5_self_test` in `kernel/src/main.c` now exercises the create/alloc/map/unmap/free/destroy lifecycle once on the real kernel heap (`OZERO: gpu core self-test complete`). It still has no device integration; the residency, eviction, coherency and access-control checklist items below are open.

### 5.1 GPU memory objects and ownership

- [x] Define Ozero-owned GPU memory-object types and lifecycle. (`struct o0_gpu_memory` in `include/ozero/gpu.h`.)
- [x] Implement object creation and destruction. (`o0_gpu_memory_create`/`o0_gpu_memory_destroy`; host-tested.)
- [x] Track physical-page backing. (`phys_pages[]` plus `page_count`; per-page PMM allocation.)
- [ ] Implement page reference and ownership accounting. (A `refcount` is taken on map and dropped on unmap; there is no separate reference()/release() API and no ownership transfer yet.)
- [x] Implement pin/unpin operations. (`o0_gpu_memory_pin`/`o0_gpu_memory_unpin`; host-tested.)
- [ ] Prevent freeing pages referenced by mappings or in-flight work. (`o0_gpu_memory_destroy` refuses while pinned or `refcount > 1`; there is no in-flight concept yet.)
- [x] Validate size, alignment and allocation limits. (create rejects zero size; the VA allocator rejects unaligned sizes and enforces the range-count limit.)
- [x] Handle allocation failure and partial-construction rollback. (`o0_gpu_memory_create` unwinds partial PMM/slab allocations; pool-exhaustion path is host-tested.)

### 5.2 GPU virtual-address allocation

- [ ] Define GPU virtual addresses as a distinct type and namespace from CPU virtual and physical addresses. (Currently plain `uint64_t`; a distinct type is a later decision.)
- [x] Implement GPU virtual-address range allocation and release. (`o0_gpu_vm_alloc`/`o0_gpu_vm_free`; host-tested.)
- [x] Enforce backend-provided address width and alignment. (The VM is created with a base/size range; the allocator enforces power-of-two alignment.)
- [x] Detect arithmetic overflow and invalid ranges. (base+size and candidate+size overflow are checked; host-tested.)
- [x] Reject overlapping allocations. (The allocator skips live ranges and mapping rejects overlap with `O0_EBUSY`; host-tested.)
- [ ] Define fragmentation behavior and deterministic failure semantics. (First-fit with a fixed 64-range table; not yet documented as a policy.)
- [x] Test allocation, release, reuse and boundary cases. (`tests/gpu_test.c`.)

### 5.3 Mapping and access control

- [ ] Define map, unmap and protection/access operations. (Map/unmap exist; no permission or access operation yet.)
- [x] Validate object backing and GPU address ranges. (`o0_gpu_vm_map` checks the object size and that the VA range is inside the space; host-tested.)
- [x] Track mapping ownership and lifetime. (`struct o0_gpu_mapping` plus the object `refcount`; host-tested.)
- [ ] Reject invalid permissions and unsupported attributes. (No permission model yet.)
- [ ] Handle partial mapping failures with rollback. (No partial-mapping path yet.)
- [ ] Prevent address-space teardown while work still references mappings. (`o0_gpu_vm_destroy` refuses while ranges remain; no work/in-flight tracking yet.)
- [ ] Define how mapping changes become visible to the device. (Needs a backend and an invalidation path; Phase 3/4.)

### 5.4 Residency and eviction

- [ ] Define resident, nonresident, pinned, evictable and in-flight states as appropriate to the backend.
- [ ] Track residency per memory object/mapping.
- [ ] Prevent eviction of pinned or in-flight resources.
- [ ] Implement an initial deterministic eviction policy.
- [ ] Define allocation-pressure behavior.
- [ ] Implement re-residency and remapping.
- [ ] Define failure behavior when residency cannot be established.

### 5.5 CPU/GPU visibility and coherency

- [ ] Research the target's actual cache/coherency requirements.
- [ ] Define CPU-to-GPU and GPU-to-CPU ownership/visibility transitions.
- [ ] Implement required cache operations and memory ordering.
- [ ] Ensure GPU writes are visible before CPU consumers read them.
- [ ] Ensure CPU writes are visible before GPU consumers execute.
- [ ] Document which operations are coherent, noncoherent or unsupported on the backend.

**Exit evidence:** host tests for allocation, overflow, overlap, mapping, access control, ownership, references, pinning, residency bookkeeping, rollback and coherency state transitions.

## 6. Phase 3 — Intel Haswell Gen7.5 PPGTT and GGTT

This phase is Haswell-specific. Do not reuse Gen8+ GPU page-table assumptions.

### 6.1 Specification review

Evidence is recorded in [docs/research/haswell-gen75-ppgtt-ggtt.md](research/haswell-gen75-ppgtt-ggtt.md),
which cites the primary PRM Volume 5 (2013-12-18) and the supporting 2013 Haswell volumes by
section and page.

- [x] Review the Haswell Graphics PRM memory-view material and relevant generation-specific programming reference volumes. (Volumes 5, 2b, 2c, 2d and 12 read; SHA-256 of the primary PDF recorded.)
- [x] Confirm the Gen7.5 PPGTT hierarchy and entry formats. (Two-level directory/table; PDE and PTE bit layouts. The 1024-versus-512 conflict is resolved: 512 directory entries, 1024 page-table entries, because `PP_DCLV` covers 32 groups of 16 entries.)
- [ ] Confirm GGTT structure and programming requirements. (Single-level walk, PTE format and the GTTADR aperture alias confirmed; Volume 5 p.33 gives base alignment, table sizes and the programming rule, but the `PGTBL_CTL`/`PGTBL_CTL2` bit layouts are not present in Volumes 2b, 2c or 5.)
- [x] Confirm address-width and physical-address constraints. (PTE physical bits 38:0, PDE page-table bits 39:0, 2 GiB virtual-address ceiling derived from the directory-cache coverage.)
- [x] Confirm reserved bits, permissions and supported page sizes. (PDE bits 3:2 reserved; no permission bits in either entry; 4 KiB and 32 KiB page sizes.)
- [x] Confirm page-directory/page-table alignment and allocation requirements. (4 KiB base alignment; directory in GGTT; tables in unsnooped main memory; 32 KiB page alignment for big pages.)
- [ ] Confirm translation-cache invalidation and ordering requirements. (Command bits, `GFX_MODE` bit 13 mode semantics and the directory-cache caveat confirmed. There is no documented completion bit; the TLBPEND registers report arbiter throttling, not invalidation progress.)
- [x] Record ambiguities and undocumented behavior as open issues; do not guess. (Section 12.3 of the dossier.)

### 6.2 PPGTT

- [x] Implement the documented Haswell two-level PPGTT structure. (`o0_hsw_ppgtt_map`/`translate` walk directory then table.)
- [x] Allocate and own page-directory pages. (`o0_hsw_ppgtt_create`, one 4 KiB page from the pool.)
- [x] Allocate and own page-table pages. (Allocated on first map into a 4 MiB window, zeroed, freed on destroy.)
- [x] Support non-contiguous physical backing where the specification permits it. (Each table is a separate pool allocation; host test maps into two tables with unrelated physical pages.)
- [x] Encode and validate directory entries. (`o0_hsw_encode_pde`/`decode_pde`; reserved bits 3:2 rejected.)
- [x] Encode and validate page-table entries. (`o0_hsw_encode_pte`/`decode_pte`; 4-bit cacheability split across bits 3:1 and 11.)
- [x] Enforce address width, alignment, reserved-bit and permission constraints. (VA below 2 GiB, 4 KiB alignment, physical address below 512 GiB for PTEs and 1 TiB for PDEs; the format carries no permission bits, and undefined cacheability 0001 is rejected.)
- [x] Implement map, unmap and address-space teardown. (Teardown refuses to free an active space.)
- [x] Prevent page-table reclamation while hardware may still reference it. (`o0_hsw_ppgtt_destroy` returns without freeing while the space is marked active.)

Implementation: `include/ozero/gpu_hsw.h`, `kernel/src/gpu_hsw.c`, tests in `tests/gpu_test.c`.

### 6.3 GGTT

- [x] Implement Haswell GGTT entry encoding and bounds checks. (Same PTE format as the PPGTT; addresses outside `entries * 4 KiB` are rejected.)
- [x] Implement global mapping allocation, map and unmap. (Software model of the table contents; no GSM installation yet.)
- [x] Validate global aperture/resource limits. (Table size must be a power-of-two number of 4 KiB pages because the pool hands out contiguous power-of-two runs.)
- [x] Keep GGTT and per-context PPGTT responsibilities distinct. (Separate types, separate API, no shared state.)
- [x] Define ownership and teardown for global mappings. (`o0_hsw_ggtt` owns one contiguous run; `destroy` releases it; `fill_dummy` implements the PRM recommendation to map every entry.)

Not done: installing the table into hardware (`PGTBL_CTL` layout is unpublished) and the real aperture size on the target machine.

### 6.4 Translation invalidation

- [ ] Implement the documented Haswell invalidation procedure. (Needs ring buffers; blocked on Phase 4.)
- [ ] Enforce required register sequencing and memory ordering. (Register sequencing needs engine MMIO and LRI; blocked on Phase 4.)
- [ ] Wait for invalidation completion where required. (No completion bit is documented; Section 12.3 of the dossier.)
- [ ] Define timeout/error behavior. (Follows from the previous item.)
- [x] Prevent use of stale mappings. (Every map and unmap sets a dirty flag reported by `o0_hsw_ppgtt_needs_invalidation` and cleared only by `o0_hsw_ppgtt_invalidated`. While a space is marked active, map and unmap return `O0_EBUSY` because the invalidation cannot be emitted yet.)
- [x] Prevent page-table or backing-page reclamation before invalidation is complete. (Teardown refuses to free an active space; an inactive space cannot be referenced by an engine.)

The backend records the invalidation obligation but emits no commands. That is a deliberate boundary, not an oversight: command emission requires the Phase 4 ring.

### 6.5 Backend tests

- [x] Test valid and invalid entries. (PTE/PDE round-trips, invalid PDE bits, invalid PTE lookup.)
- [x] Test low/high and boundary GPU virtual addresses. (Address 0, the last page, the 2 GiB ceiling and one page past it.)
- [x] Test invalid alignment and reserved bits. (Unaligned VA and physical address, PDE bits 3:2.)
- [x] Test permission validation. (The format has no permission bits; undefined cacheability 0001 is rejected.)
- [x] Test non-contiguous page-table backing. (Two page tables from unrelated pool runs.)
- [x] Test map/unmap/re-map cycles. (Map, translate with offset, unmap, re-map, translate again.)
- [ ] Test invalidation ordering and timeout paths. (Not implemented; see 6.4.)
- [x] Test address-space destruction and memory reclamation. (Every pool allocation is returned on destroy; an active space is not freed.)

`make test` runs 10 GPU tests in `tests/gpu_test.c` alongside the existing suites.

**Exit evidence:** specification-traceable host tests for PPGTT/GGTT construction and lifecycle. QEMU evidence is required only for behavior genuinely implemented by its emulated device; host tests do not prove physical GPU execution.

## 7. Phase 4 — Engine-specific command submission and completion

### 7.1 Engine discovery and contracts

- [ ] Identify engines available on the exact supported Haswell SKU.
- [ ] Research render, blitter and video engines separately where present.
- [ ] Identify engine-specific registers, ring requirements and command formats.
- [ ] Record engine availability as per-device capabilities.
- [ ] Do not assume all engines exist on every SKU or share one submission contract.

### 7.2 Command buffers and validation

- [ ] Define command-buffer objects and ownership.
- [ ] Allocate and map command-buffer backing memory.
- [ ] Enforce engine-specific alignment and size limits.
- [ ] Define the initial explicitly supported command set.
- [ ] Validate opcodes, lengths, bounds and referenced GPU addresses.
- [ ] Reject unsupported commands and invalid memory references.
- [ ] Restrict command access to authorized resources.
- [ ] Prevent mutation or destruction while commands are in flight.

### 7.3 Ring buffers and submission

- [ ] Implement per-engine ring-buffer state.
- [ ] Initialize ring resources according to the Haswell programming reference.
- [ ] Implement head/tail tracking and wraparound.
- [ ] Implement command submission and required register sequencing.
- [ ] Enforce memory ordering and visibility before submission.
- [ ] Handle full-ring, invalid-state and hardware-error conditions.
- [ ] Keep engine-specific implementation inside the Haswell backend.

### 7.4 Completion and fences

- [ ] Define Ozero GPU fence objects and sequence semantics.
- [ ] Implement completion tracking.
- [ ] Implement interrupt-based completion where supported.
- [ ] Implement polling where appropriate.
- [ ] Implement bounded waits and timeout reporting.
- [ ] Release resources only after confirmed completion or a documented safe recovery path.
- [ ] Test delayed completion, timeout and error propagation.

### 7.5 Hang detection and reset

- [ ] Research documented Haswell engine hang detection and reset procedures.
- [ ] Implement watchdog/timeout handling.
- [ ] Implement reset only where its sequence is sufficiently documented.
- [ ] Define effects of reset on contexts, queues, fences and resources.
- [ ] Quarantine resources while hardware ownership is uncertain.
- [ ] Preserve diagnostic state for failed submissions and resets.

**Exit evidence:** command-validation, ring-state, submission-state, fence, timeout and recovery tests. Hardware/emulator execution evidence must be labeled by the exact environment and feature exercised.

## 8. Phase 5 — Contexts, queues and scheduling

Begin only after address spaces and single-engine submission are reliable.

### 8.1 Context lifecycle and isolation

- [ ] Define GPU context objects and ownership.
- [ ] Implement context creation and destruction.
- [ ] Associate each context with its address-space state.
- [ ] Track context-owned resources and in-flight references.
- [ ] Prevent destruction while work remains in flight.
- [ ] Define and test the isolation guarantees actually provided.

### 8.2 Queue lifecycle and ordering

- [ ] Define queue objects and supported engine association.
- [ ] Implement queue creation and destruction.
- [ ] Serialize submissions where required.
- [ ] Define queue ordering and fence association.
- [ ] Implement queue error and cancellation states.
- [ ] Test ordering and lifetime across multiple queues.

### 8.3 Scheduling and preemption

- [ ] Define the initial bounded scheduling policy.
- [ ] Prevent indefinite resource ownership and unbounded waits.
- [ ] Integrate watchdog and timeout behavior.
- [ ] Research Haswell preemption support before implementation.
- [ ] Implement preemption only if the target hardware contract is documented and verified.
- [ ] Otherwise implement and document a bounded non-preemptive policy.
- [ ] Report scheduling/preemption capabilities per backend; never promise universal preemption.

### 8.4 Cancellation and reset interaction

- [ ] Define pending-work behavior during cancellation and reset.
- [ ] Define fence results for cancelled, failed and reset work.
- [ ] Ensure cancelled work cannot later access freed memory.
- [ ] Test context destruction with pending submissions.
- [ ] Test queue failure and recovery.
- [ ] Test multiple contexts sharing an engine where supported.

**Exit evidence:** context and queue lifecycle tests, ordering/fence tests, cancellation/reset tests and explicit documented scheduling guarantees.

## 9. Phase 6 — Backend boundaries and future GPU expansion

- [ ] Keep generic memory-object and resource-lifetime policy out of vendor register code.
- [ ] Keep Haswell PPGTT/GGTT encoding inside the Haswell backend.
- [ ] Keep Haswell command packets and engine submission inside the backend.
- [ ] Define capability-driven interfaces without flattening meaningful hardware differences.
- [ ] Define clear unsupported-feature errors.
- [ ] Verify the core does not assume Haswell page sizes, address widths, memory domains, cache behavior, engines or preemption.
- [ ] Document the evidence and implementation required to add another backend.
- [ ] Do not create empty vendor backends or claim unsupported Intel/AMD/NVIDIA devices work.

**Exit evidence:** architecture review confirms that Haswell-specific implementation details do not leak into generic contracts and future backends can express materially different capabilities without pretending they are identical.

## 10. Phase 7 — Verification, integration and acceptance

### 10.1 Host-side test coverage

- [ ] Device matching and capability validation.
- [ ] Resource/BAR parsing and bounds validation.
- [ ] GPU VA allocation, alignment, overflow and overlap.
- [ ] Memory-object lifetime, references and pinning.
- [ ] Mapping, unmapping, protection and rollback.
- [ ] Residency and eviction bookkeeping.
- [ ] Coherency/visibility state transitions.
- [ ] Haswell PPGTT/GGTT construction and teardown.
- [ ] Invalidation ordering and error paths.
- [ ] Command-buffer validation.
- [ ] Ring-buffer state and wraparound.
- [ ] Fence completion, timeout and error paths.
- [ ] Context/queue lifecycle, ordering and cancellation.

### 10.2 Kernel integration

- [ ] Integrate GPU discovery with the existing PCI/device architecture.
- [ ] Integrate GPU memory backing with PMM using explicit ownership rules.
- [ ] Integrate synchronization with existing kernel primitives.
- [ ] Ensure unsupported devices and initialization failures do not break kernel boot.
- [ ] Add serial diagnostics for discovery, capabilities, initialization and errors.
- [ ] Add a safe GPU self-test only for operations that can be performed reliably.

### 10.3 QEMU and physical verification

- [ ] Build and run the existing kernel checks and host test suites.
- [ ] Verify kernel boot with GPU support compiled in.
- [ ] Verify PCI discovery and unsupported-device handling under QEMU.
- [ ] Test GPU behavior under QEMU only if the selected emulated device genuinely implements it.
- [ ] Record QEMU boot separately from emulated GPU execution.
- [ ] Defer physical HD 4600 validation to the planned final hardware-validation phase.
- [ ] At physical validation, test device identification, MMIO, memory mapping, PPGTT/GGTT, submission, fences, timeouts, reset and repeated-workload stability.

### 10.4 Documentation and final review

- [ ] Update this document as tasks are implemented; do not mark tasks complete without evidence.
- [ ] Update `docs/PROJECT_STATUS.md` and `docs/ROADMAP.md`.
- [ ] Document supported device IDs/revisions, implemented engines and command subset.
- [ ] Document unsupported features, unresolved questions and known limitations.
- [ ] Record exact build/test commands, environment and results.
- [ ] Obtain an independent agent review of architecture, resource lifetime, memory safety and hardware sequencing.

**Exit evidence:** all required software phases implemented and tested; kernel integration stable; actual support claims bounded by available hardware evidence; documentation and review complete. Physical validation status remains separately reported if deferred.

## 11. Stage 5 completion criteria

Stage 5 is complete only when:

1. Supported GPU discovery works and unknown devices fail safely.
2. Ozero GPU memory objects, GPU virtual-address allocation, mapping, ownership and residency are implemented.
3. Haswell Gen7.5 PPGTT and GGTT are implemented from generation-correct specifications.
4. Required translation invalidation and coherency rules are implemented.
5. At least the selected initial engine has validated command submission and completion fences.
6. Timeouts, uncertain hardware ownership and recovery paths cannot cause unsafe resource reuse.
7. Basic contexts and queues have defined lifecycle, ordering and isolation guarantees.
8. The GPU-independent core does not impose Haswell-only assumptions.
9. Host tests and kernel integration evidence pass, with QEMU and physical evidence reported separately.
10. Documentation and independent review reflect the actual implementation.

## 12. Explicit non-goals and open questions

- No complete desktop, compositor, display stack or high-level graphics API.
- No Windows WDDM driver port and no Linux DRM dependency.
- No claim that HD Graphics 4600 represents every Haswell SKU or all GPUs.
- No universal page-table format, command stream, engine model or preemption contract.
- Confirm exact target device ID/revision before physical bring-up.
- Confirm which Haswell engine submission, interrupt and reset details are covered by available public programming references.
- Resolve PCI enumeration/resource-interface prerequisites from the actual repository state.
- Do not block host-side architecture and test work solely because physical hardware validation is deferred.

## 13. References

1. Intel, **Haswell Graphics PRM — Memory Views**: https://cdrdv2-public.intel.com/690966/intel-gfx-prm-osrc-hsw-memory-views.pdf. Primary reference for Haswell memory views and Gen6–Gen7.5 PPGTT structure.
2. Intel, **Intel 64 and IA-32 Architectures Software Developer Manuals**: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html. CPU/platform reference; does not replace the graphics programming reference.
3. Intel, **Technical Library**: https://www.intel.com/content/www/us/en/developer/technical-library/overview.html. Starting point for generation-specific graphics programming reference volumes and revisions.

The Intel Windows driver files supplied during this discussion are research artifacts only, subject to their licenses. They are not authoritative specifications and must not be copied into the kernel. Strings and symbols are investigative clues, not proof of register behavior or hardware support.
