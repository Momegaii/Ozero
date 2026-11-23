# IF-008: A3 DMA and platform interface

| Field | Value |
|---|---|
| Owner | A3 |
| Version | 1.1 |
| Status | Published; A1 **ACCEPT** at `b9a69d8` (`85df005`, review head `74b1471`); A3 accepted A1 conditions at `307b919`; Director D-007 pending |
| Consumers | A1, A2, A4 |
| Since | Stage 0 / Stage 1 |
| Related | D-007, D-009, D-010, D-020, D-021; XB-001, XB-005, XB-007, XB-008, XB-011 |

## Purpose

A3 block drivers need discovery, MMIO, interrupts, DMA buffers, device reset and
asynchronous I/O from A1. This interface does not create a second platform ABI.
It names the exact A1 Stage 1 surface that A3 consumes, and records the limits
A3 accepts until an IOMMU exists.

A1's signed headers are authoritative:

- `include/ozero/mm.h` - DMA constraints, buffers, allocation, sync and free.
- `include/ozero/driver.h` - device discovery, BAR mapping, IRQ, bus mastering,
  I/O limits, quiesce, reset, generic asynchronous I/O and the device callback
  table.
- `include/ozero/irq.h` - GSI/vector allocation, handler registration and masking.

A1's latest written conditions are in `docs/a3-dma-contract.md` on
`a1/stage0-review` at `30ee4f0`. A1 has signed IF-008 v1.1 at `b9a69d8`: it records
final approval only after A3 publishes `platform.h` and `block.h`. This version
is A3's publication for that review.

## Producer obligations

A1 provides these guarantees:

| Area | A1 surface | Guarantee |
|---|---|---|
| Discovery | `o0_device_enumerate` | Bus-neutral `o0_device_id`; A3 classifies the device |
| MMIO | `o0_device_map_bar`, `o0_device_unmap_bar` | Dedicated `O0_MAP_UC`, non-executable mapping; never served from the HHDM |
| IRQ | `o0_device_irq`, `o0_irq_register`, `o0_irq_mask`, `o0_irq_unmask` | GSI and vector are distinct; handler returns whether the line was ours |
| DMA constraints | `o0_device_dma_constraints` | Dispatches to A3's `dma_constraints`; A1 never infers them from a device class |
| Bus mastering | `o0_device_bus_master_enable` | Explicit enable/disable; no implicit DMA |
| I/O limits | `o0_device_io_limits` | Transfer size, queue depth and alignment limits for the generic request |
| Quiesce | `o0_device_dma_quiesce` | Dispatches to A3's `dma_quiesce`; stops new DMA and waits for in-flight requests |
| Reset | `o0_device_reset` | Dispatches to A3's `reset`; precondition is quiesced and no outstanding request |
| Generic I/O | `o0_io_submit`, `o0_io_cancel` | Dispatches to A3's `io_submit` / `io_cancel`; completion must not run before submit returns |
| Callback binding | `o0_device_register_ops` | Binds A3's `struct o0_device_ops` for the device lifetime |
| DMA allocation | `o0_dma_alloc`, `o0_dma_sync`, `o0_dma_free` | Constraint-driven allocation; physical addresses; explicit cache maintenance; one physically contiguous segment per buffer |

Stage 1 has no IOMMU domain. `o0_dma_buffer.phys` is an untranslated physical
address. There is no `o0_dma_domain`, no `dma_map_sg`, no `iommu_map`, no
`dma_domain_t`, no `iommu_domain_t`, no `iova_t` and no `dma_addr_t` in the
signed surface.

`o0_dma_alloc` returns `O0_ENOTSUP` when `size > max_segment`. The caller must
split the transfer. `o0_dma_free` requires the device to be quiesced and does
not touch device state.

## Consumer obligations

A3 must:

1. Include A1 headers as `<ozero/types.h>`, `<ozero/status.h>`, `<ozero/irq.h>`,
   `<ozero/mm.h>` and `<ozero/driver.h>`. Do not redeclare `o0_status`,
   `o0_paddr_t`, `o0_vaddr_t`, `o0_dma_buffer`, `o0_dma_constraint`,
   `o0_device_t`, `o0_io_request` or `o0_io_limits`.
2. Treat `o0_dma_buffer.phys` as a physical address. Do not program an IOMMU.
3. Split a transfer larger than `o0_dma_constraint.max_segment` into multiple
   `o0_dma_buffer` objects and multiple `o0_io_request` objects. There is no
   scatter-gather pointer in `o0_io_request` in Stage 1. A protocol driver may
   coalesce requests into its hardware SG list internally, but each generic
   request carries one contiguous buffer.
