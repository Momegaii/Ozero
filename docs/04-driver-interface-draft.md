# Ozero A3 — Driver Interface Draft (A1 Contracts)

Status: **Stage 0 desk artifact; no implementation or hardware validation**

A1 sign-off is still open; see [`06-stage0-review-response.md`](06-stage0-review-response.md) §2. The AHCI probe is blocked until A1 answers the DMA, reset, firmware, interrupt, MMIO, and block questions.

Scope: MMIO, DMA/IOMMU, interrupt, discovery, completion, memory mapping,
power, reset, and firmware-table interfaces that A3 device drivers need from A1
Draft header: [`include/o0/platform.h`](../include/o0/platform.h)

## 1. Purpose and ownership

A3 writes device drivers. A1 owns the platform mechanisms those drivers need.
The boundary is intentionally narrow:

| Area | A1 owns | A3 owns |
|---|---|---|
| PCI discovery | Enumeration, config space, BAR sizing, bus mastering, DMA masks | Binding to a vendor/device/class, protocol state |
| MMIO | Mapping, cache attributes, accessors, posting flush, unmap | Register layout, sequencing, read-modify-write locking |
| DMA / IOMMU | Domains, mappings, bounce buffers, coherency, faults | Buffer lifetime, direction, segment limits, command DMA |
| Interrupts | Vector allocation, affinity, masking, dispatch | Device interrupt cause, ack, handler policy |
| Completion | Fences, wait/wake, deferred work | Request tracking, timeout policy, reset recovery |
| Firmware | ACPI/UEFI table discovery and stable copies | Interpreting device-specific `_DSM`/`_CRS` results |
| Power | D-states, clocks, power wells, reset lines | Device-specific power sequence and context save |
| Hotplug | Slot/link events | Protocol-level rescan and media change |

The rule is simple: **A3 never programs an IOMMU, walks an ACPI table, or
touches a physical address.** A1 never interprets AHCI, NVMe, USB, or i915
registers.

## 2. ABI and capability negotiation

```c
#define O0_PLATFORM_ABI_VERSION 0x00010000u

const struct o0_platform_ops *o0_platform_ops_get(void);
```

A3 must:

1. Call `o0_platform_ops_get()` once during driver init.
2. Check `abi_version` is at least the version it was built against.
3. Check the capability bits it needs.
4. Fail the probe with `O0_ERR_NOTSUP` if a required capability is absent.

Capability bits are defined in `platform.h`:

- `O0_CAP_MMIO_WC` — write-combining mappings.
- `O0_CAP_MSI`, `O0_CAP_MSIX` — interrupt types.
- `O0_CAP_IOMMU` — isolated DMA domains.
- `O0_CAP_ACPI`, `O0_CAP_UEFI` — firmware table access.
- `O0_CAP_HOTPLUG` — slot/link hotplug events.
- `O0_CAP_DMA_COHERENT` — coherent DMA allocations.
- `O0_CAP_DMA_64BIT` — 64-bit DMA addresses.
- `O0_CAP_POLLING` — completion polling.
- `O0_CAP_CPU_HOTPLUG` — affinity changes on CPU hotplug.
- `O0_CAP_PCI_PASID` — PASID support (future).

ABI changes are additive within a major version. A struct layout change or a
semantic change requires a new `abi_version` and a compatibility path.

## 3. Discovery and resources

### 3.1 PCI enumeration

```c
o0_status (*pci_enumerate)(unsigned int index,
                           struct o0_pci_id *id,
                           struct o0_device **out);
o0_status (*pci_get_bar)(struct o0_device *dev,
                         unsigned int bar,
                         struct o0_pci_bar *out);
```

Contract:

- `pci_enumerate` is stable for the lifetime of the platform. The index is
  not a bus/slot/function promise; A3 must use the returned `o0_pci_id` and
  the opaque `o0_device`.
- `o0_pci_id` includes vendor, device, subsystem, class, subclass, prog-if,
  revision, bus, slot, and function.
- `pci_get_bar` returns the BAR's base, size, I/O vs memory, 64-bit flag, and
  prefetchable flag. A1 must have sized the BAR correctly; A3 must not write
  `0xffffffff` to size a BAR itself.
- A1 must reject access to a BAR that is not memory-mapped for MMIO use.
- A1 must handle resource conflicts before returning a BAR to A3.

A3 must bind by **class and protocol**, not only by a VID/PID list. A VID/PID
list is a quirk table, not the support policy.

### 3.2 Config space, bus mastering, DMA mask

