# ADR 0016 — Modern virtio PCI transport for block storage

- **Status:** Proposed for implementation. Implementation is written and host-tested;
  no modern-transport CI cell has been observed green yet, so this record does **not**
  approve the transport. Promote to *Accepted-provisional* only against a recorded CI run.
- **Scope:** Wave 4.2. Add the modern virtio PCI transport for the existing block driver
  without changing the block-layer contract or Ozero's native ABI.
- **Date:** 2026-11-26

## Context

The legacy virtio-blk path is implemented against the legacy I/O-port register interface
and has its own queue layout and status handshake. Modern virtio PCI devices expose
transport registers through PCI vendor-specific capabilities and memory BARs. Treating a
modern device as legacy is unsafe; the legacy driver correctly refuses to drive one.

Linux DRM, the virtio specification and other operating systems are used here as device
contracts and references, not as Ozero architecture. The transport is written against the
virtio 1.1 register and split-ring definitions; Ozero's own block, DMA, PCI and paging
interfaces remain the only internal dependencies.

## Decision

1. **Two backends, one block contract.** The legacy I/O-port transport stays in
   `kernel/src/virtio_blk.c`. The modern transport is `kernel/src/virtio_blk_modern.c`.
   `o0_virtio_blk_probe()` tries legacy first and falls back to modern only when no legacy
   function is usable, so a legacy device is never driven through MMIO and a modern-only
   device is never driven through I/O ports.
2. **Registers through an interface, not a pointer.** `struct o0_virtio_regs` names a
   bounds-checked register interface; MMIO is the production implementation
   (`o0_virtio_mmio_reg_iface`). Host tests substitute a device model with real semantics
   (two feature banks, status latching, per-queue state, fixed register widths) so the
   protocol logic is tested against a device, not a byte array.
3. **Mapping uses implemented services only.** `o0_virtio_pci_map_caps()` probes BAR
   metadata with `o0_pci_probe_bar()` and maps each used BAR once with
   `o0_as_map(O0_MAP_UC|O0_MAP_NX)` into a per-device window. It deliberately does **not**
   use the `o0_device_map_bar()` family from `include/ozero/driver.h`: that service layer
   is declared but not implemented, and inventing it as a side effect of this wave would
   create an unapproved interface. `include/ozero/driver.h` remains a separate, unstarted
   work item.
4. **Feature negotiation fails closed.** The driver must accept `VIRTIO_F_VERSION_1`;
   asking to accept anything else is a programming error. The device offer is read from
   both banks through `device_feature_select`, the accepted set must be a subset of the
   offer, driver features are programmed per bank, and `FEATURES_OK` is re-read before it
   is trusted. A device that clears `FEATURES_OK` is reported as unsupported and the
   caller must reset it.
5. **Queue configuration is ordered and validated.** Select the queue, read
   `num_queues` and the device maximum, choose a power-of-two size within both bounds,
   refuse to reconfigure an enabled queue, validate the descriptor (16-byte), available
   (2-byte) and used (4-byte) addresses for alignment, device DMA mask and wrap, program
   the three 64-bit addresses, issue a write barrier, then enable and read back
   `queue_enable`. Legacy PFN semantics are never assumed.
6. **Notification arithmetic is overflow-safe and bounds-checked.**
   `offset = queue_notify_off * notify_off_multiplier` is computed in 64 bits, the whole
   16-bit write must lie inside the mapped notification region, and the write is issued
   after a release barrier.
7. **Completions are validated.** A completion is accepted only when the used-ring entry
   at the driver's consume position names the chain head that was submitted. An id
   mismatch, a used index that jumps past the queue size, and a timeout are all reported.
   Descriptor ownership is tracked by an explicit free list, so an in-flight chain is
   never reissued.
8. **Quiesce before reuse.** A timeout or malformed completion resets the device
   (`device_status = 0`, polled until it reads 0), renegotiates features, re-programs the
   queue, re-zeroes the rings and only then continues. Ring and request buffers are pinned
   for the device lifetime and are only freed after a successful reset in teardown, so no
   path frees memory the device may still be writing.
9. **DMA is honest about having no IOMMU.** Queue memory is one physically contiguous
   pinned buffer allocated with a 44-bit address mask; data buffers are the block layer's
   bounce buffer and are handed over only if their physical range satisfies the same mask.
   A buffer the device cannot address is refused, never silently translated.
10. **Interrupts are optional.** An MSI vector is programmed when the function offers MSI
    and is re-armed after a reset, but completion is a bounded poll. A missing or
    misrouted interrupt therefore shows up as a documented timeout, not a hang.

## Consequences

- The block layer sees one extra device; nothing in the block contract changed.
- The transport is testable on the host with no hardware, because the protocol logic sits
  above a register interface. `tests/virtio_queue_test.c` exercises queue configuration,
  notification, submission, completion validation, overrun, invalid ids, timeout paths and
  the reset/quiesce bookkeeping; `tests/virtio_pci_mmio_test.c` exercises feature banks,
  `FEATURES_OK` rejection, reset timeouts and region bounds.
- A modern-only QEMU cell is required as the real-device evidence:
  `virtio-blk-pci,disable-legacy=on`. It must print `OZERO: virtio-blk modern ready
  queue=`, the read/write marker and the injected-error marker.
- No physical device is claimed. No performance statement is made.

## Alternatives considered

- **Reuse the legacy I/O-port path for modern devices:** rejected. Modern devices use
  capability-described MMIO and 64-bit queue addresses; legacy offsets and PFN
  programming are not equivalent.
- **Add virtio-specific BAR mapping to the PCI core:** rejected. Mapping is composed from
  the generic `o0_pci_probe_bar` and `o0_as_map` services at the driver boundary.
- **Implement `o0_device_*` first:** rejected for this wave. It is a larger platform
  service with its own lifecycle questions (device registry, ops binding, async I/O,
  quiesce dispatch) and deserves its own ADR rather than being invented here.
- **A separate modern block driver:** rejected. Transport selection under one block
  device keeps the block semantics in one place.

## Remaining limitations

- One queue, depth 1, synchronous polling. No descriptor batching, no indirect
  descriptors, no event suppression (`VIRTIO_F_RING_EVENT_IDX` is not negotiated).
- No MSI-X per-queue vector; the queue `msix_vector` field is left at the device default.
- No IOMMU and no bounce buffer for data, so a caller whose bounce buffer falls outside the
  device mask gets a refusal instead of a transfer.
- `VIRTIO_BLK_F_BLK_SIZE` is not negotiated; 512-byte sectors are assumed, matching the
  legacy driver.
- Transitional devices (device ID `0x1001`) are driven by the legacy transport. Only
  device ID `0x1042` selects the modern backend.
- Evidence is emulator-only.

## Exit evidence

Recorded in `docs/handoff.md`: host test commands and results, the CI run identifiers for
the `storage (virtio)` and `storage (modern-virtio)` cells, and the fact that no physical
hardware was validated.