4. Implement `struct o0_device_ops` and register it with
   `o0_device_register_ops`. A NULL callback means unsupported and makes the A1
   wrapper return `O0_ENOTSUP`.
5. Use `o0_dma_sync` around non-coherent transfers, before and after device
   access as appropriate.
6. Call `o0_device_dma_quiesce` before `o0_dma_free`, `o0_device_unmap_bar`,
   `o0_device_reset` or driver unload.
7. Call `o0_device_reset` only after quiesce and after requests have completed
   or been cancelled. Reset does not free A1 DMA buffers or unmap BARs; A3
   reinitialises the device and re-arms the queue.
8. Use `o0_device_map_bar` for BARs. Do not derive BAR addresses from the HHDM.
9. Accept only trusted ring-0 drivers in Stage 1. Do not expose DMA to userspace
   or an untrusted driver before an IOMMU domain exists.
10. Treat any semantic change as a versioned interface change, not a
    driver-local workaround.

A3's block-layer reset callback is `o0_status (*reset)(struct o0_block_dev *)`.
It resets A3 protocol state only; it does not replace A1's
`o0_device_dma_quiesce` + `o0_device_reset` sequence.

## Data layout

### `struct o0_dma_constraint`

| Field | Type | Meaning |
|---|---|---|
| `addr_mask` | `uint64_t` | Every address must satisfy `(addr & ~addr_mask) == 0`; contiguous low-bit mask |
| `alignment` | `uint32_t` | Power of two, or 0 for natural page alignment |
| `max_segment` | `uint32_t` | Bytes per physically contiguous segment; 0 means no limit |
| `coherent` | `bool` | False requires `o0_dma_sync` |
| `reserved` | `uint32_t` | Must be zero |

### `struct o0_dma_buffer`

| Field | Type | Meaning |
|---|---|---|
| `phys` | `o0_paddr_t` | Physical, untranslated address |
| `virt` | `void *` | CPU virtual address |
| `size` | `size_t` | Rounded allocation size in bytes |
| `flags` | `uint32_t` | A1-defined allocation flags |
| `reserved` | `uint32_t` | Must be zero |

### `struct o0_io_limits`

| Field | Type | Meaning |
|---|---|---|
| `max_transfer` | `uint32_t` | Bytes per request; 0 means unlimited |
| `max_queue_depth` | `uint16_t` | Maximum generic requests |
| `alignment` | `uint16_t` | Power of two, or 0 |
| `flags` | `uint32_t` | A1-defined capability flags |

### `struct o0_io_request`

| Field | Type | Meaning |
|---|---|---|
| `op` | `enum o0_io_op` | Read, write, flush or control |
| `offset` | `uint64_t` | Byte offset |
| `buffer` | `void *` | One contiguous CPU buffer |
| `length` | `size_t` | Byte length |
| `flags` | `uint32_t` | Request flags |
| `status` | `o0_status` | Completion status |
| `complete` | `o0_io_complete_fn` | The only A1 completion callback |
| `ctx` | `void *` | Callback context |
| `driver_private[2]` | `void *` | Driver-owned fields |

### `struct o0_device_ops`

A3 implements exactly this table. The table must stay valid for the device
lifetime.

| Callback | A3 behaviour | Failure |
|---|---|---|
| `dma_constraints` | Fill `addr_mask`, `alignment`, `max_segment` and `coherent` from the device; `addr_mask` is a contiguous low-bit mask | `O0_EINVAL` for invalid mask or alignment |
| `dma_quiesce` | Stop new DMA, wait for in-flight requests, stay idempotent, do not free buffers | `O0_ETIMEDOUT` if it cannot stop |
| `reset` | Reset the device after quiesce; do not free A1 buffers or unmap BARs; reinitialise afterwards | `O0_EIO` or `O0_ETIMEDOUT` |
| `io_submit` | Enqueue one contiguous request; completion must run after return; do not touch `req` after completion | `O0_EINVAL`, `O0_EBUSY`, `O0_EIO` |
| `io_cancel` | Best-effort cancel; `O0_EBUSY` means the request is already completing | `O0_EBUSY`, `O0_ENOENT` |

### Block-layer view

`struct o0_bio` embeds `struct o0_io_request io`. `io.complete` is the only A1
completion callback; the protocol driver's trampoline calls the block-layer
`end_io`. `struct o0_bio_vec` describes logical pages, not hardware SG entries.
A large bio is split into multiple `o0_dma_buffer` objects and multiple generic
requests. The protocol driver may build its hardware SG list internally, but
the generic request has no SG pointer.

