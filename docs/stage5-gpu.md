# Stage 5: GPU memory and Haswell submission

- Status: **Proposed implementation plan** — not an implementation completion claim or an approved cross-vendor driver contract
- Roadmap stage: 5
- Initial hardware validation target: Intel Haswell Gen7.5 integrated graphics (Intel HD Graphics 4600 is one concrete target)
- Scope: GPU virtual memory, residency, generation-correct Haswell submission and completion; preserve a path to additional GPU families and vendors.

## 1. Goal and scope

Stage 5 establishes the first Ozero GPU execution substrate. The first hardware backend is Intel Haswell Gen7.5 because it is a concrete target. It is a **bring-up target, not the definition of all GPUs Ozero may support**.

The design must avoid making Intel-specific details mandatory in the generic GPU core. Future Intel generations, AMD, NVIDIA and other devices may be added through separately designed backends after their specifications and hardware contracts are researched. Listing a vendor here does not claim that its hardware is supported or that a universal backend interface is already known.

Stage 5 is not a complete graphics API, desktop compositor, display stack, game compatibility layer or Windows graphics compatibility implementation. Those belong to later stages and depend on this substrate.

## 2. Architectural shape

Keep three boundaries explicit:

1. **GPU-independent Ozero core:** device identity/capability records, resource lifetime, generic GPU virtual-address allocation concepts, memory-object accounting, synchronization primitives, queue/context lifecycle contracts and common error reporting.
2. **Generation/vendor backend:** hardware-specific address-space format, page-table entry encoding, translation invalidation, memory placement/coherency rules, engine discovery, command packet validation, submission registers, interrupts/fences, reset and recovery.
3. **Future graphics services and APIs:** consumers of the GPU core; not part of the Stage 5 hardware bring-up.

This is a proposed boundary, not permission to force every device into one lowest-common-denominator feature set. Capabilities must be reported explicitly. Optional behavior remains optional, and unsupported features must fail clearly rather than be silently emulated.

Ozero's native graphics contracts must be Ozero-defined. Neither a Linux DRM/KMS ABI nor a Windows WDDM interface is the required native architecture. External interfaces and drivers may be studied as references only.

## 3. Initial backend: Intel Haswell Gen7.5

The first backend must be implemented from generation-specific documentation. Do not reuse Gen8+ assumptions or page-table formats.

Intel's Haswell memory-view documentation describes the Gen6–Gen7.5 PPGTT as a two-level scheme: a second-level page directory references first-level page tables; the directory and each page table fit in a 4 KiB page and may reside in non-contiguous physical memory. It also distinguishes per-process virtual memory from privileged/global GGTT mappings. These are backend-specific facts, not generic GPU-core rules. See Reference 1.

Initial target identification should use PCI vendor/device IDs and capability/revision data from the device, not a hard-coded assumption that every Intel integrated GPU is an HD 4600. The HD 4600 is a useful first test device only.

## 4. Required implementation sequence

Follow this order. Do not jump directly to rendering or a high-level API.

### 5.1 GPU discovery and capability description

- Establish or extend the PCI discovery prerequisite needed to locate display/graphics functions.
- Identify the GPU by vendor, device and revision IDs.
- Match only explicitly supported IDs to a backend.
- Record BAR resources, required MMIO ranges, interrupt capability and DMA constraints.
- Expose a capability record without assuming all devices share the same engines, address widths, page sizes or submission model.
- Unknown devices must remain safely unsupported; do not probe undocumented registers.

**Exit evidence:** deterministic discovery tests and a safe unsupported-device path. If PCI enumeration is not yet available in the repository, treat it as a prerequisite task rather than inventing a GPU-only shortcut.

### 5.2 GPU memory objects and address spaces

Define Ozero-owned contracts for:
- GPU memory-object allocation and lifetime;
- physical-page backing and pin/reference ownership;
- GPU virtual-address range allocation and release;
- map, unmap and protection/access attributes;
- residency state and eviction eligibility;
- CPU/GPU visibility and cache-coherency transitions;
- translation invalidation and completion requirements.

Keep CPU virtual addresses, physical addresses and GPU virtual addresses as distinct types and namespaces. Never assume a CPU page-table root can be reused as a GPU page table.

The generic layer should express intent and lifetime. The Haswell backend must encode its actual GGTT/PPGTT structures and constraints.

**Exit evidence:** unit tests for range allocation, alignment, overflow, overlap rejection, mapping lifetime, rollback on partial failure and residency bookkeeping, plus backend-specific page-table construction tests.

### 5.3 Haswell Gen7.5 PPGTT and GGTT

