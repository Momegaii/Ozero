# Ozero A3 — Driver Interface Draft (A1 Contracts)

Status: **Stage 0 desk artifact; no implementation or hardware validation**

A3 has reconciled its draft against A1's signed Stage 1 surface at
`a1/stage0-review` @ `30ee4f0`. The authoritative interface is
[IF-008 v1.1](interfaces/IF-008-dma-platform.md). A1 ACCEPTed at `b9a69d8` (`85df005`, review head `74b1471`); Director D-007 is pending, so the AHCI probe remains blocked. Stage 0 is not accepted. See
[`06-stage0-review-response.md`](06-stage0-review-response.md).

Scope: discovery, MMIO, DMA, interrupts, reset and asynchronous I/O that A3
block drivers need from A1.

Draft header: [`include/ozero/platform.h`](../include/ozero/platform.h)

The header is not a second ABI. It includes A1's `include/ozero/mm.h`,
`driver.h`, `irq.h`, `status.h` and `types.h` directly and does not redeclare
their types. A1 owns those headers; A3 owns `platform.h` and `block.h`.

## 1. Purpose and ownership

A3 writes device drivers. A1 owns the platform mechanisms those drivers need.
The boundary is intentionally narrow:

| Area | A1 owns | A3 owns |
|---|---|---|
| Discovery | `o0_device_enumerate`, bus-neutral identity | Class/protocol binding, quirk tables |
| MMIO | `o0_device_map_bar` / `o0_device_unmap_bar`, dedicated UC mapping | Register layout, sequencing, locking |
| DMA | Constraints, allocation, cache maintenance, quiesce | Buffer lifetime, direction, segment splitting, device callback table |
| Interrupts | GSI/vector allocation, handler registration, mask/unmask | Device cause, acknowledge, handler policy |
| Reset | `o0_device_dma_quiesce`, `o0_device_reset` | Protocol reinitialization after reset |
| Generic I/O | `o0_io_submit`, `o0_io_cancel`, completion callback, `o0_device_register_ops` | `struct o0_device_ops` callbacks, request tracking, timeout policy, block completion |
| Firmware | A1-owned boot and platform data | No direct ACPI/UEFI parsing in Stage 1 |

The rule is simple: **A3 never programs an IOMMU, walks an ACPI table, or derives
a BAR address from the HHDM.** A1 never interprets AHCI, NVMe, USB or i915
registers.

## 2. ABI and capability negotiation

There is no `o0_platform_ops` table in A1's signed Stage 1 surface. A3 includes
the A1 headers and calls the functions directly:

```c
#define O0_IF008_VERSION 0x00010001u

#include <ozero/types.h>
#include <ozero/status.h>
#include <ozero/irq.h>
#include <ozero/mm.h>
#include <ozero/driver.h>
```

A3 implements `struct o0_device_ops` and registers it with
`o0_device_register_ops`; A1's wrappers validate arguments and dispatch to those
callbacks.

A3 must:

1. Check `O0_IF008_VERSION` against the version it was built for.
2. Treat every A1 call as fallible; there is no capability bitmask.
3. Fail the probe with `O0_ENOTSUP` or `O0_ENOSYS` if a required service is absent.
4. Not invent a parallel platform ABI or a local capability registry.

A semantic change to DMA, MMIO, IRQ, reset or generic I/O is a versioned IF-008
change, not a driver-local decision. Global decision: **D-007**.

## 3. Discovery and resources

### 3.1 Device enumeration

```c
o0_status o0_device_enumerate(o0_device_iter_fn fn, void *ctx);
```

`o0_device_id` carries vendor, device, subsystem, class, subclass, prog-if,
revision, bus, slot and function. A3 must bind by class and protocol, not only
by a VID/PID list.

A1 does not expose PCI config-space read/write in Stage 1. A3 must not write
`0xffffffff` to size a BAR or reprogram a BAR itself. If config-space access is
required, it is a new IF-008 version.

### 3.2 BAR mapping and bus mastering

