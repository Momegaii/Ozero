#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-x86_64}"
ISO="${1:-build/ozero.iso}"
LOG="${2:-build/qemu-serial.log}"
DEBUG_LOG="${QEMU_DEBUG_LOG:-build/qemu-debug.log}"
TIMEOUT_SECONDS="${QEMU_TIMEOUT_SECONDS:-15}"

if ! command -v "$QEMU" >/dev/null 2>&1; then
    echo "error: $QEMU is not installed" >&2
    exit 2
fi
if [[ ! -f "$ISO" ]]; then
    echo "error: boot image not found: $ISO" >&2
    exit 2
fi

mkdir -p "$(dirname "$LOG")" "$(dirname "$DEBUG_LOG")"
rm -f "$LOG" "$DEBUG_LOG"

set +e
timeout "${TIMEOUT_SECONDS}s" "$QEMU" \
    -machine q35 -m 512M -cdrom "$ISO" -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown \
    -d guest_errors,cpu_reset -D "$DEBUG_LOG"
status=$?
set -e

# A healthy kernel deliberately halts forever; timeout's 124 is expected.
if [[ "$status" -ne 124 && "$status" -ne 0 ]]; then
    echo "error: QEMU exited with status $status" >&2
    echo "--- guest serial output ---" >&2
    cat "$LOG" >&2 || true
    echo "--- QEMU diagnostic output ---" >&2
    tail -n 100 "$DEBUG_LOG" >&2 || true
    exit "$status"
fi

required_markers=(
    "OZERO: kernel entry reached"
    "OZERO: Limine base revision accepted"
    "OZERO: memory map entries="
    "OZERO: boot success"
)

for marker in "${required_markers[@]}"; do
    if ! grep -Fq "$marker" "$LOG"; then
        echo "error: missing boot milestone: $marker" >&2
        echo "--- guest serial output ---" >&2
        cat "$LOG" >&2 || true
        echo "--- QEMU diagnostic output (last 100 lines) ---" >&2
        tail -n 100 "$DEBUG_LOG" >&2 || true
        exit 1
    fi
done

cat "$LOG"
echo "QEMU smoke test: PASS"