```c
o0_status (*pci_read_config)(...);
o0_status (*pci_write_config)(...);
o0_status (*pci_enable)(struct o0_device *dev, bool enable);
o0_status (*pci_set_master)(struct o0_device *dev, bool enable);
o0_status (*pci_set_dma_mask)(struct o0_device *dev, uint64_t mask);
o0_status (*pci_get_dma_mask)(struct o0_device *dev, uint64_t *mask);
```

Contract:

- `pci_enable` enables memory/IO decoding; `pci_set_master` enables bus
  mastering. A3 must not enable bus mastering before it has a valid DMA
  domain and has quiesced the device.
- `pci_set_dma_mask` must fail if the device cannot address the requested
  mask. A1 may install a bounce-buffer path for a smaller mask.
- A3 must set the mask before mapping any DMA buffer.
- Config-space writes are serialized by A1; A3 must not rely on a
  read-modify-write being atomic across multiple calls.

### 3.3 ACPI and UEFI tables

```c
o0_status (*acpi_find_table)(const char signature[4],
                             struct o0_acpi_table *out);
o0_status (*fw_memmap)(struct o0_fw_mem_desc **out, size_t *count);
o0_status (*fw_config_table)(const uint8_t guid[16],
                             const void **out,
                             size_t *len);
```

Contract:

- `acpi_find_table` returns a stable virtual mapping and the physical address
  for a 4-character ACPI signature. A1 must keep the table valid for the
  lifetime of the platform ACPI subsystem. If the firmware placed the table
  in `EfiACPIReclaimMemory`, A1 must copy it before that memory is reclaimed.
- `fw_memmap` returns a stable copy of the UEFI memory map, not a pointer into
  boot-services memory that may be reused. A3 uses it to avoid DMA into
  firmware-reserved or runtime-services regions.
- `fw_config_table` returns an industry-standard configuration table by GUID
  (for example, the ACPI 2.0 table GUID or SMBIOS3 GUID). A1 must validate
  the GUID and length.
- A3 must not call UEFI boot services or runtime services directly. If A3
  needs firmware behavior, A1 exposes a platform operation.

Key ACPI tables for A3:

| Table | Use |
|---|---|
| `RSDP`, `XSDT` | Root for all other tables |
| `DSDT`, `SSDT` | Device namespace, `_CRS`, `_PRT`, `_DSM`, `_STA` |
| `MADT` | Local APIC, I/O APIC, interrupt controller topology |
| `SRAT`, `SLIT`, `HMAT` | NUMA and memory topology |
| `NFIT` | NVDIMM / persistent memory (future) |
| `PPTT` | CPU topology |
| `BGRT` | Boot graphics (display handoff) |
| `CPEP`, `BERT`, `EINJ` | Error records and injection |
| `TPM2` | TPM 2.0 interface |
| `WDAT` | Hardware watchdog |
| `LPIT` | Low-power idle |
| `SPCR`, `DBG2` | Serial console / debug port |

`MCFG` (PCIe ECAM), `DMAR` (Intel VT-d), and `IORT` (Arm SMMU) are defined
outside the ACPI specification; A1 may expose them through the same table
lookup or through a dedicated platform operation. The important part is that
A3 does not parse them itself.

### 3.4 Firmware version reality

The reference documents for this project are ACPI 6.6 (May 2025) and UEFI
2.11 (December 2024). Haswell-era firmware is older, typically ACPI 5.x and
UEFI 2.3/2.4. A1 must:

- tolerate missing newer tables and fields;
- provide version/revision information;
- never assume an ACPI 6.6-only object exists on a Haswell machine;
- keep a compatibility path for legacy INTx routing via `_PRT` when MSI/MSI-X
  is unavailable.

## 4. MMIO

```c
o0_status (*mmio_map)(struct o0_device *dev,
                      const struct o0_pci_bar *bar,
                      o0_cache_mode mode,
                      struct o0_mmio **out);
void (*mmio_unmap)(struct o0_mmio *mmio);
uint32_t (*mmio_read32)(const struct o0_mmio *mmio, size_t offset);
uint64_t (*mmio_read64)(const struct o0_mmio *mmio, size_t offset);
void (*mmio_write32)(struct o0_mmio *mmio, size_t offset, uint32_t value);
void (*mmio_write64)(struct o0_mmio *mmio, size_t offset, uint64_t value);
void (*mmio_flush)(struct o0_mmio *mmio);
```

Contract:

