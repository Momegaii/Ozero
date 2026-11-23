# A3 DMA and platform interface: A1 approval conditions

| Field | Value |
|---|---|
| Interface | IF-008, DMA and platform contract |
| Global decisions | D-007 (interface), D-020 (constraint model), D-021 (IOMMU policy) |
| Owner | A3 |
| A1 role | Required sign-off gate before dependent implementation |
| Version reviewed | 0.1 draft, A1 conditions issued 23 November 2026 |
| Status | **Conditional. A3's `platform.h` and `block.h` are not published, so final sign-off is pending.** |

A3 has a push-permission problem. Its `platform.h` and `block.h` are not on any shared
branch, so A1 cannot inspect the actual headers. This document fixes the A1 side of the
contract now. A3 must reconcile its headers to it, publish them, and request the final
IF-008 sign-off. No AHCI probe or real DMA work starts before that sign-off.

## 1. Stage 1 restriction

- There is no IOMMU in Stage 1. Limine disables VT-d and AMD-Vi. DMA addresses are
  untranslated physical addresses.
- Only trusted ring-0 drivers may use DMA. Userspace and untrusted drivers are out of scope
  until an IOMMU interface exists under a new global decision.
- A3 must not introduce a DMA domain, an IOMMU domain, an IOVA allocator or a translated
  address type in Stage 1. Those are future interfaces, not hidden fields.

## 2. Ownership split

| Area | A1 provides | A3 implements or consumes |
|---|---|---|
| Device discovery | `o0_device_enumerate` | Match and probe the storage device |
| BAR access | `o0_device_map_bar`, `o0_device_unmap_bar` | Use the UC/NX mapping; never the HHDM |
| Interrupts | `o0_device_irq`, `o0_irq_register` | Register the handler and acknowledge the device |
| DMA constraints | `o0_device_dma_constraints` wrapper | `dma_constraints` callback with device limits |
| DMA memory | `o0_dma_alloc`, `o0_dma_sync`, `o0_dma_free` | Supply constraints, use `virt` and `phys`, sync |
| Bus mastering | `o0_device_bus_master_enable` | Enable after the device is configured |
| Quiesce | `o0_device_dma_quiesce` wrapper | `dma_quiesce` callback that stops the device |
| Reset | `o0_device_reset` wrapper | `reset` callback; reinitialise afterwards |
| I/O requests | `o0_io_submit`, `o0_io_cancel` wrappers, `struct o0_io_request` | `io_submit` and `io_cancel` callbacks |
| Limits | `o0_device_io_limits` | Validate block size and queue model |

## 3. A1 API A3 must consume

These declarations are in `include/ozero/mm.h`, `driver.h` and `irq.h`.

| Function | Semantics A3 depends on |
|---|---|
| `o0_device_enumerate(fn, ctx)` | Reports bus-neutral device identity. A3 classifies devices. |
| `o0_device_map_bar(dev, bar, &len, &virt)` | Returns a dedicated UC, NX mapping. Never served from the HHDM. Valid until the matching unmap. |
| `o0_device_unmap_bar(dev, virt, len)` | Requires the device to be quiesced. |
| `o0_device_irq(dev, &gsi, &vector)` | Returns the GSI and, where supported, an MSI/MSI-X vector. |
| `o0_device_dma_constraints(dev, &c)` | Dispatches to A3's `dma_constraints`. Must be called before the first allocation. |
| `o0_device_bus_master_enable(dev, enable)` | Enables or disables bus mastering. |
| `o0_dma_alloc(&c, size, &buf)` | One physically contiguous segment. Returns `O0_ENOTSUP` when `size > max_segment`. `size` is rounded to pages. |
| `o0_dma_sync(&buf, off, len, dir)` | Required around non-coherent transfers. No-op for coherent buffers. |
| `o0_dma_free(&buf)` | Caller must quiesce first. Does not touch device state. |
| `o0_device_dma_quiesce(dev)` | Dispatches to A3's `dma_quiesce`. Returns only when no DMA is in flight or a timeout occurs. |
| `o0_device_reset(dev)` | Dispatches to A3's `reset`. Precondition: quiesced and no request outstanding. |
| `o0_device_io_limits(dev, &limits)` | Maximum transfer, queue depth and alignment. |
| `o0_io_submit(dev, req)` | Dispatches to A3's `io_submit`. The completion callback must not run before this returns. |
| `o0_io_cancel(req)` | Best-effort. `O0_EBUSY` means the request is completing and the callback will still run. |
| `o0_device_register_ops(dev, &ops)` | Binds A3's callbacks. The table must stay valid for the device lifetime. |

## 4. A3 callbacks A3 must implement

A3 implements exactly this table. A NULL callback means the operation is unsupported and
the A1 wrapper returns `O0_ENOTSUP`.

```c
struct o0_device_ops {
    o0_status (*dma_constraints)(o0_device_t *dev, struct o0_dma_constraint *out);
    o0_status (*dma_quiesce)(o0_device_t *dev);
    o0_status (*reset)(o0_device_t *dev);
    o0_status (*io_submit)(o0_device_t *dev, struct o0_io_request *req);
    o0_status (*io_cancel)(struct o0_io_request *req);
};
```

