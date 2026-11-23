# Ozero A3 — Hardware Support Matrix

Status: **Stage 0 desk artifact; no implementation or hardware validation**

Scope: Stage 0 storage and Haswell Gen7.5 GPU support planning
Machine-readable source: [`matrix/hardware-support-matrix.csv`](../matrix/hardware-support-matrix.csv)

## 1. Purpose

This matrix records what Ozero intends to support, what protocol each device
speaks, which specification defines the behaviour, how the claim will be
tested, and what is still missing. It is a planning artifact, not a claim that
a driver exists.

The matrix deliberately separates four things that are often conflated:

1. **Media**: rotating magnetic disk, NAND flash, optical, virtual.
2. **Interface / transport**: SATA, PCI Express, USB, IDE/PATA, virtio.
3. **Protocol / command set**: AHCI, NVMe, USB MSC BOT, UAS, legacy ATA,
   SCSI.
4. **Form factor**: 2.5-inch, 3.5-inch, M.2, U.2, EDSFF, USB stick, enclosure.

A device is supported because its **controller and protocol** are supported,
not because its marketing category is supported. An SSD can be SATA, NVMe, or
USB; an HDD can be SATA, USB, or legacy PATA. The block layer may use the
rotational hint for scheduling, but the driver binds to the controller.

Form factor is not a matrix column because it does not change the
controller/protocol contract. It matters for enclosure, connector, power, and
thermal validation, which are separate hardware-integration checks.

## 2. Storage support matrix

The CSV is the canonical record. The summary table keeps the product cases
readable; the evidence table records the review fields that must exist for
every row.

### 2.1 Product cases and driver families

| ID | Driver family | Media | Interface | Protocol | Target | Implementation status |
|---|---|---|---|---|---|---|
| `hdd-sata-ahci` | `ahci` | HDD, rotational, 512e/4Kn | SATA | AHCI 1.3.1 + ATA/ATAPI | First-class boot/data | not-started |
| `ssd-sata-ahci` | `ahci` | SSD, NAND flash | SATA | AHCI 1.3.1 + ATA/ATAPI | First-class boot/data | not-started |
| `ssd-nvme-pcie` | `nvme` | SSD, NAND flash | PCI Express | NVMe Base 2.4 + NVMe over PCIe 2.4 | First-class boot/data | not-started |
| `usb-msc-bot` | `usb-bot` | Flash, HDD, or SSD media | USB | USB MSC Bulk-Only Transport 1.0 over xHCI | Removable data | not-started |
| `usb-uas` | `usb-uas` | SSD or HDD media | USB | UAS 1.0 over xHCI | Removable data | not-started |
| `hdd-legacy-ata` | `ata-legacy` | HDD, rotational | IDE/PATA | Legacy ATA/ATAPI | Best effort | not-started |
| `sata-port-multiplier` | `ahci-pm` | HDD or SSD media | SATA | AHCI 1.3.1 + port multiplier | Deferred | deferred |
| `nvme-zns` | `nvme-zns` | Zoned SSD, NAND flash | PCI Express | NVMe ZNS command set | Deferred | deferred |
| `virtio-blk` | `virtio-blk` | Virtual disk | virtio-pci | Virtio 1.2 | Test-only | not-started |

The HDD and SSD SATA rows intentionally share the `ahci` driver family. They
are separate product cases, not separate drivers. NVMe, BOT, UAS, and legacy
ATA remain separate driver families.

### 2.2 Per-row evidence