```c
o0_status o0_device_map_bar(o0_device_t *dev, uint8_t bar,
                            size_t *out_len, void **out_virt);
o0_status o0_device_unmap_bar(o0_device_t *dev, void *virt, size_t len);
o0_status o0_device_bus_master_enable(o0_device_t *dev, bool enable);
```

A1 maps a memory BAR as a dedicated uncached (`O0_MAP_UC`), non-executable
mapping. It is never served from the HHDM. A3 must quiesce DMA before unmapping
a BAR.

A3 must not enable bus mastering before it has valid DMA constraints and has
quiesced the device.

### 3.3 I/O limits

```c
struct o0_io_limits {
    uint32_t max_transfer;
    uint16_t max_queue_depth;
    uint16_t alignment;
    uint32_t flags;
};
o0_status o0_device_io_limits(o0_device_t *dev, struct o0_io_limits *out);
```

A3 validates the device-class-specific parts (logical block size, queue model,
NCQ/zoned support) before submitting I/O. A1's limits are an upper bound, not a
promise that the device supports a particular command set.

### 3.4 Firmware tables

A1's signed Stage 1 surface does not expose ACPI/UEFI table access to A3. A3
must not walk firmware tables directly. If a block driver needs a firmware
property, it must be added to IF-008 as a versioned A1 service.

## 4. MMIO

A3 accesses a BAR only through the pointer returned by `o0_device_map_bar`. The
mapping is UC and non-executable. A3 owns register layout, read-modify-write
locking and device-defined ordering.

A1 does not expose a posting-flush primitive in Stage 1. A3 must use a
device-defined read-back or an A1-approved barrier if one is added. A3 must not
assume a normal cached write reaches the device in order.

A3 must unmap the BAR only after `o0_device_dma_quiesce` and after all users of
the mapping have stopped.

## 5. DMA and IOMMU

A1's signed Stage 1 DMA surface is:

```c
struct o0_dma_constraint {
    uint64_t addr_mask;
    uint32_t alignment;
    uint32_t max_segment;
    bool coherent;
    uint32_t reserved;
};

struct o0_dma_buffer {
    o0_paddr_t phys;
    void *virt;
    size_t size;
    uint32_t flags;
    uint32_t reserved;
};

enum o0_dma_dir {
    O0_DMA_TO_DEVICE = 0,
    O0_DMA_FROM_DEVICE = 1,
    O0_DMA_BIDIRECTIONAL = 2
};

o0_status o0_dma_alloc(const struct o0_dma_constraint *constraint,
                       size_t size, struct o0_dma_buffer *out);
o0_status o0_dma_sync(struct o0_dma_buffer *buf, size_t offset, size_t len,
                      enum o0_dma_dir dir);
void o0_dma_free(struct o0_dma_buffer *buf);
```

Contract:

- There is **no IOMMU domain** in Stage 1. `o0_dma_buffer.phys` is an
  untranslated physical address.
- There is **no SG pointer** in `o0_io_request`. One request carries one
  contiguous buffer. A transfer larger than `max_segment` is split into multiple
  `o0_dma_buffer` objects and multiple requests by the protocol driver.
- `o0_dma_alloc` returns `O0_ENOTSUP` when `size > max_segment`; it does not
  silently allocate a larger contiguous block.
- The driver supplies constraints; A1 does not infer them from a device class.
- `addr_mask` must be a contiguous low-bit mask. `alignment` is a power of two
  or zero. `max_segment` is bytes per physically contiguous segment, or zero
  for no limit.
- `coherent == false` requires `o0_dma_sync` around transfers.
- `o0_dma_free` requires that no DMA is in flight; quiesce the device first.
- Stage 1 is **trusted ring-0 only**. Do not expose DMA to userspace or an
  untrusted driver before an IOMMU domain exists.

A3 must not use `o0_dma_domain`, `dma_map_single`, `dma_map_sg`, `iommu_map`,
`iommu_unmap`, `dma_domain_t`, `iommu_domain_t`, `iova_t` or `dma_addr_t`; those
symbols are not in A1's signed surface.