| Callback | Required behaviour | Failure |
|---|---|---|
| `dma_constraints` | Fill `addr_mask`, `alignment`, `max_segment` and `coherent` from the device. `addr_mask` is a contiguous low-bit mask. | `O0_EINVAL` for an invalid mask or alignment |
| `dma_quiesce` | Stop new DMA, wait for in-flight requests, stay idempotent. Do not free buffers. | `O0_ETIMEDOUT` if it cannot stop |
| `reset` | Reset the device after quiesce. Do not free A1 buffers or unmap BARs. The driver reinitialises. | `O0_EIO` or `O0_ETIMEDOUT` |
| `io_submit` | Enqueue the request. Return accepted; completion runs after return. Do not touch `req` after completion. | `O0_EINVAL`, `O0_EBUSY`, `O0_EIO` |
| `io_cancel` | Best-effort cancel. Return `O0_EBUSY` if the request is already completing. | `O0_EBUSY`, `O0_ENOENT` |

## 5. Rules for A3's `platform.h` and `block.h`

A3 must make both headers satisfy all of these before requesting sign-off:

1. Include A1 headers as `<ozero/driver.h>`, `<ozero/mm.h>`, `<ozero/irq.h>`,
   `<ozero/status.h>` and `<ozero/types.h>`. Do not redeclare `o0_status`, `o0_paddr_t`,
   `o0_vaddr_t`, `o0_dma_buffer`, `o0_dma_constraint`, `o0_device_t`, `o0_io_request` or
   `o0_io_limits`.
2. No `dma_domain_t`, `iommu_domain_t`, `iova_t`, `dma_addr_t` or equivalent translated
   address type. Device-visible addresses are `o0_paddr_t`. CPU addresses are `void *`.
3. No platform `dma_map` or `dma_unmap` that bypasses `o0_dma_alloc`, `o0_dma_sync`,
   `o0_dma_free` or `o0_as_map`.
4. `block.h` uses `struct o0_io_request`. A block request may wrap it, but there is one
   request object and one completion callback.
5. No scatter-gather pointer inside `o0_io_request`. Stage 1 has one contiguous buffer per
   request. A3 splits larger transfers into multiple requests or allocates multiple
   buffers.
6. No implicit 32-bit DMA assumption. If the device needs a 32-bit mask, set
   `addr_mask = 0xffffffff`. If it needs alignment or a segment limit, set it.
7. No reset that assumes DMA buffers, queue state or BAR mappings survive. Reset is a full
   device reset; A3 reinitialises and re-arms the queue.
8. No BAR access through the HHDM. Use `o0_device_map_bar`.
9. No request completion before `o0_io_submit` returns. Defer synchronous completions.
10. No use of a buffer after `o0_dma_free` and no free before quiesce.

## 6. Exact DMA lifecycle

1. Enumerate the device and map its BAR as UC/NX.
2. Resolve the interrupt and register the handler.
3. Call `o0_device_dma_constraints`; reject a mask that is not a contiguous low-bit mask.
4. Allocate DMA buffers with `o0_dma_alloc`; keep `buf->virt` for CPU access and
   `buf->phys` for device programming.
5. Enable bus mastering.
6. For a non-coherent buffer, call `o0_dma_sync` after writing and before reading.
7. Submit one `o0_io_request` per contiguous buffer. The completion callback owns the
   request until it runs.
8. On teardown, cancel or wait for outstanding requests, call `o0_device_dma_quiesce`,
   then `o0_dma_free`, then `o0_device_unmap_bar`, then disable bus mastering.
9. Reset only after quiesce and completion. Reinitialise the device after reset.

## 7. Error mapping

| Code | Use |
|---|---|
| `O0_EINVAL` | Bad argument, bad alignment, bad mask, size zero |
| `O0_ENOTSUP` | Unsupported constraint, unsupported operation, no IOMMU translation |
| `O0_EBUSY` | Request or device still in use |
| `O0_ERANGE` | Address outside the device's supported range |
| `O0_EIO` | Device or transport error |
| `O0_ETIMEDOUT` | Quiesce or reset did not complete |
| `O0_ENOMEM` | DMA allocation failed |

## 8. Approval status

A1 has issued the conditions above. A1 has **not** signed IF-008 v0.1 because A3's
`platform.h` and `block.h` are not published. When A3 publishes them:

1. A3 replies with the commit hash and a point-by-point checklist against §5.
2. A1 reviews the actual headers, not a summary.
3. A1 records `IF-008 v0.1 approved by A1` in the PR and in the global register.
4. Only then may A3 start the AHCI probe and real DMA work.

Until then, the Stage 1 restriction stands: trusted ring-0 drivers only, no DMA domain, no
IOMMU assumptions, no reset assumptions.

## 9. Sources

- A1, `include/ozero/mm.h`, `driver.h`, `irq.h`, `status.h`.
- A1, `docs/interface-draft.md` §11.
- A4, `docs/architecture/interfaces.md` IF-008 and the integration checklist.
- Limine, pinned protocol `877c2d4c`, base revision 6, IOMMU disabled.
