#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-x86_64}"
CPU_MODEL="${QEMU_CPU:-qemu64}"
# QEMU_SMP selects the vCPU count. The AP bring-up path is only exercised when
# this is greater than one.
SMP="${QEMU_SMP:-1}"
# QEMU_FIRMWARE selects the firmware path: "bios" (default) or "uefi" (OVMF).
# The ISO is the same hybrid image in both cases; only the emulated firmware
# differs, so the serial evidence must state which one produced it.
FIRMWARE="${QEMU_FIRMWARE:-bios}"
# QEMU_LOCK_ORDER=1 requires the build to have been compiled with
# -DO0_LOCK_ORDER_CHECK and asserts its boot marker.
LOCK_ORDER="${QEMU_LOCK_ORDER:-0}"
ISO="${1:-build/ozero.iso}"
LOG="${2:-build/qemu-serial.log}"
DEBUG_LOG="${QEMU_DEBUG_LOG:-build/qemu-debug.log}"
TIMEOUT_SECONDS="${QEMU_TIMEOUT_SECONDS:-15}"
# QEMU_STORAGE selects the storage controller cell: virtio (legacy transport),
# modern-virtio (MMIO capability transport, disable-legacy=on), ahci or nvme. It
# only changes which markers are required; the device itself is attached through
# QEMU_EXTRA_ARGS by the CI job.
STORAGE="${QEMU_STORAGE:-}"
EXTRA_ARGS=()
if [[ -n "${QEMU_EXTRA_ARGS:-}" ]]; then
    read -r -a EXTRA_ARGS <<<"$QEMU_EXTRA_ARGS"
fi
OVMF_CODE_PATH="${QEMU_OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
OVMF_VARS_PATH="${QEMU_OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}"

if ! command -v "$QEMU" >/dev/null 2>&1; then
    echo "error: $QEMU is not installed" >&2
    exit 2
fi
if [[ ! -f "$ISO" ]]; then
    echo "error: boot image not found: $ISO" >&2
    exit 2
fi

firmware_args=()
firmware_marker="firmware=BIOS"
case "$FIRMWARE" in
    bios)
        ;;
    uefi)
        firmware_marker="firmware=UEFI"
        if [[ ! -f "$OVMF_CODE_PATH" || ! -f "$OVMF_VARS_PATH" ]]; then
            echo "error: OVMF firmware not found ($OVMF_CODE_PATH, $OVMF_VARS_PATH)" >&2
            echo "       install the 'ovmf' package or set QEMU_OVMF_CODE / QEMU_OVMF_VARS" >&2
            exit 2
        fi
        # The variable store is written by the firmware, so use a per-run copy.
        vars_copy="$(dirname "$LOG")/OVMF_VARS-${CPU_MODEL}-${FIRMWARE}.fd"
        cp "$OVMF_VARS_PATH" "$vars_copy"
        firmware_args=(
            -drive "if=pflash,format=raw,unit=0,readonly=on,file=$OVMF_CODE_PATH"
            -drive "if=pflash,format=raw,unit=1,file=$vars_copy"
        )
        ;;
    *)
        echo "error: QEMU_FIRMWARE must be 'bios' or 'uefi', got '$FIRMWARE'" >&2
        exit 2
        ;;
esac

mkdir -p "$(dirname "$LOG")" "$(dirname "$DEBUG_LOG")"
rm -f "$LOG" "$DEBUG_LOG"

set +e
timeout "${TIMEOUT_SECONDS}s" "$QEMU" \
    -machine q35 -cpu "$CPU_MODEL" -smp "$SMP" -m 512M "${firmware_args[@]}" \
    -cdrom "$ISO" -boot d "${EXTRA_ARGS[@]}" \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown \
    -d int,in_asm,guest_errors,cpu_reset -D "$DEBUG_LOG"
status=$?
set -e