- `mmio_map` maps a memory BAR. The default mode is `O0_CACHE_UC`; `O0_CACHE_WC`
  is opt-in and only for buffers where write-combining is safe. I/O BARs are
  rejected.
- The mapping is valid until `mmio_unmap`. A3 must quiesce the device and
  release interrupts before unmapping.
- `mmio_read32`/`write32` are little-endian, naturally aligned accesses. A3
  must not perform unaligned or cross-register accesses.
- `mmio_read64`/`write64` are for true 64-bit registers. If a register pair
  is not atomic, A3 must use two 32-bit accesses with the device's documented
  ordering. A1 does not promise atomicity across a non-atomic pair.
- Writes are posted. `mmio_flush` performs a read-back or equivalent
  completion so that a subsequent side effect (reset, doorbell, interrupt
  acknowledge) is ordered after the write. A3 must call it where the device
  requires it; it is not free.
- A3 owns compound read-modify-write serialization with its device lock. A1
  guarantees only that an individual access is not torn.
- `mmio_unmap` must not be called from an interrupt handler.

## 5. DMA and IOMMU

```c
o0_status (*dma_domain_get)(struct o0_device *dev,
                            struct o0_dma_domain **out);
o0_status (*dma_alloc_coherent)(...);
void (*dma_free_coherent)(...);
o0_status (*dma_map_single)(...);
void (*dma_unmap_single)(...);
o0_status (*dma_map_sg)(...);
void (*dma_unmap_sg)(...);
void (*dma_sync_single_for_cpu)(...);
void (*dma_sync_single_for_device)(...);
o0_status (*iommu_map)(...);
o0_status (*iommu_unmap)(...);
```

Contract:

- Each device gets a DMA domain. A1 owns the IOMMU page tables, domains, and
  fault reporting. A3 sees `o0_dma_addr_t`, never `o0_phys_addr_t`.
- `dma_alloc_coherent` returns a CPU pointer and a DMA address for a buffer
  that is coherent with the device. It accepts alignment and a `boundary`
  constraint (for example, a 4 GB boundary for a 32-bit device). It may
  sleep.
- `dma_map_single` and `dma_map_sg` map streaming buffers. They may sleep.
  A3 must not call them from hard IRQ context.
- `dma_map_sg` may coalesce segments. A3 must use the returned `nents`, not
  the original count. `orig_nents` is preserved for unmapping.
- `dma_unmap_*` must be called exactly once for each successful map, after
  the device is known to have stopped using the buffer.
- `dma_sync_*` is required only on non-coherent architectures or for
  non-coherent mappings. On x86 it may be a barrier; A3 must still call it
  for portability.
- `iommu_map`/`iommu_unmap` are for device-specific mappings that are not
  expressible as a single buffer or SG list. A3 must not build page tables.
- A1 must provide a fault handler or a fault-reporting path. A3 must treat an
  IOMMU fault as a device error and reset the device.

DMA rules for A3:

- Do not pass a stack address, a vmalloc address without mapping, or a user
  address directly to hardware.
- Do not reuse a DMA address after unmap.
- Do not free a buffer while a command that references it is in flight.
- Respect `max_segments`, `max_segment_size`, `dma_alignment`, and boundary.
- Use `dma_alloc_coherent` for command rings, doorbell-adjacent structures,
  and status blocks that the device and CPU both write.

## 6. Interrupts

```c
o0_status (*irq_alloc)(struct o0_device *dev,
                       unsigned int min_vectors,
                       unsigned int max_vectors,
                       o0_irq_type type,
                       struct o0_irq **out);
void (*irq_free)(struct o0_irq *irq);
o0_status (*irq_request)(struct o0_irq *irq,
                         unsigned int vector,
                         o0_irq_handler_t handler,
                         void *data,
                         const char *name,
                         unsigned long flags);
void (*irq_mask)(struct o0_irq *irq, unsigned int vector);
void (*irq_unmask)(struct o0_irq *irq, unsigned int vector);
void (*irq_ack)(struct o0_irq *irq, unsigned int vector);
o0_status (*irq_set_affinity)(struct o0_irq *irq,
                              unsigned int vector,
                              unsigned int cpu);
```

Contract:

- `irq_alloc` requests a vector set. A1 chooses MSI-X, MSI, or legacy INTx
  based on the requested type and platform capability. A3 must handle fewer
  vectors than requested.
- `irq_request` binds a handler to one vector. The handler runs in hard IRQ
  context with the device's MMIO mapped and the device powered. It must not
  sleep, allocate unbounded memory, or wait on a completion.
