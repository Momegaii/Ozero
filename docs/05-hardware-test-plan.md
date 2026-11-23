# Ozero A3 — Hardware Test Plan

Status: **Stage 0 desk artifact; no implementation or hardware validation**

Scope: Stage 0 storage and Haswell Gen7.5 GPU verification
Related: [`01-hardware-support-matrix.md`](01-hardware-support-matrix.md),
[`02-storage-architecture.md`](02-storage-architecture.md),
[`03-haswell-gen75-gpu.md`](03-haswell-gen75-gpu.md),
[`04-driver-interface-draft.md`](04-driver-interface-draft.md)

## 1. Test levels

| Level | Purpose | Environment | Gate |
|---|---|---|---|
| L0 | Static and contract checks | Build host | Matrix schema, header compile, docs completeness |
| L1 | Functional protocol tests | QEMU | Boot, enumerate, read/write, flush, discard, hotplug where modelled |
| L2 | Real-hardware smoke | Haswell GT2/GT3e, SATA HDD/SSD, NVMe, USB | Boot, enumerate, read/write, reset, suspend/resume |
| L3 | Stress and error recovery | QEMU fault injection + real hardware | Timeouts, aborts, resets, surprise removal, memory pressure |
| L4 | Performance and power | Real hardware | Throughput, latency, power states, thermal stability |

No level may be skipped for a "supported" claim. L1 alone is not evidence of
hardware support.

## 2. QEMU environment

### 2.1 Record the environment

```sh
qemu-system-x86_64 --version
uname -a
git rev-parse HEAD
```

Record the QEMU version, machine type, CPU model, memory size, and kernel
command line with every result. QEMU's emulated devices change over time.

### 2.2 Create disposable images

```sh
qemu-img create -f raw ahci-hdd.img 8G
qemu-img create -f raw ahci-ssd.img 8G
qemu-img create -f raw nvme.img 8G
qemu-img create -f raw usb.img 1G
qemu-img create -f raw uas.img 8G
qemu-img create -f raw virtio.img 4G
```

These are disposable. Never point a destructive test at a disk that contains
data you care about.

### 2.3 AHCI HDD

QEMU `q35` includes an ICH9 AHCI controller (`8086:2922`). Attach a
rotational-backed image with `ide-hd`:

```sh
qemu-system-x86_64 \
  -machine q35,accel=kvm:tcg -m 2G -smp 2 \
  -drive if=none,id=hdd0,file=ahci-hdd.img,format=raw \
  -device ide-hd,drive=hdd0 \
  -nographic -no-reboot
```

If QEMU does not auto-place the device, add `bus=ide.0`. Confirm inside the
guest:

```sh
lspci -nn | grep -i sata
lsblk -d -o NAME,ROTA,SIZE,MODEL,TRAN,LOG-SEC
```

Expected: AHCI controller `8086:2922`; the disk is visible; `ROTA=1` is a
media hint. QEMU does not model mechanical seek or rotational latency, so
this test validates the AHCI command path, not HDD performance.

### 2.4 AHCI SATA SSD

Use the same command with `ahci-ssd.img` and a different serial:

```sh
qemu-system-x86_64 \
  -machine q35,accel=kvm:tcg -m 2G -smp 2 \
  -drive if=none,id=ssd0,file=ahci-ssd.img,format=raw \
  -device ide-hd,drive=ssd0 \
  -nographic -no-reboot
```

The emulated device is still an AHCI device. The `ROTA` hint may be `1` in
QEMU unless the image is marked non-rotational; do not use QEMU's `ROTA` as
evidence of SSD support. The real SATA SSD test is the evidence.

### 2.5 NVMe

QEMU's `nvme` device implements NVMe 1.4:

```sh
qemu-system-x86_64 \
  -machine q35,accel=kvm:tcg -m 2G -smp 2 \
  -drive if=none,id=nvm0,file=nvme.img,format=raw \
  -device nvme,serial=O0NVME01,drive=nvm0 \
  -nographic -no-reboot
```

