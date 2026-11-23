# Ozero A3 — Storage Architecture Note

Status: **Stage 0 desk artifact; no implementation or hardware validation**

Scope: device-independent block I/O, AHCI/SATA, NVMe, and USB mass storage
Related: [`01-hardware-support-matrix.md`](01-hardware-support-matrix.md),
[`04-driver-interface-draft.md`](04-driver-interface-draft.md),
[`05-hardware-test-plan.md`](05-hardware-test-plan.md)

## 1. Position

Ozero needs one block I/O contract and several protocol-specific drivers. The
contract must not pretend that a rotating SATA disk, an NVMe namespace, and a
USB mass-storage LUN are the same device. They share a block device model,
but they do not share command queues, error models, cache semantics, or
discovery rules.

The design position is:

- The **block layer** owns requests, logical block addressing, queue
  selection, merging, partitioning, cache policy, and the device-independent
  error model.
- A **protocol driver** owns controller discovery, command translation,
  transport queues, DMA mapping, completion, reset, and protocol-specific
  error recovery.
- **Media type is a property**, not a driver. `rotational` and `removable` are
  hints exposed by the device; they do not select a different protocol stack.
- **NVMe is not a generic SSD driver.** It binds to an NVMe controller and
  exposes its namespaces. SATA SSDs go through AHCI; USB SSDs go through USB
  mass storage and SCSI.
- **HDD and SSD are both first-class.** A SATA HDD and a SATA SSD use the same
  AHCI driver; the block layer may use the rotational hint for scheduling.

This is a vendor-neutral architecture in the sense that upper layers do not
see AHCI, NVMe, or USB registers. It is not uniform: each protocol keeps its
own driver and its own requirements.

## 2. Layering

```
 filesystems / VFS / raw block clients
                 |
         o0 block layer
   bio validation, queue selection, merging,
   partitions, cache policy, error mapping
        /          |             \
   AHCI/ATA      NVMe          SCSI command layer
   protocol      protocol       (USB MSC BOT / UAS)
      |             |                   |
   PCIe RC       PCIe RC          xHCI + USB core
      |             |                   |
   SATA HDD/SSD  NVMe SSD       USB stick / HDD / SSD
```

Two boundaries matter:

1. **Block-to-protocol**: the block layer submits an `o0_bio`; the protocol
   driver translates it into ATA, NVMe, or SCSI commands. The block layer
   never writes a command list entry, a doorbell, or a TRB.
2. **Protocol-to-platform**: the protocol driver uses A1's MMIO, DMA, IRQ,
   discovery, and completion interfaces. It never walks page tables or
   programs an IOMMU directly. See
   [`04-driver-interface-draft.md`](04-driver-interface-draft.md).

## 3. Device-independent block API

The draft header is [`include/ozero/block.h`](../include/ozero/block.h). It is
intentionally small: enough to express queueing, cache semantics, discard,
write zeroes, removable media, and later zoned operations, without encoding
AHCI, NVMe, or USB details.

### 3.1 Objects

| Object | Owner | Purpose |
|---|---|---|
| `o0_block_dev` | block layer | One logical block device: name, geometry, features, queues, protocol ops |
| `o0_block_queue` | block layer | One hardware queue: id, depth, `queue_rq`, `poll`, `timeout` |
| `o0_bio` | caller / block layer | Block request: embeds one `o0_io_request`, plus operation, LBA, vectors, flags, completion |
| `o0_block_ops` | protocol driver | Block-level reset policy, flush, discard, write zeroes, open/close, geometry |
| `o0_block_queue_ops` | protocol driver | Per-queue submission, polling, timeout |

A device may have one queue (USB BOT) or many (NVMe). The block layer must
not assume a queue depth greater than one. The protocol driver must not
assume that the block layer has already merged or aligned requests.

### 3.2 Submission and completion