| ID | Pinned specification revision | Emulator availability | Real hardware | Acceptance test |
|---|---|---|---|---|
| `hdd-sata-ahci` | AHCI 1.3.1; ATA/ATAPI ACS-5; Serial ATA Revision 3.5 | QEMU q35 `ich9-ahci` (8086:2922); no NCQ timing fidelity | Haswell PCH AHCI 8086:8c02/8c03 + rotational SATA disk | L1/L2: identify, read/write, flush, NCQ, hotplug; fio, smartctl, blktrace |
| `ssd-sata-ahci` | AHCI 1.3.1; ATA/ATAPI ACS-5; Serial ATA Revision 3.5 | QEMU q35 `ich9-ahci`; SSD-backed raw image | Haswell PCH AHCI + SATA SSD | L1/L2: identify, read/write, flush, TRIM, NCQ; fio, blkdiscard, smartctl |
| `ssd-nvme-pcie` | NVMe Base Specification 2.4 (2026-08-04); NVMe over PCIe Transport 2.4 (2026-08-04); PCI Express Base 3.0 | QEMU `nvme` (1b36:0010), NVMe 1.4 only | Haswell PCIe 3.0 + NVMe SSD | L1/L2: identify, namespace I/O, flush, queue depth, reset; nvme-cli, fio |
| `usb-msc-bot` | USB 2.0; USB 3.2; USB MSC BOT 1.0; xHCI 1.2; SCSI SBC-4 | QEMU `qemu-xhci` (1b36:000d) + `usb-storage` | Haswell xHCI 8086:8c31 + USB 2.0/3.0 stick | L1/L2: enumerate, read/write, flush, reset recovery, hotplug; usb-devices, lsblk, fio |
| `usb-uas` | UAS 1.0; USB 3.2; xHCI 1.2; SCSI SBC-4 | QEMU `qemu-xhci` + `usb-uas` + `scsi-hd` | Haswell xHCI + UAS enclosure | L1/L2: enumerate, queue depth, read/write, task management, reset; fio, hotplug |
| `hdd-legacy-ata` | ATA/ATAPI ACS-5; PCI IDE Controller Specification 1.0 | QEMU `piix3`/`piix4` IDE | Haswell PCH IDE or add-in PCI IDE + PATA disk (if available) | L1/L2: identify, PIO/DMA read/write, flush; smartctl |
| `sata-port-multiplier` | AHCI 1.3.1 section 9; Serial ATA Revision 3.5 port multiplier | none | SATA port multiplier (only if required) | Deferred until a concrete requirement exists |
| `nvme-zns` | NVMe ZNS Command Set 1.1; NVMe Base Specification 2.4 | QEMU `nvme-ns zoned=on` (version-dependent) | ZNS SSD (only if required) | Deferred until a concrete requirement exists |
| `virtio-blk` | Virtual I/O Device (VIRTIO) 1.2 | QEMU `virtio-blk-pci` (1af4:1001/1042) | Not hardware; QEMU only | L1: block API regression; fio |

### 2.3 Haswell GPU row

The GPU is tracked separately because it is not a storage device and because
QEMU does not provide a faithful Haswell i915 model.

| Field | Value |
|---|---|
| ID | `gpu-haswell-gen75` |
| Driver family | `i915` |
| Device | Haswell GT1/GT2/GT3/GT3e integrated GPU |
| Interface / programming model | PCI Express + MMIO; Gen7.5 command stream |
| Target | Stage 5 research |
| Pinned specification revision | Intel Haswell PRM Volumes 1-5 (2013-2014); Linux i915 v6.6 |
| Implementation status | not-started |
| Emulator availability | none; no faithful Haswell i915 model |
| Real hardware | Haswell GT2 and GT3e |
| Acceptance test | Stage 5 gates 5A-5E; IGT on GT2/GT3e |
| Principal gaps | eDRAM, 31-bit aliasing PPGTT, no execlists, no 64-bit relocation |

There is **no GPU support claim** until the Stage 5 gates pass on real
Haswell hardware or a clearly scoped emulator. QEMU is not that emulator.

See [`03-haswell-gen75-gpu.md`](03-haswell-gen75-gpu.md) for the detailed
analysis.

## 3. Why each row is separate

### 3.1 HDD and SSD are both first-class

The brief requires HDD and SSD support as first-class goals. The matrix keeps
two SATA rows so that a SATA HDD and a SATA SSD are both tested on the same
AHCI driver. The block layer learns `rotational` from the device (for example,
`ROTA` in Linux sysfs) and may use it for scheduling, but the AHCI driver does
not contain an "HDD path" and an "SSD path". Both use the same command set.

### 3.2 NVMe is not a synonym for SSD

NVMe is a controller interface and command set over PCI Express (or another
transport). A SATA SSD is an AHCI device. A USB SSD is a USB mass-storage
device. The NVMe driver must bind to an NVMe controller, enumerate its
namespaces, and expose block devices for those namespaces. It must not claim
arbitrary SSDs.

### 3.3 USB is a transport, not a media type

A USB stick, a USB HDD, and a USB SSD all use USB mass storage. The class
driver chooses Bulk-Only Transport (BOT) or UAS based on the interface
protocol. The media type is discovered from SCSI INQUIRY, READ CAPACITY, and
related commands, not from the USB VID/PID alone.

### 3.4 Legacy ATA is best effort

Legacy PATA is retained where practical because Haswell-era boards and test
fixtures may still expose it. It is not a promise of full support, hotplug, or
modern queueing.

## 4. Representative device IDs

These IDs are for test discovery and sanity checks. Real hardware must be
enumerated at runtime; do not hard-code a device list as the support policy.