Expected inside the guest:

```sh
lspci -nn | grep -i "non-volatile"
nvme list
nvme id-ctrl /dev/nvme0
nvme id-ns /dev/nvme0n1
```

QEMU reports `1b36:0010`. It is a useful functional target, but it is not
evidence for NVMe 2.4 features. Real hardware is required for that.

### 2.6 USB Bulk-Only Transport (BOT)

```sh
qemu-system-x86_64 \
  -machine q35,accel=kvm:tcg -m 2G -smp 2 \
  -device qemu-xhci,id=xhci \
  -drive if=none,id=usb0,file=usb.img,format=raw \
  -device usb-storage,bus=xhci.0,drive=usb0 \
  -nographic -no-reboot
```

Expected:

```sh
lspci -nn | grep -i usb
lsusb -tv
lsblk -d -o NAME,ROTA,SIZE,MODEL,TRAN
```

The QEMU xHCI controller is `1b36:000d`. The mass-storage device uses BOT.

### 2.7 USB Attached SCSI (UAS)

```sh
qemu-system-x86_64 \
  -machine q35,accel=kvm:tcg -m 2G -smp 2 \
  -device qemu-xhci,id=xhci \
  -device usb-uas,id=uas,bus=xhci.0 \
  -drive if=none,id=u0,file=uas.img,format=raw \
  -device scsi-hd,bus=uas.0,scsi-id=0,lun=0,drive=u0 \
  -nographic -no-reboot
```

Expected: the interface protocol is UAS (0x62), and the SCSI disk is visible.
QEMU does not model USB electrical behaviour, PHY training, or real enclosure
firmware. Real UAS hardware is required for a support claim.

### 2.8 virtio-blk (block API regression)

```sh
qemu-system-x86_64 \
  -machine q35,accel=kvm:tcg -m 2G -smp 2 \
  -drive if=none,id=v0,file=virtio.img,format=raw \
  -device virtio-blk-pci,drive=v0 \
  -nographic -no-reboot
```

Use this to exercise the block layer's request validation, queueing, flush,
and error mapping. It is not hardware evidence.

### 2.9 Firmware and ACPI/UEFI tests

Use OVMF for a UEFI boot path if available:

```sh
qemu-system-x86_64 \
  -machine q35,accel=kvm:tcg -m 2G -smp 2 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
  -nographic -no-reboot
```

The exact OVMF path varies by distribution. Record the firmware version and
the ACPI tables seen by the guest. QEMU's firmware is not Haswell firmware;
the real-hardware test is authoritative for ACPI/UEFI compatibility.

### 2.10 QEMU fault injection

`blkdebug` can inject errors and delays:

```ini
# blkdebug.conf
[inject-error]
event = "write_aio"
errno = "5"
once = "on"
```

```sh
qemu-system-x86_64 \
  -machine q35,accel=kvm:tcg -m 2G -smp 2 \
  -drive if=none,id=bad0,file=blkdebug:blkdebug.conf:ahci-ssd.img,format=raw \
  -device ide-hd,drive=bad0 \
  -nographic -no-reboot
```

Use `blkverify` to compare a suspect image against a reference. Use
`qemu-io` for deterministic I/O:

```sh
qemu-io -f raw -c "write -P 0x5a 0 1M" ahci-ssd.img
qemu-io -f raw -c "read -P 0x5a 0 1M" ahci-ssd.img
```

QEMU fault injection is good for block-layer error paths. It does not
reproduce real SATA link errors, NVMe firmware bugs, or USB stalls.

## 3. Real-hardware environment

### 3.1 Inventory before testing

Run the read-only inventory script:

```sh
./tools/collect_inventory.sh all > inventory.txt
```

Or run the commands directly:

```sh
lspci -nnvv > lspci.txt
lspci -t > topology.txt
lsusb -tv > lsusb.txt
lsblk -d -o NAME,ROTA,SIZE,MODEL,SERIAL,TRAN,LOG-SEC > lsblk.txt
cat /proc/cmdline > cmdline.txt
cat /proc/interrupts > interrupts.txt
cat /proc/iomem > iomem.txt
dmesg > dmesg.txt
sudo acpidump > acpi.dat
sudo dmidecode > dmi.txt
```

