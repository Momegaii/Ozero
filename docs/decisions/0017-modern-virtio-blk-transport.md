# ADR 0017: Modern virtio-blk PCI transport (MMIO capability discovery)

- **Status:** Accepted
- **Date:** 2026-11-26
- **Wave:** 4.2 (modern virtio transport)
- **Related:** `kernel/src/virtio_blk_modern.c`, `kernel/src/virtio_blk.c`,
  `include/ozero/virtio_blk.h`, `tests/virtio_blk_modern_test.c`

## 1. Context

Ozero's block layer (Wave 4.0/4.1) registers `o0_block_dev` devices with one
in-flight request and bounded-spin completion. Wave 4.2 adds virtio-blk. The
legacy transport (I/O BAR0, PFN queue programming) already exists in
`kernel/src/virtio_blk.c`. QEMU's `virtio-blk-pci` defaults to the modern
transport (capability-based MMIO regions), and `-device virtio-blk-pci`
without `disable-modern=on` exposes device ID `0x1042` rather than the legacy
`0x1001`. A modern-transport driver is needed so CI cells that omit
`disable-modern=on` can use virtio-blk.

Ozero defines its own native execution architecture and does not default to
Linux conventions. No ELF, ELF64, Linux syscall, glibc or Linux userspace
assumption appears in this code. The Limine/ELF boot image is a build/detail
artifact and does not influence the driver design.

## 2. Alternatives

| Alternative | Trade-off |
|---|---|
| **Modern first, legacy fallback (chosen)** | Probe tries modern (MMIO cap ID 0x09) first, then falls back to legacy (device ID 0x1001). Covers both QEMU defaults. One probe entry point (`o0_virtio_blk_probe`) with transport selection inside. |
| Modern only | Rejects legacy `0x1001` devices. Existing CI cells using `disable-modern=on` would break, and the legacy code path would be dead. |
| Legacy only | The current pre-Wave-4.2 state. Cannot handle `0x1042` devices, so CI cells without `disable-modern=on` get `O0_ENOTSUP`. |
| Two separate probe entry points, no fallback | Callers must know the device class ahead of time, leaking transport details into the block layer and `main.c`. |

## 3. Decision

1. **`kernel/src/virtio_blk_modern.c`** implements the modern transport:
   - PCI config-space snapshot read once, then `vblk_modern_walk_caps()` walks
     vendor-specific capability ID `0x09` to find the common, ISR, device and
     notify config regions. The walk is pure (takes `const uint8_t *cfg`) and
     is unit-tested on the host.
   - The BAR is mapped uncached (`O0_MAP_READ | O0_MAP_WRITE | O0_MAP_UC |
     O0_MAP_NX`) below NVMe's window at `0xffffc00040000000`, with an optional
     second window at `0xffffc00050000000` for the MSI-X table.
   - Feature handshake: `VIRTIO_BLK_F_FLUSH` is accepted when offered. Queue 0
     is programmed through MMIO (descriptor/avail/used physical addresses,
     queue enable). MSI-X entry 0 is programmed when available; the driver also
     polls the used ring, so a missing interrupt surfaces as a timeout.
   - One virtqueue, one request in flight. Descriptor chain: 16-byte request
     header (type, reserved, sector), optional data buffer, 1-byte status.
   - `o0_virtio_blk_modern_probe`, `o0_virtio_blk_modern_irq_count`,
     `o0_virtio_blk_modern_inject_error` are exported for diagnostics/tests.

2. **`kernel/src/virtio_blk.c`**: `o0_virtio_blk_probe` now tries modern first,
   then falls back to legacy device ID `0x1001`. `o0_virtio_blk_irq_count` and
   `o0_virtio_blk_inject_error` check legacy first, then modern.

3. **`include/ozero/virtio_blk.h`**: documents both transports; declares the
   modern entry points alongside the existing ones.

4. **Makefile**: adds `virtio_blk_modern.o` to the kernel build.

5. **CI**: `.github/workflows/quality.yml` removes `disable-modern=on` from the
   virtio cell so it exercises the modern transport.

## 4. Consequences

- The block layer is unchanged: modern virtio registers a real
  `o0_block_dev` with `nr_queues = 1`, `queue_depth = 1`, `max_segments = 1`.
  DMA is coherent; the driver pins its ring and request header for the device
  lifetime.
- MSI-X is opportunistic: when it cannot be allocated or programmed, the
  driver continues without interrupts and relies on used-ring polling.
- No physical controller is claimed; QEMU evidence only.
- Windows compatibility is unaffected: this is Ozero-native virtio, not a
  Windows driver model.
- Kernel compiles with `-Werror` on the build host.

## 5. Test plan

Host (`make test`, no QEMU):

- `tests/virtio_blk_modern_test.c`: `vblk_modern_walk_caps` against
  constructed PCI config-space images: valid four-cap layout, missing
  capability list, incomplete region set, self-referential next pointer, wrong
  config type skipped. Compiled under `-fsanitize=address,undefined` with leak
  detection under `make test-sanitize`.

Physical/CI: the modern transport is exercised by the QEMU virtio cell in
`.github/workflows/quality.yml` (storage, virtio, smp=2), which now omits
`disable-modern=on`. Markers: `virtio-blk-modern ready ...` followed by the
existing read/write and injected-error markers.

## 6. Open questions

- MSI-X multi-vector support: only entry 0 is programmed; entry 1 for a
  separate completion vector is deferred. The driver polls, so this does not
  affect correctness.
- Multiple virtqueues (queue > 0): single-queue only; multi-queue is deferred.
- Physical virtio controllers are not claimed and will not be claimed without
  Director-observed runs.