| Device | PCI ID or class | Notes |
|---|---|---|
| QEMU q35 AHCI | `8086:2922` | ICH9R/IO/IH 6-port SATA AHCI; QEMU `ich9-ahci` |
| QEMU NVMe | `1b36:0010` | QEMU NVM Express Controller; QEMU implements NVMe 1.4 |
| QEMU xHCI | `1b36:000d` | QEMU XHCI Host Controller |
| QEMU virtio-blk | `1af4:1001`, `1af4:1042` | Legacy and Virtio 1.0 block devices |
| Haswell PCH SATA AHCI | `8086:8c02`, `8086:8c03` | 8 Series/C220 6-port SATA AHCI |
| Haswell PCH xHCI | `8086:8c31` | 8 Series/C220 USB xHCI |
| Haswell PCH EHCI | `8086:8c26` | 8 Series/C220 USB EHCI #1 |
| Haswell GT2 GPU | `8086:0412` | Desktop GT2 example |
| Haswell ULT GT3 GPU | `8086:0a26` | Mobile GT3 example |
| Haswell CRW GT3 GPU | `8086:0d26` | Crystal Well GT3 example |
| AHCI class | class `0x0106`, prog-if `0x01` | SATA AHCI controller |
| NVMe class | class `0x0108` | NVM Express controller; confirm prog-if in the NVMe spec |
| USB controller class | class `0x0c03` | prog-if `0x30` xHCI, `0x20` EHCI, `0x00` UHCI, `0x10` OHCI |

The Haswell GPU IDs are examples, not a complete SKU list. Use
`lspci -nn` and the i915 PCI ID tables to confirm the exact SKU. The Linux
i915 source is the reference for the ID-to-platform mapping.

## 5. Out of scope for Stage 0

The following are deliberately excluded from the first support claim:

- eMMC, SD, UFS, and other mobile flash controllers.
- Optical/ATAPI media and tape.
- Hardware RAID volume managers and proprietary RAID metadata.
- SAN, iSCSI, NVMe over Fabrics, and network block devices.
- Thunderbolt/USB4 storage tunnels.
- Hot-plug bays whose firmware requires vendor-specific ACPI methods.
- Any GPU family other than Haswell Gen7.5.

These can be added later using the same matrix schema, with their own
specification and test vehicle.

## 6. Acceptance criteria for this matrix

- Every row pins an exact specification revision, an implementation status,
  an emulator, real hardware, and an acceptance test.
- Every required storage family (HDD/SATA, SSD/SATA, SSD/NVMe, USB BOT,
  USB UAS) is present.
- Media, interface, protocol, and form factor are separate fields. No row
  treats USB, SATA, NVMe, or PCIe as a media type.
- The SATA HDD and SATA SSD rows share the `ahci` driver family but remain
  separate product cases.
- NVMe, USB BOT, USB UAS, and legacy ATA remain distinct driver families.
- ZNS and SATA port multiplier are `deferred` until a concrete requirement
  exists.
- Haswell Gen7.5 is documented separately from Gen8+.
- The GPU row is `not-started` and makes no support claim.
- Implementation status reflects code and test evidence, not ambition.

The validator in [`tools/validate_matrix.py`](../tools/validate_matrix.py)
enforces these criteria. The matrix semantics tests in
[`tests/test_matrix_semantics.py`](../tests/test_matrix_semantics.py) add
direct assertions for the review fields.

## 7. References

Primary specifications:

- Intel, *Serial ATA Advanced Host Controller Interface (AHCI) 1.3.1*.
  <https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf>
  (archived copy: <https://web.archive.org/web/20140717053355/http://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf>)
- NVM Express, *NVMe Base Specification 2.4* and *NVMe over PCIe Transport
  Specification 2.4*, released 4 August 2026.
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
- SATA-IO, *Serial ATA Specification*.
  <https://sata-io.org/specifications>
- T13, *ATA/ATAPI Command Set (ACS)*.
  <https://www.t13.org/>

Implementation and test references:

- Linux kernel, *Block layer* documentation.
  <https://docs.kernel.org/block/index.html>
- Linux kernel, *libata* documentation.
  <https://docs.kernel.org/driver-api/libata.html>
- Linux kernel, *NVMe* documentation.
  <https://docs.kernel.org/nvme/index.html>
- Linux kernel, *USB mass storage* documentation.
  <https://docs.kernel.org/usb/mass-storage.html>
- QEMU, *NVMe Emulation*.
  <https://www.qemu.org/docs/master/system/devices/nvme.html>
- QEMU, *USB Devices*.
  <https://www.qemu.org/docs/master/system/devices/usb.html>
- QEMU, *Block drivers*.
  <https://www.qemu.org/docs/master/system/qemu-block-drivers.html>
- PCI ID repository, used to verify the IDs above.
  <https://pci-ids.ucw.cz/>