Record the motherboard/BIOS version, CPU model, GPU SKU, and whether the
machine has eDRAM.

### 3.2 Haswell PCH and device IDs

| Device | Expected ID |
|---|---|
| Haswell PCH SATA AHCI | `8086:8c02` or `8086:8c03` |
| Haswell PCH xHCI | `8086:8c31` |
| Haswell PCH EHCI | `8086:8c26` |
| Haswell GT2 GPU | `8086:0412` (example) |
| Haswell ULT GT3 GPU | `8086:0a26` (example) |
| Haswell CRW GT3 GPU | `8086:0d26` (example) |
| NVMe controller | class `0x0108`; vendor-specific ID |
| USB mass storage | USB interface class `0x08`, subclass `0x06`, protocol `0x50`/`0x62` |

Do not hard-code the NVMe vendor/device ID. Enumerate the class and record the
actual ID.

### 3.3 SATA HDD and SSD

Identify the device before any destructive test:

```sh
lsblk -d -o NAME,ROTA,SIZE,MODEL,SERIAL,TRAN,LOG-SEC
sudo smartctl -i /dev/sdX
sudo hdparm -I /dev/sdX
```

Expected:

- HDD: `ROTA=1`; SMART reports rotational media; `smartctl -a` shows seek
  and power-on data.
- SATA SSD: `ROTA=0`; SMART reports SSD media; `smartctl -a` shows wear and
  erase counts.
- Both: `TRAN=sata`; logical block size is 512 or 4096; physical block size
  is reported correctly.

Functional test (disposable disk only):

```sh
sudo fio --name=seqread --rw=read --bs=1M --size=1G \
  --filename=/dev/sdX --direct=1 --ioengine=libaio --iodepth=16
sudo fio --name=seqwrite --rw=write --bs=1M --size=1G \
  --filename=/dev/sdX --direct=1 --ioengine=libaio --iodepth=16
sudo fio --name=randread --rw=randread --bs=4K --size=1G \
  --filename=/dev/sdX --direct=1 --ioengine=libaio --iodepth=32
sudo fio --name=randwrite --rw=randwrite --bs=4K --size=1G \
  --filename=/dev/sdX --direct=1 --ioengine=libaio --iodepth=32
```

Flush and durability:

```sh
sudo dd if=/dev/zero of=/dev/sdX bs=1M count=64 conv=fsync
sudo blockdev --flushbufs /dev/sdX
```

Do not use `dd` on a disk with data. Verify the serial number first.

### 3.4 NVMe

```sh
lspci -nn | grep -i "non-volatile"
sudo nvme list
sudo nvme list-subsys
sudo nvme id-ctrl /dev/nvme0
sudo nvme id-ns /dev/nvme0n1
sudo smartctl -a /dev/nvme0
```

Expected: the controller reports an NVMe version, queue depth, namespace
count, and logical block size. The namespace reports its LBA format. Test
multiple namespaces if the device supports them.

Functional test (disposable namespace only):

```sh
sudo fio --name=nvme-seqread --rw=read --bs=1M --size=1G \
  --filename=/dev/nvme0n1 --direct=1 --ioengine=libaio --iodepth=64
sudo fio --name=nvme-randwrite --rw=randwrite --bs=4K --size=1G \
  --filename=/dev/nvme0n1 --direct=1 --ioengine=libaio --iodepth=64
sudo nvme flush /dev/nvme0n1
```

### 3.5 USB BOT and UAS

```sh
lsusb -tv
usb-devices
lsblk -d -o NAME,ROTA,SIZE,MODEL,SERIAL,TRAN
```

Expected:

- BOT: interface protocol `0x50`; one command per LUN; SCSI disk visible.
- UAS: interface protocol `0x62`; SCSI disk visible; multiple commands may be
  in flight.