- Implement the documented Haswell-era address-space structure and entry encoding.
- Allocate page-directory and page-table pages with the required physical alignment and lifetime.
- Validate address-width, reserved-bit, permission and physical-address constraints from the generation-specific specification.
- Define GGTT use separately from per-process PPGTT use; do not conflate privileged/global mappings with ordinary per-context mappings.
- Implement the required translation-cache invalidation and ordering before mappings are used or reclaimed.
- Test mapping, unmapping, invalid entries, boundary addresses, non-contiguous page-table backing and teardown.

Do not copy Gen8+ four-level GPU page-table logic into this backend.

**Exit evidence:** specification-traceable page-table tests and, where QEMU's emulated device can exercise the path, an emulator test. A host-side page-table test is not proof of real GPU execution.

### 5.4 Engine-specific command submission and completion

- Enumerate only engines documented and detected for the selected device.
- Define per-engine ring/queue state, command-buffer ownership, alignment and bounds checks.
- Validate command buffer ranges and GPU addresses before submission.
- Implement Haswell-specific submission, interrupt/poll completion and fence sequencing from the applicable programming reference.
- Define timeout, hang detection and reset behavior before allowing untrusted or long-running workloads.
- Keep render, blitter and video engines distinct where the hardware exposes distinct execution contracts.

Do not assume all engines exist on all SKUs, and do not infer a supported command set from strings in a Windows driver binary.

**Exit evidence:** command validation tests, deterministic completion/fence tests, timeout/error-path tests and a hardware/emulator execution test where supported.

### 5.5 Contexts, queues and scheduling

Only after address spaces and single-engine submission are reliable:
- add GPU context ownership and isolation;
- define queue lifecycle and submission serialization;
- implement preemption only if the target generation actually supports the required mechanism and it is documented;
- otherwise use a clearly bounded non-preemptive policy with watchdog/timeout and reset handling;
- prevent one context from freeing memory still referenced by in-flight work.

Do not promise preemption as a generic capability. Report it per backend.

**Exit evidence:** multi-context lifetime tests, queue ordering/fence tests, cancellation/reset tests and explicit documentation of the scheduling guarantees actually implemented.

## 5. Generalization rules for future GPUs

The reusable core should contain policy and resource-lifetime concepts, not vendor register definitions or one generation's page-table layout.

A future backend must independently specify:
- supported device IDs and revisions;
- GPU virtual-address width and page sizes;
- page-table hierarchy and entry format;
- translation invalidation rules;
- memory domains, cache/coherency and DMA constraints;
- engine topology and command format;
- submission/completion mechanism;
- preemption, reset and recovery capabilities.

Do not create empty Intel/AMD/NVIDIA backend directories or claim support before there is a researched specification, implementation and verification evidence. Do not force architectures with materially different memory or execution models through Haswell-specific assumptions.

## 6. Verification policy

Use host tests for pure allocation, mapping bookkeeping, validation and state machines. Use QEMU for boot integration and only those GPU behaviors the selected emulated device genuinely implements. QEMU success does not prove physical Haswell behavior.

Record each result separately:
- compile/build;
- host unit tests;
- QEMU boot;
- emulated GPU feature test;
- physical GPU test.

Do not claim a GPU feature is implemented merely because the kernel boots or because a driver binary contains a related symbol. Physical hardware testing may be deferred until the final hardware-validation phase; it is not a reason to block early architecture and host-test work.

## 7. Explicit non-goals and open questions

- No complete desktop/display compositor or graphics API in Stage 5.
- No Windows WDDM driver port and no Linux DRM dependency.
- No assumption that Intel HD 4600 represents every Haswell SKU, much less every GPU.
- No universal page-table format, command stream, engine model or preemption contract.
- Confirm exact Haswell device IDs and platform stepping from the target machine before hardware-specific testing.
- Confirm which Haswell engine submission and reset details are covered by the available public programming reference; record undocumented or ambiguous behavior as unresolved rather than guessing.
- Decide the minimal PCI enumeration/resource interface needed by the GPU backend based on the current repository state.

## 8. References

1. Intel, **Haswell Graphics PRM — Memory Views** (public Intel PDF): https://cdrdv2-public.intel.com/690966/intel-gfx-prm-osrc-hsw-memory-views.pdf. Primary source for Haswell memory views and the Gen6–Gen7.5 PPGTT description.
2. Intel, **Intel 64 and IA-32 Architectures Software Developer Manuals**: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html. CPU/platform architecture reference; it does not replace the graphics programming reference.
3. Intel, **Technical Library**: https://www.intel.com/content/www/us/en/developer/technical-library/overview.html. Use to locate the relevant generation-specific graphics programming reference volumes and revisions.

The Intel Windows driver files supplied during this discussion may be retained as local research artifacts, subject to their licenses. They are not authoritative specifications, must not be copied into the kernel, and do not establish a hardware contract. Strings or symbols found in driver binaries are clues for investigation, not proof of register behavior or supported features.