## 6. Interrupts

A1's signed interrupt surface is:

```c
o0_status o0_device_irq(o0_device_t *dev, o0_gsi_t *out_gsi,
                        o0_vector_t *out_vector);
o0_status o0_irq_register(o0_gsi_t gsi, uint32_t flags,
                          o0_irq_handler_t handler, void *ctx);
o0_status o0_irq_unregister(o0_gsi_t gsi, o0_irq_handler_t handler, void *ctx);
o0_status o0_irq_mask(o0_gsi_t gsi);
o0_status o0_irq_unmask(o0_gsi_t gsi);
o0_status o0_irq_alloc_vectors(uint32_t count, o0_vector_t *out_first);
void o0_irq_free_vectors(o0_vector_t first, uint32_t count);
```

Contract:

- GSI and vector are distinct. A GSI is a platform interrupt; a vector is a CPU
  IDT vector.
- The handler returns `true` if the interrupt belonged to the device, `false`
  otherwise. There is no `O0_IRQ_HANDLED`/`O0_IRQ_NONE` enum.
- The handler runs in hard IRQ context. It must not sleep, allocate unbounded
  memory, or wait on a completion.
- A3 acknowledges the device cause itself. A1 masks/unmasks the platform line.
- A1 does not expose `irq_set_affinity` or a separate `irq_ack` in Stage 1.
- Legacy INTx on Haswell may arrive as a GSI; A3 must not assume MSI-X.

## 7. Completion, fences, and deferred work

A1's signed Stage 1 completion surface is the generic asynchronous request:

```c
struct o0_io_request {
    enum o0_io_op op;
    uint64_t offset;
    void *buffer;
    size_t length;
    uint32_t flags;
    o0_status status;
    o0_io_complete_fn complete;
    void *ctx;
    void *driver_private[2];
};

struct o0_device_ops {
    o0_status (*dma_constraints)(o0_device_t *dev, struct o0_dma_constraint *out);
    o0_status (*dma_quiesce)(o0_device_t *dev);
    o0_status (*reset)(o0_device_t *dev);
    o0_status (*io_submit)(o0_device_t *dev, struct o0_io_request *req);
    o0_status (*io_cancel)(struct o0_io_request *req);
};

o0_status o0_device_register_ops(o0_device_t *dev,
                                 const struct o0_device_ops *ops);
o0_status o0_io_submit(o0_device_t *dev, struct o0_io_request *req);
o0_status o0_io_cancel(struct o0_io_request *req);
```

Contract:

- A3 implements the five callbacks. A NULL callback means unsupported and makes
  the A1 wrapper return `O0_ENOTSUP`.
- `o0_io_submit` dispatches to A3's `io_submit` callback. The completion callback
  must not run before `o0_io_submit` returns; a synchronous completion must be
  deferred.
- One `o0_io_request` carries one contiguous buffer. There is no SG pointer in
  the generic request. A3 splits a large transfer into multiple requests.
- `o0_io_cancel` is best-effort. If it returns `O0_EBUSY`, the request is
  already completing and the callback will still run.
- The buffer must remain valid until the completion callback runs.
- A3's block layer uses `o0_block_complete_bio`; the protocol driver's
  `io.complete` trampoline calls it. A3's protocol driver owns request tracking
  and timeout policy.
- A1 does not expose `fence_create`, `fence_signal`, `fence_wait` or
  `work_schedule` in Stage 1. A3 must not depend on them. If a block driver
  needs a deferred-work service, it is a new IF-008 version.

## 8. Memory mapping and cache maintenance

A3 uses:

- `o0_device_map_bar` for device MMIO, always dedicated UC and non-executable.
- `o0_dma_alloc` for DMA buffers, with `o0_dma_sync` for non-coherent buffers.
- `o0_dma_free` only after `o0_device_dma_quiesce`.

A1 does not expose `mem_map_pages`, `dma_alloc_coherent` or a separate
`mmio_flush` in Stage 1. A3 must not assume those names exist.