## Error behaviour

A1 status values are positive enum values, not negative errno values:

`O0_OK`, `O0_EINVAL`, `O0_ENOMEM`, `O0_ENOENT`, `O0_EBUSY`, `O0_EPERM`,
`O0_ENOSYS`, `O0_EFAULT`, `O0_EIO`, `O0_ERANGE`, `O0_ETIMEDOUT`,
`O0_EAGAIN`, `O0_EOVERFLOW`, `O0_ENOTSUP`.

A3 maps these to block status:

| A1 status | Block status |
|---|---|
| `O0_OK` | `O0_BLOCK_OK` |
| `O0_EINVAL`, `O0_ERANGE`, `O0_EOVERFLOW` | `O0_BLOCK_ERR_INVAL` |
| `O0_EIO`, `O0_EFAULT` | `O0_BLOCK_ERR_IO` |
| `O0_ETIMEDOUT` | `O0_BLOCK_ERR_TIMEOUT` |
| `O0_EBUSY`, `O0_EAGAIN` | `O0_BLOCK_ERR_AGAIN` |
| `O0_ENOTSUP`, `O0_ENOSYS` | `O0_BLOCK_ERR_NOTSUP` |
| other | `O0_BLOCK_ERR_TRANSPORT` |

An IOMMU fault does not exist in Stage 1 because there is no IOMMU domain.
A device that DMA-writes outside its buffers is a trusted-driver bug or
hardware failure, not an isolated fault.

## Compatibility

Version 1.1 is the Stage 1 trusted ring-0 interface. It is additive only.
Adding an IOMMU domain handle, an SG pointer in `o0_io_request`, or a reset-kind
argument is a breaking version change and requires a new IF-008 version plus A1
and director approval.

Open cross-agent dependencies:

- **D-009 / XB-007:** **Approved; XB-007 closed** in A4 `b4232a0`.
  `include/ozero/` is the only public root; A1 `03f8883` implements it; A3 is
  aligned and claims no remaining namespace conflict.
- **D-010 / XB-008:** **Approved; XB-008 closed** in A4 `b4232a0`. A1 owns
  `tools/check-headers.sh` and `tests/abi_static_asserts.c`; A4 owns CI
  integration.
- **D-020:** DMA is constraint-driven, never device-class-driven. A3 supplies
  `o0_dma_constraint`; A1 does not infer it.
- **D-021:** no IOMMU in Stage 1; trusted ring-0 drivers only. Director approval
  is pending.
- **D-007 / XB-001:** A1 **ACCEPT**ed this exact version at `b9a69d8`
  (`85df005`, review head `74b1471`); Director approval of D-007 and D-021 is
  still required before the AHCI probe. XB-001 is closed in A4 `b4232a0`.
- **XB-011:** `origin/a3/stage0-review-response` is pushed. A4 `b4232a0`
  records XB-011 closed, but the A4 candidate lacks `a3/` and root
  `include/ozero/`; A3 PR #4 is open into the A4 candidate.

## Version history

| Version | Change | Compatibility | Date |
|---|---|---|---|
| 1.0 | Reconcile A3 `platform.h`/`block.h` with A1 `mm.h`/`driver.h`; remove DMA-domain, SG-map and reset-kind assumptions; record trusted ring-0 restriction | Initial reconciled version | 2026-11-23 |
| 1.1 | Align to A1 `30ee4f0`: `include/ozero/` public root, direct inclusion of A1 headers, `struct o0_device_ops`, one contiguous buffer per `o0_io_request`, no SG pointer, reset does not free buffers or unmap BARs, five A1 conditions | Additive over 1.0; no A1 type redeclared | 2026-11-23 |

## References

- A1 review conditions:
  <https://github.com/Momegaii/Ozero/blob/a1/stage0-review/docs/a3-dma-contract.md>
- A1 signed DMA surface:
  <https://github.com/Momegaii/Ozero/blob/a1/stage0-review/include/ozero/mm.h>
- A1 signed driver surface:
  <https://github.com/Momegaii/Ozero/blob/a1/stage0-review/include/ozero/driver.h>
- A1 signed interrupt surface:
  <https://github.com/Momegaii/Ozero/blob/a1/stage0-review/include/ozero/irq.h>
- A1 global decision proposals:
  <https://github.com/Momegaii/Ozero/blob/a1/stage0-review/docs/decisions/a1-global-decisions.md>
- A4 global decision register:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/decisions/decision-log.md>
- A4 cross-agent blocker register:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/process/cross-agent-blockers.md>
- Intel, *Serial ATA AHCI 1.3.1*:
  <https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf>