# A healthy kernel deliberately halts forever; timeout's 124 is expected.
if [[ "$status" -ne 124 && "$status" -ne 0 ]]; then
    echo "error: QEMU exited with status $status" >&2
    echo "--- guest serial output ---" >&2
    cat "$LOG" >&2 || true
    echo "--- QEMU diagnostic output ---" >&2
    tail -n 160 "$DEBUG_LOG" >&2 || true
    exit "$status"
fi

# Markers below are serial output only: they prove the kernel reached that
# point in this emulator and firmware, not that it works on physical hardware.
required_markers=(
    "OZERO: build commit="
    "OZERO: kaslr layout offset="
    "OZERO: cpu vendor="
    "OZERO: platform="
    "OZERO: kernel entry reached"
    "$firmware_marker"
    "OZERO: Limine base revision accepted"
    "OZERO: memory map entries="
    "OZERO: boot success"
    # Interrupt delivery: the 8259 output must reach the CPU through a
    # configured local APIC (ExtINT virtual wire) or, where no APIC is
    # available, directly. The marker records which configuration ran.
    "OZERO: interrupt delivery="
    "OZERO: ioapic=active"
    "OZERO: smp cpus=$SMP online=$SMP"
    "OZERO: timer="
    "OZERO: bootloader reclaim bytes="
    # Stage 3 execution model: page tables, heap, scheduler, self-tests, and
    # the demo threads running to completion under the preemptive scheduler.
    # These last markers are what prove the timer interrupt is actually being
    # delivered, so they must never be dropped to make a boot "pass".
    "OZERO: page tables active"
    "OZERO: kernel heap ready"
    "OZERO: scheduler initialised"
    "OZERO: stage3 self-test complete"
    "OZERO: kernel threads created="
    # Stage 4 storage: block layer, RAM disk, OzeroFS and the fault path.
    "OZERO: block layer ready devices="
    # Wave 4.1: PCI capability walk over real config space in the emulator.
    "OZERO: pci caps msi="
    "OZERO: ramdisk registered name=ram0"
    "OZERO: o0fs formatted blocks="
    "OZERO: o0fs mounted free blocks="
    "OZERO: o0fs file OK bytes="
    "OZERO: o0fs fault test OK"
    "OZERO: storage self-test complete"
    # Stage 5 GPU: core memory objects and GPU discovery over the PCI layer.
    "OZERO: gpu core self-test complete"
    "OZERO: gpu pci display devices="
    "OZERO: gpu discovery complete"
    # Stage 5 submission model: validated batch submission, fences, timeout safety
    # and context isolation. No device is claimed, so the fence must time out.
    "OZERO: gpu context isolation OK"
    "OZERO: gpu submit seqno=1"
    "OZERO: gpu fence timeout OK"
    "OZERO: gpu faulted queue refused work"
    "OZERO: gpu recovery refused without verified reset"
    "OZERO: gpu unconsumed work quarantined"
    "OZERO: gpu submission self-test complete"
    # Wave 2 user mode: entry through iretq, the native syscall ABI, contained
    # user faults, rejected syscall pointers and leak-free process churn.
    "OZERO: gdt+tss installed"
    "OZERO: framebuffer bars=8"
    "OZERO: framebuffer checksum="
    "OZERO: user entry OK"
    "OZERO: syscall exit status=0"
    "OZERO: user fault contained"
    "OZERO: syscall bad pointer rejected"
    "OZERO: user churn OK processes=16"
    "OZERO: wave2 self-test complete"
    # Wave 3 capability handles, budgets and synchronous IPC. These markers are
    # emulator evidence only; they do not claim physical hardware behaviour.
    "OZERO: wave3 done spawner status=0"
    "OZERO: ipc pair OK pair-1"
    "OZERO: handle negative tests OK"
    "OZERO: budget exhaustion OK"
    "OZERO: ipc negative tests OK"
    "OZERO: wave3 churn OK pairs=8"
    "OZERO: wave3 self-test complete"
)