For GPU page tables and command buffers, A3 must use `o0_dma_alloc` and build
its own GPU page tables. A3 must not program an IOMMU or use an IOMMU domain.

## 9. Power, reset, and hotplug

A1's signed Stage 1 reset surface is:

```c
o0_status o0_device_dma_quiesce(o0_device_t *dev);
o0_status o0_device_reset(o0_device_t *dev);
```

Contract:

- `o0_device_dma_quiesce` stops new DMA and waits for in-flight requests.
- `o0_device_reset` is unconditional; there is no reset-kind argument. It
  dispatches to A3's `reset` callback after quiesce.
- Reset invalidates in-flight requests and device state, but does not free
  kernel DMA buffers or unmap BARs. A3 reinitialises and re-arms the queue.
- A1 does not expose runtime PM, reset lines or hotplug registration in Stage 1.
  A3 must not assume `pm_get`, `pm_set_state`, `reset_assert`,
  `reset_deassert` or `hotplug_register`.

A3 reset sequence:

1. Stop accepting new requests.
2. Mask interrupts.
3. Abort or time out outstanding requests.
4. Call `o0_device_dma_quiesce`.
5. Call `o0_device_reset`.
6. Reinitialize the device.
7. Unmask interrupts.
8. Requeue or fail outstanding requests exactly once.

A3's block-layer `reset` callback resets A3 protocol state only. It does not
replace A1's quiesce/reset sequence.

## 10. Error model

All A1 operations return `o0_status`. The values are positive enum values, not
negative errno values:

| Status | Meaning |
|---|---|
| `O0_OK` | Success |
| `O0_EINVAL` | Bad argument or alignment |
| `O0_ENOMEM` | Out of memory |
| `O0_ENOENT` | No such object, device or mapping |
| `O0_EBUSY` | Resource busy |
| `O0_EPERM` | Not permitted |
| `O0_ENOSYS` | Not implemented |
| `O0_EFAULT` | Invalid user pointer |
| `O0_EIO` | Hardware or transport error |
| `O0_ERANGE` | Outside the supported range |
| `O0_ETIMEDOUT` | Timed out |
| `O0_EAGAIN` | Retry later |
| `O0_EOVERFLOW` | Arithmetic overflow |
| `O0_ENOTSUP` | Valid request the interface cannot express |

A3 must translate these into protocol-specific recovery. Only `O0_EAGAIN` is
safe to retry immediately in a submission path.

## 11. Locking, ordering, and lifetime

| Object | Lifetime | Rule |
|---|---|---|
| BAR mapping | Until `o0_device_unmap_bar` | Quiesce DMA first |
| DMA buffer | Until `o0_dma_free` | No in-flight DMA; quiesce first |
| `o0_io_request` | Until completion callback | Cancel is best-effort; buffer stays alive |
| IRQ handler | Until `o0_irq_unregister` | A1 must serialize against in-flight handlers |
| Device | Until A1 removes it | A3 must tolerate removal without touching freed memory |

`o0_device_reset` invalidates in-flight requests and device state. It does not
free A1 DMA buffers or unmap BARs; A3 must reinitialize the device and re-arm
the queue, but it must not free a buffer or unmap a BAR from the `reset`
callback.

## 12. Contract checklist for A1

| Requirement | Why | How to verify |
|---|---|---|
| `o0_device_enumerate` returns stable IDs | Driver binding | Enumerate twice; compare IDs |
| BAR mapping is dedicated UC and not HHDM | Safe MMIO | Map a BAR; check pointer and cache attributes |
| GSI/vector split | Correct IRQ routing | Allocate MSI and legacy; compare GSI/vector |
| DMA constraints are caller-supplied | Device-specific limits | Allocate with a 32-bit mask and a 4 KB segment limit |
| DMA addresses are physical in Stage 1 | No IOMMU | Check `o0_dma_buffer.phys` against the allocation |
| `o0_dma_sync` is explicit for non-coherent buffers | No implicit maintenance | Write, sync, read from device |
| `o0_device_dma_quiesce` stops DMA | Safe free/unmap/reset | Start DMA; quiesce; verify no writes |
| `o0_device_reset` is unconditional | No reset-kind assumption | Quiesce; reset; reinitialize |
| `o0_io_cancel` is best-effort | Completion may still run | Cancel a request; check callback |
| `o0_device_register_ops` binds A3's callback table | No implicit callbacks | Register a table; call a wrapper; verify dispatch |
| Reset does not free buffers or unmap BARs | A3 owns reinitialisation | Quiesce, reset, reuse the same buffer and BAR mapping |
| `o0_io_limits` bounds transfer size and depth | Block-layer validation | Query limits; reject oversized I/O |

