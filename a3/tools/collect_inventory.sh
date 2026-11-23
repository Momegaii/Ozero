#!/bin/sh
# Read-only hardware inventory for Ozero Stage 0.
# Usage: ./tools/collect_inventory.sh [all|storage|gpu]
# Some sections need root; they are skipped or use sudo -n when available.

set -u

MODE="${1:-all}"
case "$MODE" in
    all|storage|gpu) ;;
    -h|--help|help)
        echo "Usage: $0 [all|storage|gpu]"
        echo "Prints a read-only inventory to stdout. Redirect it to a file."
        exit 0
        ;;
    *)
        echo "Usage: $0 [all|storage|gpu]" >&2
        exit 2
        ;;
esac

section() {
    printf '\n\n===== %s =====\n' "$*"
}

run() {
    printf '\n### %s\n' "$*"
    if command -v "$1" >/dev/null 2>&1; then
        "$@" 2>&1 || true
    else
        echo "SKIP: $1 not found"
    fi
}

run_priv() {
    printf '\n### %s\n' "$*"
    if [ "$(id -u)" -eq 0 ]; then
        "$@" 2>&1 || true
    elif command -v sudo >/dev/null 2>&1; then
        sudo -n "$@" 2>&1 || echo "SKIP: sudo -n failed"
    else
        echo "SKIP: needs root and sudo is not available"
    fi
}

section "metadata"
date -u
uname -a
id
printf '\n### /proc/cmdline\n'
cat /proc/cmdline 2>/dev/null || true
printf '\n### git\n'
git rev-parse HEAD 2>/dev/null || echo "not a git repository"

section "cpu and memory"
run lscpu
printf '\n### MemTotal\n'
grep -E '^MemTotal|^MemAvailable' /proc/meminfo 2>/dev/null || true
run numactl --hardware

section "pci"
run lspci -nnvv
run lspci -t
printf '\n### interesting PCI devices\n'
lspci -nn 2>/dev/null | grep -Ei 'sata|non-volatile|usb|vga|display|3d|raid' || true

section "usb"
run lsusb -tv
run usb-devices

section "block devices"
run lsblk -d -o NAME,ROTA,SIZE,MODEL,SERIAL,TRAN,LOG-SEC
run lsblk -o NAME,MAJ:MIN,RM,SIZE,RO,TYPE,MOUNTPOINTS,FSTYPE,LABEL,UUID
run findmnt
run df -h

section "block queue attributes"
for d in /sys/block/*; do
    [ -e "$d" ] || continue
    name=${d##*/}
    printf '\n### /sys/block/%s\n' "$name"
    for attr in rotational removable logical_block_size physical_block_size nr_requests scheduler; do
        if [ -r "$d/queue/$attr" ]; then
            printf '%s: ' "$attr"
            cat "$d/queue/$attr"
        fi
    done
    for attr in model serial size; do
        if [ -r "$d/device/$attr" ]; then
            printf '%s: ' "$attr"
            cat "$d/device/$attr"
        fi
    done
done

if [ "$MODE" = "all" ] || [ "$MODE" = "storage" ]; then
    section "nvme"
    run nvme list
    run nvme list-subsys
    for n in /dev/nvme*n1; do
        [ -e "$n" ] || continue
        ctrl=${n%n*}
        printf '\n### %s\n' "$n"
        nvme id-ctrl "$ctrl" 2>&1 || true
        nvme id-ns "$n" 2>&1 || true
    done

    section "smart"
    for d in /dev/sd? /dev/nvme?; do
        [ -e "$d" ] || continue
        printf '\n### smartctl %s\n' "$d"
        smartctl -i -a "$d" 2>&1 || true
    done

    section "acpi and dmi"
    run_priv acpidump -o /tmp/o0-acpi.dat
    printf '\n### /tmp/o0-acpi.dat\n'
    ls -l /tmp/o0-acpi.dat 2>/dev/null || true
    run_priv dmidecode
    printf '\n### /proc/interrupts\n'
    cat /proc/interrupts 2>/dev/null || true
    printf '\n### /proc/iomem\n'
    cat /proc/iomem 2>/dev/null || true

    section "storage kernel messages"
    dmesg 2>/dev/null | grep -Ei 'ahci|ata[0-9]|nvme|usb-storage|uas|xhci|sd [a-z]|scsi|pcie|aer|iommu|dmar' || true
fi

if [ "$MODE" = "all" ] || [ "$MODE" = "gpu" ]; then
    section "gpu"
    lspci -nn 2>/dev/null | grep -Ei 'vga|display|3d' || true
    for d in /sys/class/drm/card*/device; do
        [ -e "$d" ] || continue
        printf '\n### %s\n' "$d"
        for attr in vendor device subsystem_vendor subsystem_device; do
            if [ -r "$d/$attr" ]; then
                printf '%s: ' "$attr"
                cat "$d/$attr"
            fi
        done
        if [ -r "$d/uevent" ]; then
            printf '\nuevent:\n'
            cat "$d/uevent"
        fi
    done
    printf '\n### i915 messages\n'
    dmesg 2>/dev/null | grep -Ei 'i915|edram|gtt|ppgtt|gpu hang|render ring' || true
    printf '\n### i915 debugfs\n'
    if [ -d /sys/kernel/debug/dri ]; then
        ls -la /sys/kernel/debug/dri 2>&1 || true
        for f in /sys/kernel/debug/dri/*/i915_*; do
            [ -e "$f" ] || continue
            printf '\n### %s\n' "$f"
            head -n 80 "$f" 2>&1 || true
        done
    else
        echo "SKIP: /sys/kernel/debug/dri not available (debugfs or driver missing)"
    fi
fi

section "done"
echo "Inventory complete. Review before sharing; logs may contain serial numbers."
