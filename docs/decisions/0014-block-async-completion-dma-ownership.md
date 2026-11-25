# ADR 0014: Block-layer async completion and DMA ownership

- **Status:** Accepted (design only for the asynchronous drivers; the synchronous core is
  implemented). Director approved 2026-11-25.
- **Date:** 2026-11-25
- **Wave:** 4.0 (design), implemented from 4.1–4.3
- **Related:** ADR 0003 (Stage 4 substrate), ADR 0012 (crash consistency),
  `include/ozero/block.h`, `docs/wave4.md`

## 1. Context

`include/ozero/block.h` already declares the contract the async drivers will use:

- one `o0_bio` per request, with an `o0_bio_end_t end_io` callback and exactly one
  completion per bio;
- `queue_rq` submits, `poll` retires, and `timeout` may cancel only if it can quiesce all
  DMA and completion callbacks — otherwise ownership stays with the driver;
- timed-out requests stay quarantined until a late completion arrives or the driver
  confirms cancellation;
- `o0_bio_vec` describes logical pages, not hardware scatter-gather entries, and the
  driver may split one bio into several DMA segments.

`kernel/src/block.c` today implements this with **static request slots and bounce
buffers**, driven synchronously: the caller queues, waits, and the RAM disk completes
inline. That is correct for the RAM disk, but it says nothing explicit about:

- who may touch a bio's memory between submission and completion;
- when a DMA buffer may be freed;
- what happens when a completion arrives after a timeout on another CPU;
- whether completion runs in interrupt context or in a thread.

Wave 4.1–4.3 add a DMA allocator, virtio-blk (legacy and modern), AHCI and NVMe, with
interrupt-driven completion, some cells at `-smp 2` so completions land on a non-boot
CPU. Those drivers make every one of the gaps above reachable.

## 2. Alternatives

| Alternative | Trade-off |
|---|---|
| **Async bio with explicit ownership states and reference-counted DMA buffers (chosen)** | Matches the existing `queue_rq`/`end_io` shape; makes lifetime rules checkable in host tests; supports IRQ completion on any CPU. Cost: a state machine and reference counting on both bio and buffers. |
| Synchronous-only API (copy everything into a bounce buffer, block the caller) | Simple and already works for the RAM disk. Cannot express virtio/AHCI/NVMe queueing, and blocks a CPU per request. Rejected as the only path, retained as a compatibility wrapper. |
| Futures/coroutines ("await this bio") | Nicest call sites, but needs scheduler integration, growable stacks or state machines per call site, and cancellation semantics. Deferred; the block layer must not depend on it. |
| Polling only, no interrupts | Removes IRQ-context rules and is easy to test, but wastes a CPU, defeats the `-smp 2` completion test, and does not exercise MSI/MSI-X routing from Wave 3. Rejected as the primary path. |
| Caller frees buffers on timeout | Simple to write, but a late DMA write after free corrupts unrelated memory. Rejected explicitly: this is the defect this ADR exists to prevent. |

## 3. Decision

### Ownership states

Every submitted bio is in exactly one state: `prepared` → `queued` → `in_flight` →
`completing` → `done` | `quarantined`. Transitions are made under the queue lock.
`end_io` is invoked exactly once, either by the driver's completion path or by the
block layer when a submission fails before reaching the device. After `end_io` returns,
the block layer owns neither the bio nor its buffers.

### Buffer lifetime

1. A buffer handed to a driver is **pinned** for the whole request: an in-flight count
   on the DMA allocation. Freeing a pinned buffer is refused (`O0_EBUSY`), not deferred.
2. The bios's `o0_bio_vec` array and the `o0_bio` itself are caller-owned and must stay
   valid until `end_io`. The block layer copies neither.
3. Bounce buffers follow the same pinning rule. A bounce copy-in happens before
   submission; the copy-out happens only in the completion path, after the device
   reports completion.
4. Device-visible memory comes from the DMA allocator (§4.1): physically contiguous,
   correctly aligned for the device's `dma_alignment`, and never a kernel stack
   address.