- Return `O0_IRQ_HANDLED` if the device asserted the interrupt, `O0_IRQ_NONE`
  if it did not (shared interrupt), and `O0_IRQ_WAKE` if the interrupt should
  wake a suspended device.
- `irq_ack` is the device-level acknowledge. A1 may auto-mask the vector; A3
  must clear the device cause before `irq_unmask`.
- `irq_set_affinity` may fail if the CPU is offline or the vector is not
  movable. A3 must tolerate that.
- A1 must serialize `irq_free` with in-flight handlers. A3 must not free an
  IRQ while a handler can still run.

Legacy INTx on Haswell may be routed through `_PRT`; A1 decodes the routing
and provides a vector. A3 must not assume MSI-X is available on every
function.

## 7. Completion, fences, and deferred work

```c
struct o0_fence *(*fence_create)(const char *name);
void (*fence_signal)(struct o0_fence *fence, o0_status status);
o0_status (*fence_wait)(struct o0_fence *fence, uint64_t timeout_ns);
void (*fence_put)(struct o0_fence *fence);
o0_status (*fence_add_callback)(struct o0_fence *fence,
                                o0_fence_cb_t cb,
                                void *data);
o0_status (*work_schedule)(void (*fn)(void *), void *data);
```

Contract:

- A fence is a one-shot completion object. `fence_signal` may be called from
  IRQ, poll, or process context. `fence_wait` may sleep and is for process
  context only.
- `fence_add_callback` registers a callback that may run in IRQ context. The
  callback must not sleep. A3 must keep the callback's data alive until the
  callback runs or the fence is destroyed.
- `fence_put` releases the last reference. A3 must not free a fence while a
  waiter or callback is registered.
- `work_schedule` defers a function to a platform work context. It does not
  promise ordering with respect to other work items unless A1 documents it.
- A3 must have a timeout path for every wait. A fence that never signals is a
  device failure, not a reason to hang the caller forever.

For block I/O, the fence is the bridge between the protocol driver's
completion and the block layer's `o0_block_complete_bio`. For GPU work, it is
the bridge between the seqno write/user interrupt and the request's
completion.

## 8. Memory mapping and cache maintenance

```c
o0_status (*mem_map_pages)(struct o0_dma_domain *domain,
                           o0_phys_addr_t phys,
                           size_t len,
                           o0_cache_mode mode,
                           void **out);
void (*mem_unmap)(void *addr, size_t len);
void (*cache_flush)(void *addr, size_t len);
```

Contract:

- `mem_map_pages` maps a physical range for CPU access. It is for device
  memory windows, reserved pages, or firmware buffers that are not normal
  RAM. A3 must not use it to map arbitrary user pages.
- `mem_unmap` releases the mapping. A3 must ensure no CPU access remains.
- `cache_flush` flushes CPU caches for a range. On x86 it may be a no-op or a
  `clflush` loop; A3 must call it when handing a non-coherent buffer to a
  device that does not snoop.
- For GPU page tables and command buffers, A3 must use `dma_alloc_coherent`
  or explicitly flush; the GPU does not snoop the CPU cache in all cases.

## 9. Power, reset, and hotplug

```c
o0_status (*pm_get)(struct o0_device *dev);
void (*pm_put)(struct o0_device *dev);
o0_status (*pm_set_state)(struct o0_device *dev, o0_power_state state);
o0_status (*reset_assert)(struct o0_device *dev, unsigned int line);
o0_status (*reset_deassert)(struct o0_device *dev, unsigned int line);
o0_status (*hotplug_register)(struct o0_device *dev,
                              void (*cb)(struct o0_device *, void *),
                              void *data);
```

Contract:

- `pm_get`/`pm_put` are reference-counted runtime-PM operations. A3 must hold
  a reference while accessing MMIO or DMA. `pm_put` may trigger a transition
  to a lower power state.
- `pm_set_state` requests a D-state. A1 must save/restore PCI config space
  and platform state as required; A3 must save/restore device-specific state.
- `reset_assert`/`reset_deassert` control a reset line. A1 must guarantee that
  no DMA is in flight after a reset assert/deassert sequence, or provide a
  separate quiesce operation. A3 must stop queues before asserting reset.
- `hotplug_register` delivers slot/link events. A3 must rescan the protocol
  and handle surprise removal without touching freed memory.

Reset rules for A3:

1. Stop accepting new requests.
2. Mask interrupts.
3. Abort or time out outstanding requests.
4. Assert reset.
5. Reinitialize the device.
6. Unmask interrupts.
7. Requeue or fail outstanding requests exactly once.

A1 must not return from a reset operation while the device can still DMA.
A3 must not assume that a reset clears every register; reinitialize what the
protocol requires.

## 10. Error model

All A1 operations return `o0_status`. A3 must handle:

- `O0_ERR_INVAL` — bad argument, misaligned access, invalid BAR.
- `O0_ERR_NOMEM` — allocation failure.
- `O0_ERR_BUSY` — resource busy; retry or fail.
- `O0_ERR_TIMEOUT` — operation did not complete; treat as device failure.
- `O0_ERR_IO` — transport or device error.
- `O0_ERR_NODEV` — device removed.
- `O0_ERR_PERM` — access denied by platform policy.
- `O0_ERR_AGAIN` — transient; retry.
- `O0_ERR_NOTSUP` — capability absent.
- `O0_ERR_FAULT` — IOMMU or memory fault.
- `O0_ERR_RESET` — device or queue must be reset.
- `O0_ERR_OVERFLOW` — address or size overflow.

A3 must translate these into protocol-specific recovery, not ignore them.
`O0_ERR_AGAIN` is the only status that is safe to retry immediately in a
submission path; everything else needs a policy decision.

## 11. Locking, ordering, and lifetime

### 11.1 Context rules

| Operation | May sleep? | May be called from IRQ? |
|---|---|---|
| `pci_*` config access | Usually no | No |
| `mmio_map` / `mmio_unmap` | Yes | No |
| `mmio_read*` / `write*` | No | Yes |
| `dma_alloc_coherent` | Yes | No |
| `dma_map_single/sg` | Yes | No |
| `dma_sync_*` | No | Yes |
| `irq_alloc` / `irq_free` | Yes | No |
| `irq_request` | Yes | No |
| `irq_mask/unmask/ack` | No | Yes |
| `fence_signal` | No | Yes |
| `fence_wait` | Yes | No |
| `fence_add_callback` | No | Yes |
| `work_schedule` | No | Yes |
| `pm_get` / `pm_put` | May sleep | No |

If an operation's context is not listed, A1 must document it. A3 must not
guess.

### 11.2 Ordering

- MMIO writes are posted. Use `mmio_flush` before a dependent side effect.
- DMA mappings must be complete before the doorbell/command that references
  them. Use a write barrier or `mmio_flush` as the device requires.
- Completion may happen before `queue_rq` returns. A3 must not touch the bio
  after handing it to the queue unless it still owns it.
- Fence callbacks may run before `fence_add_callback` returns if the fence is
  already signaled. A3 must handle that race.

### 11.3 Lifetime

- `o0_device` is valid from enumeration until remove. A3 must not cache it
  across a remove.
- `o0_mmio` is valid until `mmio_unmap`.
- `o0_dma_domain` is valid until the device is removed.
- `o0_irq` is valid until `irq_free`.
- `o0_fence` is valid until the last `fence_put`.
- A3 must release every mapping, DMA buffer, IRQ, and fence before remove.
  A1 should track leaks in debug builds.

## 12. Contract checklist for A1

| Requirement | Why | How to verify |
|---|---|---|
| MMIO map/unmap with cache mode | Drivers need safe register access | Map a BAR, read/write, flush, unmap; check no faults |
| MMIO posting flush | Reset/doorbell ordering | Write a register, flush, read back a side-effect register |
| DMA domain per device | Isolation and address translation | Map a buffer, run a device DMA, unmap |
| DMA mask and boundary | 32-bit devices and IOMMU | Set a 32-bit mask, map above 4 GB, verify bounce |
| SG mapping with coalescing | Block and NVMe scatter/gather | Map a fragmented buffer, check `nents` and data |
| MSI/MSI-X allocation | Modern interrupts | Allocate 1 and N vectors, check affinity and masking |
| Legacy INTx fallback | Haswell compatibility | Disable MSI, verify `_PRT` routing |
| Fence signal from IRQ | Completion without sleeping | Signal from a handler, wait in process context |
| Reset with no in-flight DMA | Safe recovery | Start DMA, reset, verify no writes after reset |
| ACPI table copy | Reclaim safety | Find a table in reclaim memory, reclaim, read copy |
| UEFI memory map copy | Avoid firmware memory | Compare map before/after reclaim |
| Runtime PM reference | Power-safe MMIO | Access registers with and without a reference |
| Hotplug event delivery | Surprise removal | Remove a device, verify callback and no use-after-free |
| Capability bits | Graceful degradation | Disable a capability, verify probe fails cleanly |

