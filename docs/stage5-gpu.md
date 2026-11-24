# Stage 5: GPU memory and Haswell submission

- Status: **Active — Phase 1 implementation in progress; build/QEMU verification outstanding; runtime not verified**
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

- [x] Inspect existing PCI enumeration and configuration-space access.
- [ ] Implement or complete PCI enumeration if required; reuse existing kernel facilities where sound.
- [ ] Enumerate devices and functions deterministically.
- [ ] Read vendor ID, device ID, revision, class and subclass.
- [ ] Identify graphics/display functions.
- [ ] Parse BAR resources, including 64-bit BARs and resource sizes.
- [ ] Validate MMIO ranges, alignment and address overflow.
- [ ] Identify interrupt capabilities and establish device interrupt routing requirements.
- [ ] Establish DMA address-width and alignment constraints.
- [ ] Avoid creating a GPU-only PCI shortcut that duplicates or conflicts with the kernel PCI layer.

### 4.2 GPU identification and safe initialization

- [ ] Define a GPU device descriptor.
- [ ] Match only explicitly researched and supported device IDs/revisions.
- [ ] Add the initial Haswell Gen7.5 backend match.
- [ ] Confirm the actual HD Graphics 4600 device ID and revision on target hardware before device-specific bring-up.
- [ ] Map only validated MMIO resources.
- [ ] Define initialization and shutdown/error unwind paths.
- [ ] Keep unknown devices safely unsupported.
- [ ] Do not probe undocumented registers or infer support from driver-binary strings.

### 4.3 Capability description

- [ ] Record GPU generation and revision.
- [ ] Record GPU virtual-address width and supported page sizes.
- [ ] Record memory domains and DMA constraints.
- [ ] Record detected engines and engine-specific capabilities.
- [ ] Record submission/completion mechanisms.
- [ ] Report preemption, reset and recovery only when documented and implemented.
- [ ] Expose capabilities through a GPU-independent Ozero interface.
- [ ] Add deterministic device-match and capability-validation tests.

**Exit evidence:** tests for supported/unsupported IDs, descriptor validation, BAR parsing and resource bounds; safe unsupported-device behavior; successful kernel integration without breaking boot.

## 5. Phase 2 — GPU-independent memory objects and address spaces

### 5.1 GPU memory objects and ownership

- [ ] Define Ozero-owned GPU memory-object types and lifecycle.
- [ ] Implement object creation and destruction.
- [ ] Track physical-page backing.
- [ ] Implement page reference and ownership accounting.
- [ ] Implement pin/unpin operations.
- [ ] Prevent freeing pages referenced by mappings or in-flight work.
- [ ] Validate size, alignment and allocation limits.
- [ ] Handle allocation failure and partial-construction rollback.

### 5.2 GPU virtual-address allocation

- [ ] Define GPU virtual addresses as a distinct type and namespace from CPU virtual and physical addresses.
- [ ] Implement GPU virtual-address range allocation and release.
- [ ] Enforce backend-provided address width and alignment.
- [ ] Detect arithmetic overflow and invalid ranges.
- [ ] Reject overlapping allocations.
- [ ] Define fragmentation behavior and deterministic failure semantics.
- [ ] Test allocation, release, reuse and boundary cases.

### 5.3 Mapping and access control

- [ ] Define map, unmap and protection/access operations.
- [ ] Validate object backing and GPU address ranges.
- [ ] Track mapping ownership and lifetime.
- [ ] Reject invalid permissions and unsupported attributes.
- [ ] Handle partial mapping failures with rollback.
- [ ] Prevent address-space teardown while work still references mappings.
- [ ] Define how mapping changes become visible to the device.

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

- [ ] Review the Haswell Graphics PRM memory-view material and relevant generation-specific programming reference volumes.
- [ ] Confirm the Gen7.5 PPGTT hierarchy and entry formats.
- [ ] Confirm GGTT structure and programming requirements.
- [ ] Confirm address-width and physical-address constraints.
- [ ] Confirm reserved bits, permissions and supported page sizes.
- [ ] Confirm page-directory/page-table alignment and allocation requirements.
- [ ] Confirm translation-cache invalidation and ordering requirements.
- [ ] Record ambiguities and undocumented behavior as open issues; do not guess.

### 6.2 PPGTT

- [ ] Implement the documented Haswell two-level PPGTT structure.
- [ ] Allocate and own page-directory pages.
- [ ] Allocate and own page-table pages.
- [ ] Support non-contiguous physical backing where the specification permits it.
- [ ] Encode and validate directory entries.
- [ ] Encode and validate page-table entries.
- [ ] Enforce address width, alignment, reserved-bit and permission constraints.
- [ ] Implement map, unmap and address-space teardown.
- [ ] Prevent page-table reclamation while hardware may still reference it.

### 6.3 GGTT

- [ ] Implement Haswell GGTT entry encoding and bounds checks.
- [ ] Implement global mapping allocation, map and unmap.
- [ ] Validate global aperture/resource limits.
- [ ] Keep GGTT and per-context PPGTT responsibilities distinct.
- [ ] Define ownership and teardown for global mappings.

### 6.4 Translation invalidation

- [ ] Implement the documented Haswell invalidation procedure.
- [ ] Enforce required register sequencing and memory ordering.
- [ ] Wait for invalidation completion where required.
- [ ] Define timeout/error behavior.
- [ ] Prevent use of stale mappings.
- [ ] Prevent page-table or backing-page reclamation before invalidation is complete.

### 6.5 Backend tests

- [ ] Test valid and invalid entries.
- [ ] Test low/high and boundary GPU virtual addresses.
- [ ] Test invalid alignment and reserved bits.
- [ ] Test permission validation.
- [ ] Test non-contiguous page-table backing.
- [ ] Test map/unmap/re-map cycles.
- [ ] Test invalidation ordering and timeout paths.
- [ ] Test address-space destruction and memory reclamation.

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