```
o0_block_submit_bio(dev, bio)
    -> validate op, LBA, vectors, alignment
    -> choose queue (bio->queue_id or block-layer policy)
    -> queue->ops->queue_rq(queue, bio)
         -> driver allocates DMA buffers and generic requests
         -> fills bio->io (one contiguous buffer per request)
         -> calls o0_io_submit(dev, &bio->io)
         -> returns O0_OK if accepted, O0_EAGAIN if busy
    -> completion (IRQ, poll, or process context)
         -> bio->io.complete trampoline
              -> o0_block_complete_bio(bio, status, bytes_done)
                   -> calls bio->end_io exactly once
```

Rules:

- `queue_rq` must not sleep. It may allocate from a preallocated pool or
  return `O0_EAGAIN`.
- `bio->io` is the single A1 generic request object. There is no SG pointer in
  it; a transfer larger than `max_segment` becomes multiple buffers and
  multiple requests. `bio->io.complete` is the only A1 completion callback;
  its trampoline calls `o0_block_complete_bio`.
- A bio accepted by `queue_rq` must complete exactly once, even on reset or
  timeout. The driver owns the bio until it calls
  `o0_block_complete_bio`.
- `poll` is optional; it may be called with a budget and must not block.
- `timeout` is a diagnostic and recovery hook. It must not free the bio
  while the hardware may still DMA into it.
- The block layer must quiesce a queue before unregistering the device.

### 3.3 Operations and semantics

| Operation | Completion means | Ordering and durability |
|---|---|---|
| `O0_BLOCK_READ` | Data is in the bio vectors | Reads observe prior completed writes to the same LBA |
| `O0_BLOCK_WRITE` | Data has been accepted by the device | Not necessarily durable unless `O0_BIO_F_FUA` or a later flush completes |
| `O0_BLOCK_FLUSH` | All prior writes are durable | Must be issued after the writes it covers; completion is the durability point |
| `O0_BLOCK_DISCARD` | The range may be deallocated | Advisory; it may not read back as zero; never a security erase |
| `O0_BLOCK_WRITE_ZEROES` | The range reads back as zero | If `no_unmap` is false, the device may deallocate; if true, it must not |
| `O0_BLOCK_ZONE_*` | Zone state transition | Reserved for a later zoned-storage milestone |

`O0_BIO_F_FUA` means "this write must be durable when it completes". If the
device has no FUA equivalent, the block layer may emulate it with a write
followed by a flush. That is a policy decision and must be visible in
telemetry; it is not free.

`O0_BLOCK_F_FLUSH` advertises that the device has a volatile write cache and
supports a flush command. If the device has no volatile cache, the block layer
may treat flush as a no-op, but it must not claim durability it cannot provide.

`O0_BLOCK_F_DISCARD` and `O0_BLOCK_F_WRITE_ZEROES` are advertised only when the
protocol and device support them. If they are absent, the block layer may
emulate write zeroes by writing zeroes, and may ignore discard. It must not
silently turn discard into a zeroing operation.

### 3.4 Geometry and media

`o0_block_geometry` carries:

- logical and physical block size (512, 512e, 4Kn, 4K);
- number of logical sectors;
- maximum hardware sectors per request;
- maximum segments and segment size;
- DMA alignment and boundary constraints;
- queue depth;
- feature bits, including `ROTATIONAL`, `REMOVABLE`, `NCQ`, `POLL`, and
  `METADATA`.

The block layer uses logical block size for LBA arithmetic. It uses physical
block size and alignment for splitting and merging. It must not assume 512-byte
sectors. A 4Kn device is not a 512e device with a different label.

`REMOVABLE` means the media may change. The block layer must invalidate
partitions and caches on media change, and must serialize open/close against
removal. A USB stick is removable; an NVMe namespace is normally not.

### 3.5 Error model