## 13. What A1 must not expose

- IOMMU page tables or domain internals. Stage 1 has no IOMMU domain.
- ACPI AML interpreter internals or raw firmware tables to A3.
- Device-specific register semantics. A1 is not a driver.
- Implicit resets or implicit DMA. Reset and bus mastering are explicit.
- Unbounded delays in interrupt context.
- A promise that a firmware table is valid forever.

A1 does expose physical DMA addresses in Stage 1. That is the recorded
limitation, not an accident. A3 must not treat it as isolation.

## 14. What A3 must not assume

- That an IOMMU domain exists.
- That an SG pointer exists in `o0_io_request`.
- That a reset-kind argument exists.
- That BAR mappings come from the HHDM.
- That DMA is coherent; use `o0_dma_sync` when `coherent == false`.
- That an IRQ is exclusive; handle shared lines.
- That MSI-X is available.
- That config-space access or firmware tables are exposed.
- That a posting-flush primitive exists.
- That runtime PM or hotplug callbacks exist.
- That a device is present after removal.

## 15. Open decisions

Global decision register: A4's
[`docs/decisions/decision-log.md`](https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/decisions/decision-log.md).

| ID | Decision | Owner | Status |
|---|---|---|---|
| D-001 | A1/A2/A3 domain split and GitHub team slugs | A4 | Proposed |
| D-002 | Branch isolation and review gate; protect `main` | A4 | Proposed |
| D-005 | Boot protocol and kernel handoff | A1 | Proposed |
| D-006 | Accept Stage 0 | A4 | Proposed |
| D-007 | Approve IF-008 and the A1 sign-off gate | A1 + A3 | Proposed; A1 ACCEPT at `b9a69d8` (`85df005`); Director approval pending |
| D-009 | Public header namespace | A1 + A4 | **Approved** in A4 `b4232a0`: `include/ozero/` only; A1 `03f8883`; XB-007 closed |
| D-010 | `tools/check-headers.sh` ownership | A1 + A4 | **Approved** in A4 `b4232a0`: A1 owns checker and static asserts; A4 owns CI; XB-008 closed |
| D-020 | DMA is constraint-driven, never device-class-driven | A1 | Proposed |
| D-021 | IOMMU policy: trusted ring-0 only in Stage 1 | A1 | Proposed; director approval pending |
| D-031 | Branch protection | A4 | Proposed; no merge to `main` |

A3 implementation choices (AHCI/NVMe native drivers, NVMe 1.4 core, 4Kn/TRIM)
are recorded in `docs/02` and `docs/05`; they are not global decisions until the
director adds them to the register.

## 16. References

- [IF-008 v1.1](interfaces/IF-008-dma-platform.md)
- A1 `include/ozero/mm.h`, `driver.h`, `irq.h`, `status.h`, `types.h`:
  <https://github.com/Momegaii/Ozero/tree/30ee4f0/include/ozero>
- A4 global decision register:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/decisions/decision-log.md>
- A4 cross-agent blocker register:
  <https://github.com/Momegaii/Ozero/blob/a4/stage0-review-followup/docs/process/cross-agent-blockers.md>
- Intel, *Serial ATA AHCI 1.3.1*:
  <https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf>
- NVM Express, *Specifications*:
  <https://nvmexpress.org/specifications/>
- UEFI Forum, *Specifications*:
  <https://uefi.org/specifications>
