#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-x86_64}"
ISO="${1:-build/ozero.iso}"
LOG="${2:-build/qemu-serial.log}"

if ! command -v "$QEMU" >/dev/null 2>&1; then
    echo "error: $QEMU is not installed" >&2
    exit 2
fi

mkdir -p "$(dirname "$LOG")"
rm -f "$LOG"
set +e
timeout 15s "$QEMU" \
    -machine q35 -m 512M -cdrom "$ISO" -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown
status=$?
set -e

# A healthy kernel deliberately halts forever; timeout's 124 is expected.
if [[ "$status" -ne 124 && "$status" -ne 0 ]]; then
    echo "error: QEMU exited with status $status" >&2
    cat "$LOG" >&2 || true
    exit "$status"
fi

grep -Fq "OZERO: kernel entry reached" "$LOG"
grep -Fq "OZERO: Limine base revision accepted" "$LOG"
grep -Fq "OZERO: memory map entries=" "$LOG"
grep -Fq "OZERO: boot success" "$LOG"
cat "$LOG"
echo "QEMU smoke test: PASS"