| Block status | Meaning | Typical protocol cause |
|---|---|---|
| `O0_BLOCK_OK` | Completed successfully | Good completion |
| `O0_BLOCK_ERR_IO` | Generic I/O failure | Unspecified command failure |
| `O0_BLOCK_ERR_TIMEOUT` | Command did not complete in time | Lost interrupt, stuck queue, link hang |
| `O0_BLOCK_ERR_MEDIUM` | Media error | Uncorrectable read, write fault, grown defect |
| `O0_BLOCK_ERR_TRANSPORT` | Transport/link error | SATA link error, PCIe AER, USB stall |
| `O0_BLOCK_ERR_RESET` | Device or queue must be reset | Controller hang, fatal protocol error |
| `O0_BLOCK_ERR_INVAL` | Invalid request | Bad LBA, vector, alignment, or op |
| `O0_BLOCK_ERR_NOTSUP` | Operation not supported | Discard/write zeroes/FUA absent |
| `O0_BLOCK_ERR_AGAIN` | Temporarily busy | Queue full, tag exhaustion |

The protocol driver should preserve detail in its own log or trace record,
but the block layer sees this stable set. Retry policy belongs to the block
layer or a policy module, not to the protocol driver's command builder.

### 3.6 Queue model

- `nr_queues` and per-queue depth are device properties.
- The block layer may map CPUs to queues, but must tolerate fewer queues than
  CPUs.
- A protocol driver may complete a bio from hard IRQ, threaded IRQ, poll, or
  process context. The completion path must be safe in all of them.
- A queue reset must fail or requeue all outstanding bios exactly once.
- Tag allocation is per queue. The driver may store its tag in the bio's
  driver-private field; the block layer does not interpret it.

### 3.7 DMA and memory

The block layer passes page-based vectors. The protocol driver uses A1's
signed Stage 1 DMA surface:

- `o0_device_dma_constraints` for the device's `addr_mask`, alignment and
  `max_segment`;
- `o0_dma_alloc` for command lists, doorbell-adjacent structures, status
  blocks and data buffers;
- `o0_dma_sync` for non-coherent buffers, before and after device access;
- `o0_dma_free` only after `o0_device_dma_quiesce`.

There is no IOMMU domain and no SG map object in Stage 1. There is also no SG
pointer in `o0_io_request`. A bio larger than `max_segment` is split into
multiple `o0_dma_buffer` objects and multiple generic requests; the protocol
driver may build its own transport SG list internally. The driver must respect
`max_segments`, `max_segment_size`, `dma_alignment`, `addr_mask` and any
32-bit boundary. A1 has no bounce-buffer path in the signed Stage 1 surface;
a device that cannot address a buffer must fail the request, not silently
truncate it. The driver must not pass a CPU virtual address to hardware.

## 4. Protocol boundaries

### 4.1 AHCI / SATA

The AHCI driver is a SATA controller driver. It is responsible for:

- mapping the AHCI BAR (ABAR, PCI configuration offset `0x24`, normally BAR5)
  and reading `CAP`, `GHC`, `PI`, `VS`, and per-port registers;
- enabling AHCI (`GHC.AE`) and performing an HBA reset when required;
- enumerating implemented ports from `PI`;
- allocating the command list (`PxCLB`/`PxCLBU`) and received-FIS area
  (`PxFB`/`PxFBU`) in memory the device can DMA to;
- setting `PxCMD.FRE` before `PxCMD.ST`, and clearing both before changing
  list pointers;
- building a command list entry (CFIS, ACMD, PRDT length, command table
  address), a command table, and a physical region descriptor table;
- issuing commands through `PxCI`, and using `PxSACT` for NCQ;
- handling `PxIS` completion and error bits, `PxTFD`, `PxSERR`, and task-file
  error recovery;
- handling hot-plug (`PxIS.PCS`, `PxSSTS.DET`) and link power management when
  enabled;
- translating block operations into ATA commands, including 48-bit LBA,
  `FLUSH CACHE EXT`, and `DATA SET MANAGEMENT` (TRIM) when supported.

AHCI details are defined in AHCI 1.3.1: the command list has up to 32 slots
(§1.6.2, §4.2.2), the command table and PRDT are in §4.2.3 and §4.2.3.3,
`PxCMD.ST` and `PxCMD.FRE` sequencing is in §10.3.1 and §10.3.2, and the
registers are in §3.3. NCQ uses the `PxSACT` and `PxCI` registers (§3.3.13,
§3.3.14).