- Both: `TRAN=usb`; removable flag set; media change is handled.

Hotplug test:

1. Insert the device; wait for enumeration.
2. Mount and read a file.
3. Unmount and remove the device.
4. Reinsert; verify a new block device and no stale partitions.
5. Repeat while I/O is in flight to test surprise removal.

Do not use a USB device that contains the only copy of your data.

### 3.6 Haswell GPU and eDRAM

QEMU does not provide a faithful i915 model. Run these on real hardware:

```sh
lspci -nn | grep -i vga
dmesg | grep -i i915
cat /sys/class/drm/card0/device/vendor
cat /sys/class/drm/card0/device/device
```

Record whether eDRAM is present. The i915 driver logs the detected eDRAM size
during initialization; a GT3e machine should report 128 MB. If you have
`intel_reg_read`, read `HSW_EDRAM_CAP` at `0x120010` and record bit 0. Treat
raw MMIO reads as diagnostic only; a wrong register access can hang the GPU.

IGT coverage (paths vary by install):

```sh
sudo igt/gem_exec_basic
sudo igt/gem_exec_nop
sudo igt/gem_exec_parallel
sudo igt/gem_ctx_basic
sudo igt/gem_ctx_create
sudo igt/gem_ctx_switch
sudo igt/gem_ctx_thrash
sudo igt/gem_mmap
sudo igt/gem_reloc
sudo igt/gem_wait
sudo igt/gem_busy
sudo igt/gem_sync
sudo igt/gem_evict
sudo igt/gem_shrink
sudo igt/gem_ppgtt
sudo igt/gem_reset_stats
sudo igt/gem_exec_fence
sudo igt/kms_flip
```

Some tests are generation-specific and may skip on Haswell. Record skips;
do not count a skip as a pass.

GPU hang recovery:

- Use the i915 debugfs hang injection if available (the exact path varies).
- Run a batch that writes a known value, force a hang, and verify the driver
  resets the engine and completes unrelated work.
- Check `dmesg` for the GPU error state and the reset path.

Suspend/resume:

```sh
sudo rtcwake -m mem -s 10
dmesg | tail -100
```

Verify that the GPU, SATA, NVMe, and USB devices reinitialize, and that no
request is lost or duplicated.

## 4. Coverage matrix

| Requirement | QEMU | Real hardware | Test ID |
|---|---|---|---|
| SATA HDD boot/data | Yes (AHCI command path) | Yes | `ST-AHCI-HDD` |
| SATA SSD boot/data | Yes (AHCI command path) | Yes | `ST-AHCI-SSD` |
| NVMe boot/data | Yes (NVMe 1.4) | Yes | `ST-NVME` |
| USB BOT | Yes | Yes | `ST-USB-BOT` |
| USB UAS | Yes | Yes | `ST-USB-UAS` |
| Flush / FUA durability | Fault injection | Power-loss / flush test | `ST-FLUSH` |
| Discard / write zeroes | QEMU + block layer | Real device | `ST-DISCARD` |
| 512e / 4Kn | QEMU images | Real devices | `ST-SECTOR` |
| Hotplug / surprise removal | Limited | Yes | `ST-HOTPLUG` |
| Error recovery | `blkdebug` | SMART / link error | `ST-ERROR` |
| ACPI/UEFI discovery | OVMF | Haswell firmware | `FW-ACPI-UEFI` |
| Haswell GPU | No | Yes | `GPU-*` |
| eDRAM | No | GT3e only | `GPU-EDRAM` |
| Runtime power | No | Yes | `PWR-*` |
| Performance | No | Yes | `PERF-*` |

## 5. Pass/fail criteria

A storage test passes only when:

- the device enumerates with the expected protocol and class;
- the block device reports the correct logical and physical block size;
- read, write, flush, and (if supported) discard/write zeroes complete;
- `fio` direct I/O at the advertised queue depth completes without errors;
- no kernel log contains an unexpected I/O error, timeout, or reset;
- a clean unmount and power cycle preserves the written data;
- a reset or surprise removal does not produce a use-after-free or a hung
  queue.

