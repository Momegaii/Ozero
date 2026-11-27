#!/usr/bin/env python3
"""Verify the O0 desktop UI renders visible output.

Boots Ozero in QEMU, waits for the `OZERO: stage6 desktop active` serial
marker, takes QMP screendumps at intervals, and checks that:
  * the background is dark (black / near-black)
  * at least one neon-green sphere pixel is present

This is CI evidence for Stage 6.7 custom desktop UI rendering.
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
ISO = Path("build/ozero.iso").resolve()
SERIAL = Path("build/qemu-desktop-serial.log").resolve()
SCREENDUMP = Path("build/qemu-desktop.ppm").resolve()

# Desktop colors from docs/decisions/0020-custom-desktop-ui.md
BG_COLOR = (0, 0, 0)          # black
NEON_GREEN = (0, 255, 0)      # neon green launcher
TOLERANCE = 30

PAGE_SIZE = 4096


def qmp_command(stream, cmd):
    stream.write((json.dumps(cmd) + "\n").encode())
    line = stream.readline()
    if not line:
        raise RuntimeError("QMP stream closed")
    return json.loads(line)


def parse_ppm(data: bytes):
    """Parse a P6 PPM. Returns (width, height, pixels_bytes)."""
    if not data.startswith(b"P6\n"):
        raise ValueError(f"unexpected PPM header: {data[:20]!r}")
    pos = 3
    # Skip optional comment lines
    while data[pos:pos + 1] == b"#":
        pos = data.index(b"\n", pos) + 1
    # Read dimensions
    end = data.index(b"\n", pos)
    dims = data[pos:end].decode().split()
    width, height = int(dims[0]), int(dims[1])
    pos = end + 1
    # Read maxval
    end = data.index(b"\n", pos)
    pos = end + 1
    pixels = data[pos:]
    return width, height, pixels


def sample_colors(data, width, height, step):
    """Return Counter of sampled RGB colors from PPM pixel data."""
    from collections import Counter
    colors = Counter()
    pos = 3
    while data[pos:pos + 1] == b"#":
        pos = data.index(b"\n", pos) + 1
    end = data.index(b"\n", pos)
    pos = end + 1
    end = data.index(b"\n", pos)
    pos = end + 1
    pixels = data[pos:]
    for y in range(0, height, step):
        for x in range(0, width, step):
            off = (y * width + x) * 3
            if off + 2 < len(pixels):
                colors[(pixels[off], pixels[off + 1], pixels[off + 2])] += 1
    return colors


def count_matching(data, width, height, target_rgb, tolerance, step):
    """Count sampled pixels within tolerance of target_rgb."""
    count = 0
    total = 0
    pos = 3
    while data[pos:pos + 1] == b"#":
        pos = data.index(b"\n", pos) + 1
    end = data.index(b"\n", pos)
    pos = end + 1
    end = data.index(b"\n", pos)
    pos = end + 1
    pixels = data[pos:]
    for y in range(0, height, step):
        for x in range(0, width, step):
            off = (y * width + x) * 3
            if off + 2 >= len(pixels):
                continue
            total += 1
            r, g, b = pixels[off], pixels[off + 1], pixels[off + 2]
            if (abs(r - target_rgb[0]) <= tolerance and
                abs(g - target_rgb[1]) <= tolerance and
                abs(b - target_rgb[2]) <= tolerance):
                count += 1
    return count, total


def main() -> int:
    if shutil.which(QEMU) is None:
        print(f"error: {QEMU} not installed", file=sys.stderr)
        return 2
    if not ISO.is_file():
        print(f"error: boot image not found: {ISO}", file=sys.stderr)
        return 2

    serial_parent = SERIAL.parent
    serial_parent.mkdir(parents=True, exist_ok=True)
    SCREENDUMP.unlink(missing_ok=True)

    with tempfile.TemporaryDirectory(prefix="ozero-desktop-") as tmp:
        qmp_path = Path(tmp) / "qmp.sock"

        command = [
            QEMU, "-machine", "q35", "-cpu", "qemu64", "-m", "512M",
            "-cdrom", str(ISO), "-boot", "d",
            "-display", "none", "-serial", f"file:{SERIAL}", "-monitor", "none",
            "-qmp", f"unix:{qmp_path},server=on,wait=off",
            "-no-reboot", "-no-shutdown",
        ]
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, text=True)
        try:
            # Wait for the desktop-active marker.
            deadline = time.time() + 40.0
            found = False
            while time.time() < deadline:
                if process.poll() is not None:
                    print("error: QEMU exited early", file=sys.stderr)
                    return 1
                if SERIAL.exists():
                    text = SERIAL.read_text(errors="replace")
                    if "OZERO: stage6 desktop active" in text:
                        found = True
                        break
                time.sleep(0.2)

            if not found:
                text = SERIAL.read_text(errors="replace") if SERIAL.exists() else ""
                print(f"error: timed out waiting for stage6 desktop active\n{text}",
                      file=sys.stderr)
                return 1

            # Give the desktop thread time to render several frames.
            time.sleep(2.0)

            if not qmp_path.exists():
                print("error: QMP socket did not appear", file=sys.stderr)
                return 1

            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(10.0)
            sock.connect(str(qmp_path))
            stream = sock.makefile("rwb", buffering=0)
            stream.readline()  # greeting
            qmp_command(stream, {"execute": "qmp_capabilities"})

            best_bg_pct = 0
            best_green = 0
            best_width = 0
            best_height = 0

            # Take up to 3 screendumps at intervals to catch rendered frames.
            for attempt in range(3):
                reply = qmp_command(stream, {
                    "execute": "screendump",
                    "arguments": {"filename": str(SCREENDUMP)},
                })
                if "error" in reply:
                    print(f"warning: screendump attempt {attempt + 1} failed: {reply}",
                          file=sys.stderr)
                    time.sleep(0.5)
                    continue

                if not SCREENDUMP.exists():
                    time.sleep(0.5)
                    continue

                data = SCREENDUMP.read_bytes()
                try:
                    width, height, _ = parse_ppm(data)
                except ValueError as exc:
                    print(f"warning: PPM parse failed: {exc}", file=sys.stderr)
                    time.sleep(0.5)
                    continue

                best_width, best_height = width, height

                bg_count, total = count_matching(data, width, height, BG_COLOR,
                                                  TOLERANCE, 7)
                green_count, _ = count_matching(data, width, height,
                                                 NEON_GREEN, TOLERANCE, 7)
                bg_pct = bg_count * 100 // total if total else 0

                if bg_pct > best_bg_pct or green_count > best_green:
                    best_bg_pct = bg_pct
                    best_green = green_count

                print(f"attempt {attempt + 1}: dark={bg_pct}% green={green_count}")

                if best_bg_pct >= 50 and best_green > 0:
                    break

                time.sleep(0.5)

            sock.close()

            print(f"screendump: {best_width}x{best_height}")
            print(f"dark background pixels: {best_bg_pct}% (best attempt)")
            print(f"neon-green sphere pixels: {best_green} (best attempt)")

            if best_bg_pct < 50:
                print("FAIL: background is not predominantly dark", file=sys.stderr)
                return 1
            if best_green == 0:
                print("FAIL: no neon-green sphere pixels found", file=sys.stderr)
                return 1

            print("PASS: desktop UI renders dark background with neon-green spheres")
            return 0
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    sys.exit(main())