The AHCI driver must not:

- parse a filesystem or know whether the media is rotating;
- expose raw AHCI registers to the block layer;
- handle NVMe or USB commands;
- assume that a port's device is an HDD or an SSD;
- assume that a port multiplier is present.

### 4.2 NVMe

The NVMe driver is a PCI Express function driver. It is responsible for:

- mapping BAR0/1 and reading the controller registers (`CAP`, `VS`, `CC`,
  `CSTS`, `AQA`, `ASQ`, `ACQ`, and the doorbell region);
- resetting the controller (`CC.EN = 0`, wait for `CSTS.RDY = 0`; then
  `CC.EN = 1`, wait for `CSTS.RDY = 1`);
- creating the admin submission and completion queues;
- issuing Identify Controller, Identify Namespace, and Active Namespace List
  commands;
- creating I/O submission/completion queue pairs and programming their
  doorbells;
- translating block reads, writes, flushes, dataset management, and write
  zeroes into NVM command set commands;
- building PRP lists or SGLs for the data transfer;
- handling completion queue entries, the phase bit, status codes, and
  asynchronous events;
- allocating MSI-X vectors and mapping them to queues;
- supporting controller shutdown (`CC.SHN`) and reset;
- exposing one block device per namespace, with the namespace's logical block
  size and capacity.

The NVMe Base 2.4 specification defines the controller architecture (§3),
data structures (§4), admin command set (§5), and NVM command set (§6); the
NVMe over PCIe Transport 2.4 specification defines the PCIe binding. QEMU
implements NVMe 1.4, so the Stage 0 driver must be 1.4-compatible and
negotiate newer features rather than requiring them.

The NVMe driver must not:

- bind to a SATA SSD or a USB SSD;
- assume one namespace per controller;
- assume 512-byte logical blocks;
- assume that every namespace uses the NVM command set;
- use a single admin queue for I/O;
- ignore the completion phase bit or status code.

### 4.3 USB mass storage

USB storage is a stack, not a single driver:

1. **xHCI host controller driver**: owns the PCI function, MMIO registers,
   command ring, event ring, transfer rings, TRBs, doorbells, port state, and
   MSI-X. It exposes USB devices and pipes to the USB core.
2. **USB core**: enumerates devices, reads descriptors, assigns addresses,
   selects configurations, and manages interfaces and endpoints.
3. **Mass-storage class driver**: binds to interface class 0x08, subclass
   0x06, and protocol 0x50 (BOT) or 0x62 (UAS). It implements CBW/CSW
   (BOT) or the UAS command/status/data pipes over USB streams, and
   translates SCSI CDBs.
4. **SCSI command layer**: provides SCSI host/device/command objects, sense
   data, and error handling.
5. **SCSI disk driver**: registers the block device, handles partitions, and
   translates block bios into SCSI READ/WRITE/SYNCHRONIZE CACHE/UNMAP.

BOT 1.0 uses a 31-byte Command Block Wrapper (CBW) and a 13-byte Command
Status Wrapper (CSW). It allows one command per LUN at a time and a maximum
16-byte CDB. Reset recovery uses the Bulk-Only Mass Storage Reset class
request and Clear Feature(HALT). UAS 1.0 adds command queueing and USB
streams, but it is optional; a USB 3 enclosure may still be BOT.

The USB stack must not:

- register a block device directly from the xHCI driver;
- assume BOT supports queue depth greater than one;
- treat the USB VID/PID as the media type;
- ignore SCSI sense data;
- assume UAS is present because the link is USB 3.

## 5. Cross-protocol feature mapping

