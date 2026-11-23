# XB-005 A3 hardware-evidence gap and safe test plan

| Field | Value |
| --- | --- |
| Status | Gap analysis and plan. **XB-005 remains Open.** No hardware probe was run. |
| Owner | A3 (historical), A4 records the consolidated plan |
| Evidence level | L0 static only; no L1, L2, L3 or L4 result exists |
| Gate | D-007 and D-021 must be Approved before any AHCI or DMA probe |
| Stage 0 | **NOT ACCEPTED**; this plan does not change D-006 |
| Date | 2026-11-23 |

## 1. What exists

A3's package is a desk artifact. It contains a 10-row hardware-support matrix, a test plan, A1
headers, interface headers and static validators. The reproducible L0 checks are:

```sh
make -C a3 check
make -C a3 check-a1-headers A1_INCLUDE=include
```

Recorded result: 19 tests pass, matrix validation passes, deliverable check passes, header smoke
compiles. These checks prove the matrix and headers are internally consistent. They do **not**
prove that any driver, device or bus works.

A3's `EVIDENCE.md` records that QEMU was not installed and no Haswell hardware was attached when
the package was written. The current A4 environment has QEMU 10.0.13 (TCG), but that does not create
driver evidence: there is no A3 kernel driver, no bootable A3 test image and no test harness that
issues an AHCI, NVMe, xHCI or virtio request.

## 2. Exact missing evidence

| Matrix row | What exists | What is missing | Why it is not evidence |
| --- | --- | --- | --- |
| `hdd-sata-ahci` | Matrix row, AHCI 1.3.1 citations, header smoke | AHCI driver; QEMU q35 `ich9-ahci` L1 run; identify, read, write, flush, NCQ and hotplug results; Haswell PCH `8086:8c02`/`8c03` L2 run | No code path touches an ABAR, command list, FIS or PRDT |
| `ssd-sata-ahci` | Matrix row, media/transport split | Same AHCI driver evidence, plus TRIM/UNMAP, queue depth and write-cache/FUA results | A matrix row is a plan, not a device result |
| `ssd-nvme-pcie` | Matrix row, NVMe 2.4 citations | NVMe driver; QEMU `nvme` L1 run; namespace I/O, flush, queue depth and reset; real NVMe L2 | QEMU models NVMe 1.4, not the 2.4 reference; no driver exists |
| `usb-msc-bot` | Matrix row, BOT/xHCI citations | xHCI and BOT driver; QEMU `qemu-xhci` + `usb-storage` L1 run; enumerate, read/write, flush, reset recovery and hotplug; real Haswell xHCI L2 | No xHCI ring, TRB or BOT CBW/CSW code exists |
| `usb-uas` | Matrix row, UAS citations | xHCI and UAS driver; QEMU `usb-uas` + `scsi-hd` L1 run; queue depth, task management and reset; real UAS enclosure L2 | UAS is optional and has no implementation |
| `hdd-legacy-ata` | Matrix row, ATA/ATAPI citations | Legacy ATA driver; QEMU piix3/piix4 L1 run; PIO/DMA read/write and flush; real PATA L2 | No legacy ATA code exists |
| `virtio-blk` | Matrix row, Virtio 1.2 citations | Virtio-blk driver; QEMU `virtio-blk-pci` L1 run; block API regression | Test-only vehicle; no driver exists |
| `gpu-haswell-gen75` | Matrix row, Haswell PRM and i915 citations | i915 driver; Stage 5 gates 5A–5E; real Haswell GT2/GT3e IGT results | No faithful QEMU i915 model; no GPU code exists |
| `sata-port-multiplier` | Deferred row | A concrete requirement before any work | Deferred by the Stage 0 review |
| `nvme-zns` | Deferred row | A concrete requirement before any work | Deferred by the Stage 0 review |

The missing evidence is therefore not a missing paragraph. It is a missing **driver, test image,
execution record and, for L2, hardware**.

## 3. Prerequisites and safety gates

| Gate | Current state | What it unlocks |
| --- | --- | --- |
| D-005 boot protocol | **Proposed**; package at [`D-005-boot-protocol-package.md`](../decisions/D-005-boot-protocol-package.md) | A bootable A3 test image and a QEMU boot path |
| D-007 IF-008 v1.1 and A1 sign-off gate | **Proposed**; A1 ACCEPT at `b9a69d8`; A3 accepted §6 at `307b919` | Any AHCI/DMA/platform implementation |
| D-021 IOMMU policy | **Proposed**; trusted ring-0 only in Stage 1 | Any DMA mapping or device-master enablement |
| A1/A3 v1.1 signatures | A1/A3 review pending for IF-009 v1.1; IF-008 already accepted by A1 | Interface stability for the test image |
| Hardware access | No Haswell machine attached | L2 and above |