## 13. What A1 must not expose

- Raw physical addresses to A3. A3 uses DMA addresses.
- IOMMU page tables or domain internals.
- ACPI AML interpreter internals. A1 exposes decoded resources and tables.
- Device-specific register semantics. A1 is not a driver.
- Implicit resets. A reset must be explicit and observable.
- Unbounded delays in interrupt context.
- A promise that a firmware table is valid forever.

## 14. What A3 must not assume

- That MMIO writes are posted; use `mmio_flush`.
- That DMA is coherent on every platform; use `dma_sync_*`.
- That an IRQ is exclusive; handle shared interrupts.
- That a reset clears all device state; reinitialize.
- That a firmware table remains valid after reclaim; copy it.
- That a BAR is 64-bit or prefetchable.
- That MSI-X is available.
- That a queue depth greater than one is supported.
- That a cache mode is honored without a capability check.
- That a device is present after a hotplug event.

## 15. Open decisions

| ID | Question | Recommendation | Impact |
|---|---|---|---|
| P-D1 | Linux-like DMA API or a minimal custom API? | Minimal custom API with `o0_dma_domain` | Less Linux coupling; A3 must not expect `dma_map_page` semantics |
| P-D2 | May `dma_map_*` sleep? | Yes; document it and forbid in IRQ | Simplifies IOMMU mapping; A3 must defer from IRQ |
| P-D3 | Is `mmio_read64` atomic? | Only for true 64-bit registers; otherwise two 32-bit reads | Avoids false atomicity on register pairs |
| P-D4 | Hard IRQ or threaded IRQ? | Hard IRQ for ack, optional threaded bottom half | Keeps latency low; A3 must split work |
| P-D5 | Reset semantics? | A1 guarantees no DMA after reset and returns quiesced | Makes recovery safe; A1 must implement it |
| P-D6 | Firmware table lifetime? | A1 copies ACPI/UEFI tables before reclaim | Avoids use-after-free; costs memory |
| P-D7 | Capability discovery? | Bitmask plus per-call errors | Graceful degradation; A3 must check both |

## 16. References

- Draft headers: [`include/o0/platform.h`](../include/o0/platform.h),
  [`include/o0/block.h`](../include/o0/block.h).
- UEFI Forum, *Unified Extensible Firmware Interface Specification 2.11*
  (December 2024):
  - §4.3 EFI System Table, §4.4 Boot Services Table, §4.5 Runtime Services
    Table, §4.6.1 EFI_CONFIGURATION_TABLE.
  - §7.2.3 GetMemoryMap, §7.4.6 ExitBootServices.
  - §8.4.1 SetVirtualAddressMap.
  - §13.9.1 EFI_BLOCK_IO_PROTOCOL.
  - §14.4.1 EFI_PCI_IO_PROTOCOL.
  - §17.1.2 EFI_USB2_HC_PROTOCOL.
  - §12.9.2 EFI_GRAPHICS_OUTPUT_PROTOCOL.
  - <https://uefi.org/specs/UEFI/2.11/>
- UEFI Forum, *Advanced Configuration and Power Interface (ACPI)
  Specification 6.6* (May 2025):
  - §5.2.5 RSDP, §5.2.8 XSDT, §5.2.11 DSDT/SSDT.
  - §5.2.12 MADT, §5.2.16 SRAT, §5.2.26 NFIT, §5.2.31 PPTT.
  - §6.2.2 `_CRS`, §6.2.12 `_OSC`, §6.2.14 `_PRT`.
  - <https://uefi.org/specs/ACPI/6.6/>
- PCI-SIG, *PCI Express Base Specification*.
  <https://pcisig.com/specifications>
- PCI-SIG, *PCI Firmware Specification* (MCFG table).
  <https://pcisig.com/specifications>
- Intel, *Virtualization Technology for Directed I/O Architecture
  Specification* (DMAR table).
  <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
- Linux kernel, *DMA API*.
  <https://docs.kernel.org/core-api/dma-api.html>
- Linux kernel, *PCI* documentation.
  <https://docs.kernel.org/PCI/index.html>
- Linux kernel, *Runtime Power Management*.
  <https://docs.kernel.org/power/runtime_pm.html>
- Linux kernel, *ACPI* documentation.
  <https://docs.kernel.org/firmware-guide/acpi/index.html>