| Block feature | AHCI / SATA | NVMe | USB BOT | USB UAS |
|---|---|---|---|---|
| Queue depth | Up to 32 tags with NCQ | Up to 64K entries per queue pair | 1 command per LUN | Multiple commands via streams |
| Flush | `FLUSH CACHE EXT` | Flush command | SCSI `SYNCHRONIZE CACHE` | SCSI `SYNCHRONIZE CACHE` |
| FUA | ATA FUA / write cache controls | FUA bit in write command | SCSI FUA bit | SCSI FUA bit |
| Discard | `DATA SET MANAGEMENT` (TRIM) | Dataset Management | SCSI `UNMAP` | SCSI `UNMAP` |
| Write zeroes | ATA write zeroes / write same | Write Zeroes | SCSI `WRITE SAME` | SCSI `WRITE SAME` |
| Removable | Possible (bay) | Normally no | Yes | Yes |
| Sector sizes | 512, 512e, 4Kn | 512, 4K (namespace format) | 512, 4K | 512, 4K |
| Error detail | Task file, `PxSERR`, NCQ log | Completion status, status code | SCSI sense | SCSI sense |
| Reset | HBA/port reset | Controller reset, queue reset | BOT reset, Clear Halt | UAS task management, reset |
| Hot-plug | Yes (PxIS.PCS) | Optional | Yes | Yes |
| Power | ALPM, DevSleep | APST, D-states | USB suspend/resume | USB suspend/resume |

This table is the reason a single "storage driver" cannot be correct. The
block layer can expose one feature set, but the protocol driver must implement
the feature with the right command and the right error path.

## 6. Cross-agent boundaries

### 6.1 A1 — platform

A3 consumes A1's contracts through
[`include/ozero/platform.h`](../include/ozero/platform.h), which includes A1's
`<ozero/driver.h>`, `<ozero/mm.h>` and `<ozero/irq.h>`: bus-neutral device
enumeration, BAR mapping, DMA constraints/allocation/sync/free, GSI/vector
allocation, bus mastering, I/O limits, quiesce, reset and generic asynchronous
I/O.

Stage 1 does **not** provide an IOMMU domain, PCI config-space access, ACPI/UEFI
table access, completion fences, runtime power management or hotplug callbacks.

A3 storage drivers rely on A1 for:

- a dedicated UC/NX BAR mapping; never an HHDM address;
- physical, untranslated DMA addresses and a constraint-driven allocator
  (trusted ring-0 only);
- GSI/vector allocation and handler registration;
- `o0_device_dma_quiesce` and `o0_device_reset` with no in-flight DMA after
  quiesce;
- `o0_io_submit` / `o0_io_cancel` and the A3 `struct o0_device_ops` callbacks.

### 6.2 A2 — Windows graphics and storage API requirements

A2 owns the Windows-facing API requirements. A2 should express them as block
layer features (flush, FUA, discard, write zeroes, removable, queue depth,
sector size) and not as AHCI/NVMe/USB register accesses. If a Windows API
requires a capability the hardware lacks, A2 and A3 must agree on emulation
and its cost.

### 6.3 A4 — verification

A4 owns QEMU automation, real-hardware test records, and regression tracking.
A3 provides protocol-specific test hooks and a deterministic way to inject
timeouts, aborts, and resets. The test plan is in
[`05-hardware-test-plan.md`](05-hardware-test-plan.md).

## 7. A3 design choices and open questions (not global decisions)

