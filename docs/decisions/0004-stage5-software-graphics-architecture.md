# Stage 5 software architecture decision: vendor-neutral graphics core

- Date: 2026-11-24
- Scope: Stage 5 software architecture and host/emulator verification only
- Status: proposed implementation boundary for Project Director acceptance
- Supersedes: any interpretation of the Stage 5 checklist that makes physical GPU installation or hardware execution a prerequisite for closing this software milestone

## Decision

Ozero will expose one Ozero-defined, GPU-independent graphics substrate and attach hardware-specific backends through explicit capability and engine-operation contracts. Stage 5's software architecture milestone can be completed without installing a GPU driver, programming device MMIO registers, or verifying command execution on a physical adapter.

This decision does not claim that Ozero currently supports every graphics adapter. A device is supported only when a separately researched backend explicitly matches it and implements the operations it advertises. Unknown devices remain discoverable but unsupported. A generic framebuffer/display fallback is a separate display-output path; it is not a GPU-acceleration backend.

## Existing implementation boundary

The current branch already contains the main software seams:

| Layer | Existing contract | Responsibility |
|---|---|---|
| Device discovery | `include/ozero/gpu_device.h`, `kernel/src/gpu_device.c` | PCI display classification, validated BAR metadata, backend matching and capability reporting |
| Generic memory/VM | `include/ozero/gpu.h`, `kernel/src/gpu.c` | Ozero-owned memory objects, GPU VA ranges, mappings, lifetime and residency-facing API |
| Generic scheduling | `include/ozero/gpu_queue.h`, `kernel/src/gpu_queue.c` | Contexts, queues, command ownership, sequence numbers, fences, bounded waits, cancellation and quarantine |
| Backend boundary | `struct o0_gpu_backend_ops`, `struct o0_gpu_engine_ops` | Device matching/capabilities and opaque validate/submit/poll/retire/cancel/reset operations |
| First backend implementation | `include/ozero/gpu_hsw.h`, `kernel/src/gpu_hsw.c` | Haswell Gen7.5-specific page-table encoding, command validation and software ring/submission model |

The generic layers must not include or depend on Haswell register offsets, page-table entry formats, engine names, ring layout, or vendor command encodings. Those details stay in the backend. The backend must not redefine generic resource ownership or fence semantics.

## Required contracts

### Device and capability contract

- Discovery records identity and validated resource metadata without requiring that a backend claim the device.
- Backend matching is explicit and conservative. No wildcard vendor match and no inferred support.
- Capabilities describe what that backend actually implements. Unknown or unimplemented capabilities remain false/zero.
- Unsupported operations return a defined Ozero error, normally `O0_ENOTSUP`; they are never silently emulated.
- `submission_supported` means the backend has a complete implementation of submission and completion semantics. It must not be set merely because command encoding or a software queue model exists.
- `submission_hardware_verified` is evidence metadata, not a prerequisite for the software-only milestone. It remains false until actual device execution is deliberately tested.

### Memory and address-space contract

- GPU virtual addresses belong to a GPU address-space namespace and must never be treated as CPU virtual or physical addresses.
- Generic objects own backing-page references; mappings own references to objects; in-flight work pins every resource the device may still access.
- Unmap, free, eviction and destruction must refuse or defer while mappings or in-flight work retain ownership.
- Residency, visibility/coherency, permissions and eviction are explicit states/operations. If a backend cannot implement an operation, it returns unsupported rather than pretending the operation succeeded.
- Backend page sizes, VA widths, alignment and physical-address limits are supplied as capabilities and validated before backend-specific encoding.

### Queue, submission and synchronization contract

- A context owns its address space and attached queues. A queue binds one context to one opaque engine instance.
- Sequence numbers are monotonic and fences have terminal states with explicit errors.
- All waits are bounded. An unbounded wait is invalid.
- Command validation occurs before submission.
- Resource retirement and execution completion are distinct facts. A read-pointer advance may prove the device stopped fetching a buffer; it does not prove that commands completed.
- Only backend-provided completion evidence may signal a fence.
- Timeout, cancellation, or uncertain device ownership quarantines the queue/engine. Resources remain pinned until the backend confirms consumption.
- Recovery is allowed only when a backend implements a documented reset contract; otherwise recovery returns unsupported and quarantine remains.

## Backend onboarding contract

A future backend must be added behind the existing interfaces, not by adding vendor conditionals to generic code. Each backend contribution must provide:

1. A researched device/generation match policy and an explicit supported-device list.
2. A capability record with conservative defaults.
3. Backend-owned memory/address translation rules and validation.
4. Engine-specific command validation and submission implementation.
5. Completion and retirement evidence with distinct semantics.
6. Bounded timeout, cancellation, reset and recovery behavior; unsupported paths must fail closed.
7. Host tests for pure encoding, bounds, state transitions, invalid inputs, resource lifetime and failure paths.
8. A backend conformance test using fake/injected engine operations to exercise the generic queue contract.
9. Documentation that distinguishes specification facts, derived assumptions, unknowns and tested behavior.

Vendor order is not a support promise. Intel, AMD, NVIDIA and virtual adapters are independent future backend projects. No vendor is considered supported merely because its name appears in documentation.

## Software-only Stage 5 acceptance

The software architecture portion is complete when all of the following hold:

- [ ] Generic device, memory/VM and queue/fence APIs have no vendor-specific types or register assumptions in their public contracts.
- [ ] A fake backend can be injected into the generic queue layer and can pass the backend conformance tests without hardware.
- [ ] Generic tests cover unsupported devices, capability defaults, mapping/lifetime rules, queue isolation, invalid command rejection, fence ordering, bounded timeout, cancellation, retirement versus completion, quarantine and refused recovery.
- [ ] The Haswell backend's pure encoding and validation tests remain isolated from generic tests.
- [ ] Host test suites and QEMU boot/self-tests pass, with exact commands and output recorded.
- [ ] Documentation accurately labels what is implemented, host-tested, QEMU-booted, emulated, and hardware-verified.
- [ ] No physical GPU installation, MMIO programming, or hardware execution is required for this software-only acceptance.

A passing host test or QEMU boot proves only the tested software path in that environment. It does not prove GPU command execution, real device support, or universal adapter compatibility.

## Explicitly out of scope for this closeout

- Installing or enabling any vendor GPU driver.
- Writing GPU MMIO registers or enabling a physical engine.
- Physical GPU execution, interrupts, reset, preemption or hardware recovery.
- Claiming Intel, AMD, NVIDIA or virtual-GPU acceleration support without a separately completed backend.
- A complete graphics API, compositor, desktop, display stack or Windows graphics compatibility layer.

These remain separate future milestones. A framebuffer path may be developed independently for basic display output, but it must not be described as accelerated GPU support.

## Documentation reconciliation required

The Stage 5 plan and status documents contain historical Haswell bring-up tasks and some stale statements about device matching. Before calling the whole Stage 5 milestone closed, reconcile those documents with this decision and audit the current source so that:

- header comments match the actual Haswell match table and the current discovery behavior;
- the generic capability flags are not overstated;
- the software-only exit criteria are clearly separated from optional physical bring-up;
- no checklist item is marked complete without corresponding tests or source evidence.

This decision changes the acceptance boundary, not the evidence. Existing test claims must still be verified from their recorded runs; no new test pass is asserted by this document.