No AHCI, DMA, GPU or real-hardware probing may start while D-007 or D-021 is Proposed. Static
checks and documentation work are allowed.

## 4. Safe test plan

### Phase 0 — L0 static baseline (complete)

- `make -C a3 check`
- `make -C a3 check-a1-headers A1_INCLUDE=include`
- Record the commit, tool versions, raw output and exit codes.

### Phase 1 — Preconditions (not started)

1. Director records D-005, D-007 and D-021.
2. A1 confirms the boot contract and the kernel entry state for the test image.
3. A3 confirms IF-008 v1.1 and IF-009 v1.1 signatures.
4. A4 adds a separate `a3-qemu-l1` target; it must not be labelled `build-and-boot` or
   `mbr-harness-smoke`.
5. The test image uses a disposable disk image and no host block device.

### Phase 2 — QEMU L1 functional tests (blocked)

After Phase 1, use the A3 test plan's QEMU recipes with disposable images. The minimum sequence is:

| Test | QEMU device | First assertion | Safety rule |
| --- | --- | --- | --- |
| `ST-AHCI-HDD` | q35 `ich9-ahci` + raw HDD image | Controller `8086:2922`; identify; read-only sector 0 | No host disk; no write until identify passes |
| `ST-AHCI-SSD` | q35 `ich9-ahci` + raw SSD image | Identify; read/write/flush on the image | Disposable image only |
| `ST-NVME` | `-device nvme` | Controller `1b36:0010`; namespace identify; read-only I/O | QEMU 1.4, not 2.4 |
| `ST-USB-BOT` | `qemu-xhci` + `usb-storage` | Enumerate; read-only sector | No host USB passthrough |
| `ST-USB-UAS` | `qemu-xhci` + `usb-uas` + `scsi-hd` | Enumerate; queue depth; read-only I/O | No host USB passthrough |
| `ST-VIRTIO` | `virtio-blk-pci` | Block API read/write regression | Test-only, not hardware evidence |

Each test record must include: QEMU version, machine type, CPU model, memory size, kernel command
line, device IDs, image hash, exact command, raw serial log, pass/fail and the commit.

### Phase 3 — Real-hardware L2 smoke (not started)

Required hardware: Haswell PCH SATA AHCI `8086:8c02`/`8c03`, Haswell xHCI `8086:8c31`, a SATA
HDD, a SATA SSD, an NVMe SSD on PCIe 3.0, a USB BOT stick, a UAS enclosure, and a Haswell GT2 or
GT3e GPU for Stage 5. Record the inventory before testing. No destructive write test runs before a
read-only identify and a disposable-data confirmation.

### Phase 4 — L3/L4 (not started)

Fault injection, reset recovery, surprise removal, memory pressure, throughput, latency and power.
These require L2 to pass first.

## 5. Evidence format

Every result must carry:

```text
test id:
commit:
host or QEMU:
qemu version / machine / cpu / memory:
kernel command line:
device id and firmware:
image hash:
exact command:
raw log path:
expected:
actual:
pass/fail:
```

A matrix row may move from `not-started` only when the named acceptance test has a raw record.
A compile, a header check or a matrix validation is not a driver test.

## 6. What this plan does not do

- It does not run AHCI, DMA, NVMe, USB, virtio or GPU probing.
- It does not approve D-005, D-007 or D-021.
- It does not close XB-005.
- It does not claim that QEMU 10.0.13 is a Haswell machine.
- It does not make Stage 0 accepted.

## 7. Next measurable milestone

After D-007 and D-021 are Approved, the first measurable milestone is a **read-only QEMU L1 AHCI
identify** on a disposable q35 image, with the full record above. A write, flush, NCQ or hotplug
test follows only after identify passes and the record is reviewed.

## Sources

- A3 [`EVIDENCE.md`](../../a3/EVIDENCE.md), [`docs/05-hardware-test-plan.md`](../../a3/docs/05-hardware-test-plan.md) and [`matrix/hardware-support-matrix.csv`](../../a3/matrix/hardware-support-matrix.csv) at `48d434f`.
- A4 [`cross-agent-blockers.md`](../process/cross-agent-blockers.md) XB-005; [`interfaces.md`](interfaces.md) IF-008.
- A1 IF-008 review at `b9a69d8`; A3 §6 acceptance at `307b919`.
- QEMU documentation for `ich9-ahci`, `nvme`, `qemu-xhci`, `usb-storage`, `usb-uas` and `virtio-blk-pci`.