### Completion context

1. Completion may run in interrupt context. Therefore `end_io` must not sleep, block on
   a mutex, allocate from a sleeping path, or call back into the block layer with the
   queue lock held.
2. Callers that need to sleep (the filesystem does) submit and wait on their own
   primitive; the wait is woken by `end_io`. The block layer does not sleep in IRQ
   context on the caller's behalf.
3. Completion may arrive on any online CPU (Wave 3 IPI/routing), so per-CPU assumptions
   are forbidden inside `end_io`; the bio carries no implied CPU affinity.
4. Lock classes: queue locks are class 0, consistent with the Wave 3 runqueue rule, so a
   completion path can take a queue lock from interrupt context without inverting the
   checker in `O0_LOCK_ORDER_CHECK` builds.

### Timeout, late completion, cancellation

1. Timeout never frees anything. The request moves to `quarantined`; the bio and its
   buffers stay pinned.
2. A late completion is accepted from `quarantined` exactly once and observed as a
   completion (not as success); it cannot be silently discarded, because it proves the
   device still owned the buffer.
3. `timeout()` returns `O0_OK` only after the driver has quiesced all DMA and callbacks
   for that request. Any other result leaves ownership with the driver, and the buffer
   stays pinned.
4. A queued request that never reached the device may be failed back to the caller; that
   failure must still deliver exactly one `end_io`.

### DMA allocator (Wave 4.1)

- Returns physically contiguous, power-of-two page runs from the buddy PMM, aligned to
  the device's requirement, with an owner tag and a pin count.
- Bounce buffers are the default because no IOMMU is configured; direct mapping of
  caller pages is an explicit opt-in the driver must advertise.
- Host tests must prove: a pinned buffer cannot be freed; unpinning after completion
  frees exactly once; a doubly-completed bio is detected; and a partial multi-segment
  transfer of a bio does not release the whole buffer early.

Implementation status (Wave 4.1, 2026-11-25): `kernel/src/dma.c` implements the
allocator with contiguous power-of-two PMM runs, an alignment-aware order, a
`addr_mask` check, and a pin count per buffer. `o0_dma_free()` returns `O0_EBUSY` and
frees nothing while a pin is held, so the buffer cannot be returned to the PMM under an
in-flight request. `tests/dma_test.c` covers the pinned-free refusal, nested pins, double
free and constraint rejection. The allocator is not yet wired to a driver; that is Wave
4.2.

## 4. Consequences

- Drivers must be written against the state machine, not against "the RAM disk returns
  immediately". The RAM disk gets adapted to it, and the synchronous helper becomes a
  wrapper over submit + wait.
- The block layer gains accounting: in-flight bios, pinned buffers, quarantined
  requests. These become boot markers and test assertions (for example "no buffer
  pinned after idle").
- The filesystem can rely on a defined durability boundary: `FLUSH`/`FUA` completion
  means the device accepted the barrier, which is what ADR 0012 builds transactions on.
- `-smp 2` completion-on-AP testing becomes part of Wave 4.2's CI cell rather than an
  optional extra.

## 5. Open questions for the Director

1. May `end_io` complete directly in the interrupt handler (proposal), or must the block
   layer always defer completion to a thread? Deferral costs latency and a work queue;
   direct completion constrains callers more.
2. Should a doubly-completed bio panic in debug builds and be quarantined in release
   builds, or always panic? A double completion is a driver bug, so the proposal is:
   panic in both, because it corrupts accounting either way.
3. Is bounce-buffer-by-default acceptable for the whole wave, or is direct DMA mapping
   of caller pages required as soon as one driver proves it?
4. What is the maximum per-bio size the block layer enforces (segments × segment size),
   and does it derive from device geometry only, or also from a global memory cap?
5. Does the DMA allocator belong to the block layer (proposal) or to a neutral platform
   DMA service shared with future network and GPU drivers? If shared, this ADR should be
   revisited before 4.2.
