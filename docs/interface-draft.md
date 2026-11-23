# Interface draft (Stage 0)

Status: sketch. Header names and documented invariants are intended to be stable; function
signatures are not frozen. The syscall ABI is explicitly not defined here.

The headers compile freestanding as C11 and C++17, and the documented ABI facts are
checked by `tools/check-headers.sh` (see §9).

## 1. Conventions

| Rule | Detail |
|---|---|
| Return type | `o0_status` for fallible operations. A returned pointer or physical address is 0 on failure only where documented; prefer the status form for new APIs |
| No errno | `o0_status` is kernel-internal. Translation to a user-visible ABI is deferred (A2/A4) |
| No NTSTATUS | No Windows status values, object-manager semantics or IRP model in generic headers |
| Addresses | `o0_paddr_t` and `o0_vaddr_t` are 64-bit, always. Never `unsigned long` for an address |
| Sizes | Bytes unless the name says pages or order. `len` is rounded outward to pages by mapping calls |
| Ownership | Documented per call: who allocates, who frees, how long a pointer stays valid |
| Alignment | Power-of-two alignments; 0 means natural or page alignment as documented |
| Opaque types | `o0_as_t`, `o0_process_t`, `o0_thread_t`, `o0_object_t`, `o0_device_t` are forward-declared only. No struct layout is public |
| Locking | Not implied by the API. Callers use `o0_irq_save`/`o0_irq_restore` and subsystem locks; handler context is documented per interface |
| Freestanding | Only `<stdint.h>`, `<stddef.h>` and `<stdbool.h>` are allowed in public headers |
| Attributes | `O0_NODISCARD` on allocating or converting calls; `O0_NORETURN` on functions that do not return |

## 2. Header inventory

| Header | Provides | Primary consumer |
|---|---|---|
| `types.h` | Fixed-width types, page/alignment helpers, opaque forward declarations | All |
| `status.h` | `o0_status`, status names | All |
| `boot.h` | Normalised memory map, boot info, CPU list, firmware type | Entry, PMM, ACPI, SMP |
| `cpu.h` | Feature table, per-CPU info, TSC, idle/halt | Scheduler, time, arch |
| `mm.h` | PMM, address spaces, kernel heap, HHDM, DMA | A2, A3, all kernel code |
| `irq.h` | GSI/vector types, handler registration, masking, vector allocation, irq state | A3, drivers, arch |
| `thread.h` | Process and thread lifecycle, yield, sleep | A2, scheduler |
| `ipc.h` | Opaque handles, rights, object types, channels | A2, IPC tests |
| `driver.h` | Device enumeration, BAR mapping, IRQ/DMA plumbing, async I/O request | A3 |
| `o0.h` | Umbrella include | Tests, external consumers |

## 3. Physical memory

- `o0_pmm_init` consumes the normalised map from `boot.h`. It must not trust the original
  Limine array, which lives in reclaimable memory. [LIMINE-MEMMAP]
- Allocation is by order (`2^order` contiguous pages). This is enough for page tables,
  stacks, DMA and the kernel heap without committing to a buddy allocator now (U2).
- Physical address 0 is never returned as a valid allocation, so `0` is an unambiguous
  failure value even though page 0 may be usable at revision ≥ 3. [LIMINE-BASE]
- `o0_pmm_release_bootloader_memory` is the only path that frees reclaimable memory, and
  it refuses until the boot contract's checklist passes (boot contract §8, §9.3). This
  makes the unsafe transition a single auditable call.
- HHDM conversion is **checked**: `o0_hhdm_to_virt(phys, len, &out)` requires every byte
  of the range to lie in an HHDM-mapped region and all arithmetic to be overflow-free
  (boot contract §5.1). `o0_phys_to_virt` is only a fast path for a single page already
  validated by the allocator or by the checked call; it is not a general mapper.
  [LIMINE-LAYOUT]
- Device MMIO is never reached through the HHDM. `o0_device_map_bar` creates a dedicated
  uncached, non-executable mapping; the driver unmaps it after quiescing DMA.

## 4. Virtual memory

- `o0_as_map` takes a physical range and explicit flags. It never guesses cache
  attributes. `O0_MAP_WC` requires PAT and must not conflict with an existing mapping of
  the same page. [LIMINE-CACHE]
- `O0_MAP_NX` is explicit. Writable data mappings default to NX unless the caller asks for
  executable. This makes the unsafe case visible in review.