| ID | Question | Recommendation | Why |
|---|---|---|---|
| A3-C1 | Native block drivers or a universal SCSI layer? | Native AHCI and NVMe block drivers; SCSI layer only for USB MSC/UAS | AHCI is ATA and NVMe is NVMe; forcing both through SCSI adds translation and hides protocol errors. USB MSC/UAS genuinely are SCSI. |
| A3-C2 | Support 4Kn? | Yes, at the block layer and in AHCI/NVMe | 4Kn changes LBA arithmetic and alignment; a 512-only assumption will fail on real devices. |
| A3-C3 | Advertise discard by default? | Only when the device supports it; otherwise ignore | Discard is advisory and not a zeroing operation; silent emulation is misleading. |
| A3-C4 | Write cache default? | Expose `FLUSH`/`FUA`; default to write-back only with a visible policy | Durability must be explicit; a write completion is not a durability guarantee. |
| A3-C5 | Queue depth assumptions? | Per-protocol: AHCI 32, NVMe 64+, BOT 1, UAS multiple | The block layer must not assume parallelism that the transport cannot provide. |
| A3-C6 | NVMe baseline version? | 1.4-compatible core; negotiate 2.4 features only when present | Matrix pins NVMe 2.4; QEMU is 1.4. Requiring 2.4 would make QEMU useless. |
| A3-C7 | Hot-plug scope? | AHCI and USB first; NVMe optional | Hot-plug is a property of the bay/transport, not the media. |
| A3-C8 | Legacy ATA? | Best effort, no hot-plug promise | Hardware availability and obsolete command sets make it a poor first target. |
| A3-C9 | Metadata / protection information? | Later milestone | It changes the data path and namespace format; not needed for first boot. |
| A3-C10 | Zoned storage? | Later milestone; keep zone opcodes reserved | The block API has hooks, but no Stage 0 driver. |

## 8. Test hooks and acceptance

The storage architecture is accepted when:

- a SATA HDD and a SATA SSD both boot and pass `fio` on the same AHCI driver;
- an NVMe namespace boots and passes `fio` on a real NVMe controller;
- a USB BOT stick and a UAS enclosure enumerate, mount, and survive a
  hot-plug/reset cycle;
- flush and FUA ordering is verified with a power-loss or fault-injection
  test;
- discard and write zeroes return `NOTSUP` when unsupported, and behave as
  specified when supported;
- a queue reset completes or requeues every outstanding bio exactly once;
- 512e and 4Kn devices both produce correct LBA arithmetic.

The detailed commands, device IDs, and pass/fail records are in
[`05-hardware-test-plan.md`](05-hardware-test-plan.md).

## 9. References

Primary specifications:

- Intel, *Serial ATA AHCI 1.3.1 Specification*.
  <https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf>
- NVM Express, *NVMe Base Specification 2.4* and *NVMe over PCIe Transport
  Specification 2.4*.
  <https://nvmexpress.org/specifications/>
- USB Implementers Forum, *USB Mass Storage Class Bulk-Only Transport 1.0*.
  <https://www.usb.org/document-library/mass-storage-bulk-only-10>
- USB Implementers Forum, *USB Attached SCSI Protocol (UASP) 1.0*.
  <https://www.usb.org/document-library/usb-attached-scsi-protocol-uasp-v10-and-adopters-agreement>
- USB Implementers Forum, *USB 2.0 Specification*.
  <https://www.usb.org/document-library/usb-20-specification>
- USB Implementers Forum, *USB 3.2 Specification*.
  <https://www.usb.org/document-library/usb-32-specification-released-september-22-2017-and-ecns>
- Intel, *Extensible Host Controller Interface for USB (xHCI) 1.2*.
  <https://www.intel.com/content/www/us/en/technical-specifications/extensible-host-controler-interface-usb-xhci.html>
- PCI-SIG, *PCI Express Base Specification*.
  <https://pcisig.com/specifications>
- T10, *SCSI Block Commands (SBC)*.
  <https://www.t10.org/>
- T13, *ATA/ATAPI Command Set (ACS)*.
  <https://www.t13.org/>

Implementation references:

- Linux kernel, *Block layer*.
  <https://docs.kernel.org/block/index.html>
- Linux kernel, *libata*.
  <https://docs.kernel.org/driver-api/libata.html>
- Linux kernel, *NVMe*.
  <https://docs.kernel.org/nvme/index.html>
- Linux kernel, *USB mass storage*.
  <https://docs.kernel.org/usb/mass-storage.html>
- Linux kernel, *PCI*.
  <https://docs.kernel.org/PCI/index.html>
- QEMU, *NVMe Emulation*.
  <https://www.qemu.org/docs/master/system/devices/nvme.html>
- QEMU, *USB Devices*.
  <https://www.qemu.org/docs/master/system/devices/usb.html>