A GPU test passes only when:

- the GPU is identified as Haswell Gen7.5, not a generic Intel GPU;
- GGTT and PPGTT mappings agree for a shared buffer;
- the 2 GB PPGTT boundary is enforced;
- relocation survives eviction and rebind;
- `MI_SET_CONTEXT` + `MI_NOOP` does not hang;
- seqno write and user interrupt complete a request;
- a forced hang is recovered;
- eDRAM presence is detected on GT3e and the enable sequence completes;
- IGT tests pass or skip for a documented generation reason.

## 6. Test record template

```
Test ID:
Date (UTC):
Commit:
Tester:
Platform / board:
CPU:
GPU SKU / eDRAM:
Firmware / BIOS version:
QEMU version / machine type:
Kernel / Ozero build:
Device IDs (lspci -nn):
Storage transport / media:
Command:
Expected:
Actual:
Logs (paths):
Pass / Fail / Skip:
Notes:
```

Store logs with the record. A result without logs is not reproducible.

## 7. Repeatability

- Pin the QEMU version and machine type in CI.
- Pin the kernel/Ozero commit and record it in every result.
- Use raw images with known hashes; recreate them for each destructive run.
- Record `lspci -nnvv`, `lsusb -tv`, `lsblk`, `dmesg`, and the relevant
  `/sys/block/*/queue/*` values.
- Run each test at least three times on real hardware; report variance.
- Separate "pass", "fail", and "skip"; do not hide skips in a pass count.

## 8. Limitations

- QEMU NVMe implements NVMe 1.4, while the reference specification is 2.4.
  QEMU cannot validate 2.4-only features.
- QEMU AHCI is functional but not timing-accurate; it does not model
  mechanical disks, real NCQ reordering, or SATA link errors.
- QEMU USB has no PHY, electrical, or real firmware behaviour; BOT/UAS
  functional tests are not USB compliance tests.
- QEMU has no faithful Haswell i915 model. GPU tests require real hardware.
- Real hardware varies by firmware, disk model, and enclosure. Record the
  exact model and firmware.
- Haswell-era firmware is older than ACPI 6.6/UEFI 2.11; compatibility tests
  must include the actual machine firmware, not only OVMF.
- Power-loss tests can corrupt data. Use disposable devices only.

## 9. Safety

- Destructive commands (`dd`, `wipefs`, `blkdiscard`, `fio` on a raw device)
  are allowed only on disposable images or disks.
- Verify the model and serial number before writing to a raw device.
- Do not run SMART self-tests that spin down or stress a production disk.
- Do not read/write arbitrary MMIO registers on a production machine; a wrong
  access can hang the GPU or the platform.
- Keep a recovery boot path (USB or network) when testing storage drivers.

## 10. References

- QEMU, *NVMe Emulation*.
  <https://www.qemu.org/docs/master/system/devices/nvme.html>
- QEMU, *USB Devices*.
  <https://www.qemu.org/docs/master/system/devices/usb.html>
- QEMU, *Block drivers*.
  <https://www.qemu.org/docs/master/system/qemu-block-drivers.html>
- QEMU, *Invocation*.
  <https://www.qemu.org/docs/master/system/invocation.html>
- Linux kernel, *Block layer*.
  <https://docs.kernel.org/block/index.html>
- Linux kernel, *libata*.
  <https://docs.kernel.org/driver-api/libata.html>
- Linux kernel, *NVMe*.
  <https://docs.kernel.org/nvme/index.html>
- Linux kernel, *USB mass storage*.
  <https://docs.kernel.org/usb/mass-storage.html>
- Linux kernel, *Runtime Power Management*.
  <https://docs.kernel.org/power/runtime_pm.html>
- IGT GPU Tools.
  <https://gitlab.freedesktop.org/drm/igt-gpu-tools>
- Intel, *Haswell PRM* (archived).
  <https://web.archive.org/web/20230108225954/https://01.org/linuxgraphics/documentation/hardware-specification-prms>