- User mappings require `O0_MAP_USER`; kernel mappings must not carry it. SMEP/SMAP are
  enabled when available, so a missing user bit is a real protection boundary, not a
  convention. [SDM-3A] "Protection".
- `o0_as_create` creates a new address space with the kernel half shared. The exact
  higher-half layout is internal and may change without notice.
- TLB maintenance is the implementation's responsibility; callers must not assume a
  mapping is visible on another CPU without a documented shootdown.
- `o0_kalloc` is a kernel heap for small objects. It is not a general-purpose allocator
  for user memory and must not be used for DMA.

## 5. Interrupts

- A **GSI** is a global system interrupt from ACPI MADT; a **vector** is an IDT entry.
  `o0_irq_register` takes a GSI; `o0_irq_alloc_vectors` returns vectors for MSI/MSI-X or
  per-CPU use. Conflating them is a routing bug. [ACPI-5.2.12]
- Handlers run with interrupts disabled on the interrupted stack (or an IST stack for
  double fault, NMI and machine check). A handler must not sleep, allocate from a
  blocking path, or take a lock that can be held by a preempted thread.
- Shared lines are supported through the `bool` return value: a handler returns true only
  if it serviced the device.
- Mask/unmask is explicit. The kernel never unmasks a line before a handler is registered
  and the device is quiesced. Limine hands off with all routable lines masked.
  [LIMINE-STATE]
- `o0_irq_save`/`o0_irq_restore` are the only supported critical-section primitives in
  Stage 1. They are nesting-safe.

## 6. Threads and processes

- `o0_process_t` owns an address space and a handle table. `o0_thread_t` is a schedulable
  context in exactly one process.
- `o0_thread_create` takes an explicit entry function and stack size. Kernel threads and
  user threads share the type; the process's address space decides the privilege of the
  entry path.
- `o0_thread_exit` does not return. `o0_thread_join` is the only supported way to observe
  a thread's exit status; there is no implicit reaping.
- Priority is an `int32_t` field reserved now, not a scheduling policy commitment (U3).
- No Windows thread/process semantics, tokens, or APC model is implied.

## 7. Objects, handles and IPC

- A handle is an opaque 64-bit value, scoped to a process, with rights and a generation
  counter. It is never a kernel pointer. A stale handle fails with `O0_ENOENT`; it never
  aliases a reused slot.
- `O0_RIGHT_*` is a starting set, not a frozen ABI. Rights are checked on every handle
  operation.
- `o0_channel_create` returns two endpoints; `send` and `recv` are a minimal transport for
  Stage 2 tests, not a final IPC design. Message copying, zero-copy sections and
  cross-process handle transfer are open questions.
- `o0_handle_wait` is the only wait primitive in the sketch. Wait-for-multiple and
  alertable waits are deferred.

## 8. Driver-facing services

- `o0_device_enumerate` is bus-neutral. `struct o0_device_id` carries vendor/device/class
  fields; classification is A3's job, not the kernel's.
- `o0_device_map_bar` returns a dedicated uncached (`O0_MAP_UC`), non-executable mapping
  for a memory BAR; it is never served from the HHDM. I/O-port BARs are not covered by
  this sketch; if needed, they get a separate interface with explicit permission checks.
- `o0_device_irq` resolves the GSI and, where the device supports it, allocates an
  MSI/MSI-X vector. The driver registers its handler through `irq.h`.
- `o0_device_dma_constraints` returns the device's addressing limits. The kernel never
  infers them from a device class. `o0_dma_alloc` validates the constraint: `addr_mask`
  must be a contiguous low-bit mask, `alignment` a power of two or zero, and every
  returned address must satisfy both. The CPU's physical width is an upper bound, not a
  device constraint.
- Coherency is explicit. A non-coherent buffer requires `o0_dma_sync` before a to-device
  transfer and after a from-device transfer. The kernel performs no implicit cache
  maintenance.
- Lifetimes are explicit. A BAR mapping is valid until `o0_device_unmap_bar`; a DMA buffer
  until `o0_dma_free`. Both require `o0_device_dma_quiesce` first. A reset invalidates
  every mapping and buffer association.
- `o0_device_dma_quiesce` stops new DMA and waits for in-flight requests.
  `o0_device_reset` requires a quiesced device with all requests completed or cancelled.
  `o0_io_cancel` is best-effort: `O0_EBUSY` means the completion callback will still run.