if [[ "$SMP" -gt 1 ]]; then
    required_markers+=("OZERO: tlb shootdown OK")
    required_markers+=("OZERO: tlb shootdown range OK")
    required_markers+=("OZERO: tlb shootdown full OK")
    required_markers+=("OZERO: smp scheduling OK cpus=")
    required_markers+=("OZERO: smp user churn OK processes=16")
fi

if [[ "$LOCK_ORDER" == "1" ]]; then
    required_markers+=("OZERO: lock order check=on")
fi

# Wave 4.2/4.3 storage cells. The controller is attached with QEMU_EXTRA_ARGS and
# each cell requires that controller's read/write marker plus its injected-error
# path. These markers are emulator evidence only.
case "$STORAGE" in
    "")
        ;;
    virtio)
        required_markers+=("OZERO: virtio-blk read/write OK")
        required_markers+=("OZERO: virtio-blk error path OK")
        ;;
    modern-virtio)
        required_markers+=("OZERO: virtio-blk read/write OK")
        required_markers+=("OZERO: virtio-blk error path OK")
        # The modern transport must report its own bring-up: a modern-only cell
        # that silently fell back to legacy register programming would be a
        # transport bug, not a pass.
        required_markers+=("OZERO: virtio-blk modern ready queue=")
        ;;
    ahci)
        required_markers+=("OZERO: ahci read/write OK")
        required_markers+=("OZERO: ahci error path OK")
        ;;
    nvme)
        required_markers+=("OZERO: nvme read/write OK")
        required_markers+=("OZERO: nvme error path OK")
        ;;
    *)
        echo "error: QEMU_STORAGE must be virtio, modern-virtio, ahci or nvme, got '$STORAGE'" >&2
        exit 2
        ;;
esac

# The SMAP fault-fixup path only runs when the CPU advertises SMAP. QEMU's
# 'max' model does; qemu64 and Haswell-v1 do not.
if [[ "$CPU_MODEL" == "max" ]]; then
    required_markers+=("OZERO: smep=on smap=on")
fi

# The scheduler may interleave the two worker threads differently between
# boots. Check these as a set; never encode one particular interleaving.
unordered_markers=(
    "OZERO: worker-a iteration=2"
    "OZERO: worker-b iteration=2"
    "OZERO: worker-a exiting"
    "OZERO: worker-b exiting"
)

# UEFI firmware may emit ANSI cursor/colour sequences around its own output.
# Strip CSI and OSC sequences before matching so a marker split by escape
# codes is not reported as missing.
MATCH_LOG="${LOG%.log}.match.log"
python3 - "$LOG" "$MATCH_LOG" <<'PY'
import re
import sys

source, target = sys.argv[1], sys.argv[2]
data = open(source, "rb").read().decode("utf-8", "replace")
ansi = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]|\x1b\][^\x07]*(?:\x07|\x1b\\)")
open(target, "w", encoding="utf-8").write(ansi.sub("", data))
PY

for marker in "${required_markers[@]}" "${unordered_markers[@]}"; do
    if ! grep -Fq "$marker" "$MATCH_LOG"; then
        echo "error: missing boot milestone: $marker" >&2
        echo "--- guest serial output ---" >&2
        cat "$LOG" >&2 || true
        echo "--- QEMU diagnostic output (last 160 lines) ---" >&2
        tail -n 160 "$DEBUG_LOG" >&2 || true
        exit 1
    fi
done

fault_count=$(grep -Fc "OZERO: user fault contained" "$MATCH_LOG" || true)
if [[ "$fault_count" -lt 3 ]]; then
    echo "error: expected 3 contained user faults, found $fault_count" >&2
    echo "--- guest serial output ---" >&2
    cat "$LOG" >&2 || true
    exit 1
fi

cat "$LOG"
echo "QEMU smoke test: PASS (firmware=$FIRMWARE cpu=$CPU_MODEL smp=$SMP qemu=$("$QEMU" --version | head -n 1))"
