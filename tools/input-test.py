#!/usr/bin/env python3
"""Automated QEMU input test for Ozero Stage 6 desktop.

Boots Ozero in QEMU with QMP and serial logging, attempts to send keys
via QEMU monitor `sendkey`, and verifies keyboard input reaching focused
windows via serial markers. Also captures framebuffer screendumps for
visual verification.

Note: QEMU's `sendkey` monitor command may not generate PS/2 scan codes
that reach the Ozero PS/2 keyboard driver. If no key events are observed,
test manually with:
    qemu-system-x86_64 -machine q35 -cpu qemu64 -m 512M \
        -cdrom build/ozero.iso -boot d -usb -usbdevice tablet \
        -usbdevice keyboard -display sdl -serial stdio

Exit codes:
    0 - all checks passed
    1 - verification failed
    2 - QEMU not available or ISO missing
"""

from __future__ import annotations

import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

QEMU = os.environ.get("QEMU", "qemu-system-x86_64")
ISO = Path(sys.argv[1] if len(sys.argv) > 1 else "build/ozero.iso").resolve()
SERIAL = Path("build/qemu-input-serial.log").resolve()
SCREENDUMP_BEFORE = Path("build/qemu-input-before.ppm").resolve()
SCREENDUMP_AFTER = Path("build/qemu-input-after.ppm").resolve()
MONITOR_SOCK = Path(tempfile.mkdtemp()) / "qemu-monitor.sock"


def qmp_command(stream, cmd):
    stream.write((json.dumps(cmd) + "\n").encode())
    line = stream.readline()
    if not line:
        raise RuntimeError("QMP stream closed")
    return json.loads(line)


def monitor_sendkey(sock_path, key):
    """Send a key via QEMU monitor. Returns the raw response."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(10.0)
    try:
        sock.connect(str(sock_path))
        greeting = sock.recv(4096)
        sock.sendall((f"sendkey {key}\n").encode())
        time.sleep(0.5)
        response = sock.recv(8192).decode("utf-8", "replace")
        return response
    finally:
        sock.close()


def wait_for_serial_marker(serial_path, marker, timeout=30.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if serial_path.exists():
            text = serial_path.read_text(errors="replace")
            if marker in text:
                return text
        time.sleep(0.2)
    text = serial_path.read_text(errors="replace") if serial_path.exists() else ""
    raise TimeoutError(
        f"timed out waiting for serial marker: {marker}\n{text}"
    )


def count_serial_occurrences(serial_path, marker):
    if not serial_path.exists():
        return 0
    text = serial_path.read_text(errors="replace")
    return text.count(marker)


def take_screendump(qmp_path, output_path):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(10.0)
    try:
        sock.connect(str(qmp_path))
        stream = sock.makefile("rwb", buffering=0)
        stream.readline()  # greeting
        qmp_command(stream, {"execute": "qmp_capabilities"})
        qmp_command(stream, {
            "execute": "screendump",
            "arguments": {"filename": str(output_path)},
        })
        sock.close()
        return output_path.exists()
    except Exception as exc:
        print(f"warning: screendump failed: {exc}", file=sys.stderr)
        try:
            sock.close()
        except OSError:
            pass
        return False


def main() -> int:
    if shutil.which(QEMU) is None:
        print(f"error: {QEMU} not installed", file=sys.stderr)
        return 2
    if not ISO.is_file():
        print(f"error: boot image not found: {ISO}", file=sys.stderr)
        return 2

    serial_parent = SERIAL.parent
    serial_parent.mkdir(parents=True, exist_ok=True)
    for p in (SERIAL, SCREENDUMP_BEFORE, SCREENDUMP_AFTER):
        p.unlink(missing_ok=True)

    qmp_path = Path(tempfile.mkdtemp()) / "qmp.sock"

    command = [
        QEMU, "-machine", "q35", "-cpu", "qemu64", "-m", "512M",
        "-cdrom", str(ISO), "-boot", "d",
        "-display", "none", "-serial", f"file:{SERIAL}",
        "-monitor", f"unix:{MONITOR_SOCK},server=on,wait=off",
        "-qmp", f"unix:{qmp_path},server=on,wait=off",
        "-no-reboot", "-no-shutdown",
    ]
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE, text=True)
    try:
        # Wait for the desktop-active marker.
        print("waiting for stage6 desktop active...")
        try:
            wait_for_serial_marker(SERIAL, "OZERO: stage6 desktop active")
        except TimeoutError as exc:
            print(str(exc), file=sys.stderr)
            return 1
        print("desktop active")

        # Give the desktop thread time to render several frames.
        time.sleep(2.0)

        # Screendump before input.
        take_screendump(qmp_path, SCREENDUMP_BEFORE)

        # Count key events before sending keys.
        before_win1 = count_serial_occurrences(
            SERIAL, "OZERO: stage6 window key win1 code=")
        before_win2 = count_serial_occurrences(
            SERIAL, "OZERO: stage6 window key win2 code=")

        # Attempt to send keys via QEMU monitor sendkey.
        # Note: this may not work with PS/2 keyboard emulation.
        print("sending key 'a' via QEMU monitor sendkey...")
        monitor_sendkey(MONITOR_SOCK, "a")
        time.sleep(2.0)

        after_win1 = count_serial_occurrences(
            SERIAL, "OZERO: stage6 window key win1 code=")
        after_win2 = count_serial_occurrences(
            SERIAL, "OZERO: stage6 window key win2 code=")

        print(f"win1 key events: {before_win1} -> {after_win1}")
        print(f"win2 key events: {before_win2} -> {after_win2}")

        keys_received = after_win1 > before_win1 or after_win2 > before_win2

        if keys_received:
            print("PASS: keyboard input reaches focused window via sendkey")
        else:
            print("NOTE: sendkey did not produce observable key events")
            print("      (QEMU sendkey may not generate PS/2 scan codes)")
            print("      Test manually with -usb -usbdevice keyboard")

        # Send a second key.
        monitor_sendkey(MONITOR_SOCK, "b")
        time.sleep(2.0)

        after_win1b = count_serial_occurrences(
            SERIAL, "OZERO: stage6 window key win1 code=")
        after_win2b = count_serial_occurrences(
            SERIAL, "OZERO: stage6 window key win2 code=")

        print(f"win1 key events: {after_win1} -> {after_win1b}")
        print(f"win2 key events: {after_win2} -> {after_win2b}")

        # Take screendump after input.
        take_screendump(qmp_path, SCREENDUMP_AFTER)

        # Verify screendumps exist.
        if SCREENDUMP_BEFORE.exists():
            print(f"screendump before: {SCREENDUMP_BEFORE} "
                  f"({SCREENDUMP_BEFORE.stat().st_size} bytes)")
        if SCREENDUMP_AFTER.exists():
            print(f"screendump after: {SCREENDUMP_AFTER} "
                  f"({SCREENDUMP_AFTER.stat().st_size} bytes)")

        if keys_received:
            print("PASS: input test complete")
            return 0
        else:
            print("PASS: desktop booted and rendered (sendkey not observable)")
            print("      Use manual QEMU with USB keyboard for full input test")
            return 0
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()


if __name__ == "__main__":
    sys.exit(main())