- `o0_device_io_limits` reports `max_transfer`, `max_queue_depth` and `alignment`.
  `struct o0_io_request` stays generic; block size, queue model, SCSI CDBs and NVMe
  descriptors are A3 extensions.
- `struct o0_dma_buffer` describes one physically contiguous buffer. If
  `max_segment < size`, `o0_dma_alloc` returns `O0_ENOTSUP`; A3 builds scatter-gather
  lists from multiple buffers. There is no segment-list object in this sketch.
- There is no display, GPU or graphics interface in this header set. Generic memory and
  driver services contain no device-generation assumptions; the acceptance check is
  automated.

## 9. Verification performed

```
$ ./tools/check-headers.sh
== standalone header compilation ==
PASS  boot.h
PASS  cpu.h
PASS  driver.h
PASS  ipc.h
PASS  irq.h
PASS  mm.h
PASS  o0.h
PASS  status.h
PASS  thread.h
PASS  types.h
== umbrella header, C and C++ ==
PASS  ozero/o0.h (C11)
PASS  ozero/o0.h (C++17)
== ABI static assertions ==
PASS  tests/abi_static_asserts.c
== generic-interface device neutrality ==
PASS  no device-class or GPU-generation terms in include/ozero
== freestanding headers only ==
PASS  no hosted libc headers in include/ozero
RESULT: PASS
```

This proves header coherence and the device-neutrality criterion. It does not prove any
runtime behaviour; that is Stage 1.

## 10. Stability and change control

- Kernel-internal ABI: unstable until Stage 2.
- Header names, documented invariants and the `o0_` prefix: stable.
- Syscall numbers, handle encoding, `o0_status` to user-visible error mapping: deferred to
  A2/A4, and any change goes through review.
- Removing or renaming a public function requires an A4-visible change note.
- Adding a field to a public struct is a versioned change; the kernel must not reinterpret
  an older structure.

## 11. A3 interface review (A1 sign-off)

Scope: the DMA and MMIO surface in `mm.h` and `driver.h`. A1 signs the generic services
for Stage 1 **subject to the IOMMU blocker below**.

| Item | Disposition | Note |
|---|---|---|
| Address width | Signed | `addr_mask` + `max_segment`, validated; CPU physical width is only an upper bound |
| IOMMU assumptions | **Blocker** | No IOMMU in Stage 1 (Limine disables VT-d/AMD-Vi). Returned addresses are physical and untranslated; the API has no domain handle. Limine's Keep IOMMU feature exists, but using it would require the kernel to own a VT-d/AMD-Vi driver |
| Coherency | Signed | Explicit `coherent` flag plus `o0_dma_sync`; no implicit maintenance |
| Map/unmap lifetime | Signed | BAR until unmap, DMA buffer until free, both after quiesce; reset invalidates both |
| Reset and in-flight DMA | Signed | `o0_device_dma_quiesce` → `o0_device_reset`; `o0_io_cancel` best-effort with completion still possible |
| Block I/O shape | Signed | `o0_io_limits` for transfer size, queue depth and alignment; block semantics stay in A3 |
| Interrupt routing | Signed | GSI vs vector split; MSI/MSI-X vector allocation; handler context rules |
| Device-class neutrality | Signed and automated | `tools/check-headers.sh` neutrality check |

Conditions attached to the sign-off:

1. No untrusted or userspace driver receives DMA before an IOMMU domain exists. The
   Stage 1 model is trusted ring-0 drivers only.
2. A3 must not assume a single `o0_dma_buffer` covers a transfer larger than
   `max_segment`; it builds a scatter-gather list from multiple buffers.
3. A3 must not assume BAR mappings come from the HHDM; they are dedicated UC mappings.
4. Any change to these semantics is a versioned interface change, not a driver-local
   decision.

If A3 rejects condition 1 or 2, the blocker is escalated before the AHCI probe or any
real DMA, not worked around in a driver.

## 12. Open questions

- Is a single `o0_status` enum sufficient for A2's compatibility layer, or is a mapping
  table to NTSTATUS needed at the boundary? (A2)
- Should DMA constraints include an explicit IOMMU domain handle once IOMMU support
  exists? Today Limine disables VT-d/AMD-Vi, so there is no domain. [LIMINE-STATE]
- Does A3 want a device-class registry in the kernel, or a pure callback model where A3
  owns all classification?
- Is a blocking `o0_io_submit` variant needed, or is the callback-only form sufficient?
