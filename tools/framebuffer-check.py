#!/usr/bin/env python3
"""Boot Ozero and verify the rendered Stage 6 desktop in a QEMU screendump.

The kernel first validates and paints the boot framebuffer, then the scheduled
Stage 6 desktop replaces that fallback pattern. This test waits for the desktop
activation marker and independently checks launcher, assistant, panel and
taskbar pixels in a QMP screendump. It is emulator evidence, not physical proof.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

COLORS = (
    (24, 32, 72), (24, 92, 150), (20, 150, 120), (72, 160, 48),
    (210, 170, 32), (210, 100, 32), (160, 48, 72), (220, 220, 220),
)
TOLERANCE = 3


def expected_pixel(x: int, y: int, width: int) -> tuple[int, int, int]:
    band = (x * 8) // width
    stripe = ((x // 24) + (y // 24)) & 1
    r, g, b = COLORS[band]
    if stripe:
        r, g, b = (r * 3) // 4, (g * 3) // 4, (b * 3) // 4
    return r, g, b


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError(f"{path}: not a binary PPM (P6)")
    fields: list[bytes] = []
    i = 2
    while len(fields) < 3:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while i < len(data) and data[i:i + 1] != b"\n":
                i += 1
            continue
        start = i
        while i < len(data) and not data[i:i + 1].isspace():
            i += 1
        fields.append(data[start:i])
    while i < len(data) and data[i:i + 1].isspace():
        i += 1
    width, height, maximum = (int(field) for field in fields)
    if maximum != 255:
        raise ValueError(f"{path}: unsupported maxval {maximum}")
    pixels = data[i:i + width * height * 3]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path}: truncated pixel data")
    return width, height, pixels


def sample(pixels: bytes, width: int, x: int, y: int) -> tuple[int, int, int]:
    offset = (y * width + x) * 3
    return pixels[offset], pixels[offset + 1], pixels[offset + 2]


def fnv(pixels: list[tuple[int, int, int]]) -> int:
    value = 2166136261
    for r, g, b in pixels:
        value ^= (r << 16) | (g << 8) | b
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def qmp_command(stream, payload: dict) -> dict:
    stream.write((json.dumps(payload) + "\n").encode())
    while True:
        line = stream.readline()
        if not line:
            raise RuntimeError("QMP closed the connection")
        message = json.loads(line)
        if "return" in message or "error" in message:
            return message


def wait_for(path: Path, needle: str, process: subprocess.Popen,
             timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = path.read_text(errors="replace") if path.exists() else ""
        if needle in text:
            return text
        if process.poll() is not None:
            raise RuntimeError(
                f"QEMU exited with {process.returncode} before {needle!r}\n{text}")
        time.sleep(0.2)
    text = path.read_text(errors="replace") if path.exists() else ""
    raise RuntimeError(f"timed out waiting for {needle!r}\n{text}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", default="build/ozero.iso")
    parser.add_argument("--serial", default="build/qemu-framebuffer.log")
    parser.add_argument("--screendump", default="build/qemu-framebuffer.ppm")
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    parser.add_argument("--cpu", default=os.environ.get("QEMU_CPU", "qemu64"))
    parser.add_argument("--firmware", default=os.environ.get("QEMU_FIRMWARE", "bios"))
    parser.add_argument("--timeout", type=float, default=40.0)
    args = parser.parse_args()

    if shutil.which(args.qemu) is None:
        print(f"error: {args.qemu} is not installed", file=sys.stderr)
        return 2
    iso = Path(args.iso).resolve()
    if not iso.is_file():
        print(f"error: boot image not found: {iso}", file=sys.stderr)
        return 2
    serial = Path(args.serial).resolve()
    screendump = Path(args.screendump).resolve()
    serial.parent.mkdir(parents=True, exist_ok=True)
    screendump.unlink(missing_ok=True)

    firmware_args: list[str] = []
    with tempfile.TemporaryDirectory(prefix="ozero-fb-") as tmp:
        qmp_path = Path(tmp) / "qmp.sock"
        if args.firmware == "uefi":
            code = Path(os.environ.get("QEMU_OVMF_CODE",
                                       "/usr/share/OVMF/OVMF_CODE_4M.fd"))
            source_vars = Path(os.environ.get("QEMU_OVMF_VARS",
                                              "/usr/share/OVMF/OVMF_VARS_4M.fd"))
            if not code.is_file() or not source_vars.is_file():
                print("error: OVMF firmware not found", file=sys.stderr)
                return 2
            vars_copy = Path(tmp) / "OVMF_VARS.fd"
            shutil.copyfile(source_vars, vars_copy)
            firmware_args = [
                "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
                "-drive", f"if=pflash,format=raw,unit=1,file={vars_copy}",
            ]
        elif args.firmware != "bios":
            print("error: --firmware must be bios or uefi", file=sys.stderr)
            return 2

        command = [
            args.qemu, "-machine", "q35", "-cpu", args.cpu, "-m", "512M",
            *firmware_args, "-cdrom", str(iso), "-boot", "d",
            "-display", "none", "-serial", f"file:{serial}", "-monitor", "none",
            "-qmp", f"unix:{qmp_path},server=on,wait=off",
            "-no-reboot", "-no-shutdown",
        ]
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, text=True)
        try:
            log = wait_for(serial, "OZERO: stage6 desktop active", process,
                           args.timeout)
            # The activation marker precedes the first frame; allow several
            # 16 ms render/present cycles before sampling the desktop.
            time.sleep(0.5)
            if not qmp_path.exists():
                raise RuntimeError("QMP socket did not appear")
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(10.0)
            sock.connect(str(qmp_path))
            stream = sock.makefile("rwb", buffering=0)
            json.loads(stream.readline())
            reply = qmp_command(stream, {"execute": "qmp_capabilities"})
            if "error" in reply:
                raise RuntimeError(f"QMP capabilities failed: {reply}")
            reply = qmp_command(stream, {
                "execute": "screendump",
                "arguments": {"filename": str(screendump)},
            })
            if "error" in reply:
                raise RuntimeError(f"screendump failed: {reply}")
            qmp_command(stream, {"execute": "quit"})
            stream.close()
            sock.close()
            process.wait(timeout=10)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()

    width, height, pixels = read_ppm(screendump)
    if width < 640 or height < 480:
        raise SystemExit(
            f"framebuffer-check: FAIL unexpected desktop size {width}x{height}")

    # Desktop launcher centres: (w/6,h/4), then 72-pixel steps on
    # alternating rows. The selected centres are outside demo-window bounds.
    checks = (
        ("green launcher", width // 6, height // 4, (0, 255, 0)),
        ("purple O0 assistant", width // 6 + 216, height // 4 + 72,
         (128, 0, 128)),
        ("right information panel", width - 20, height // 2, (10, 10, 10)),
        ("bottom taskbar", 20, height - 5, (5, 5, 5)),
    )
    for name, x, y, expected in checks:
        actual = sample(pixels, width, x, y)
        if max(abs(a - e) for a, e in zip(actual, expected)) > TOLERANCE:
            raise SystemExit(
                f"framebuffer-check: FAIL {name} at x={x} y={y} "
                f"actual={actual} expected={expected}")

    if "OZERO: framebuffer checksum=" not in log:
        raise SystemExit(
            "framebuffer-check: FAIL desktop started without boot framebuffer evidence")

    print(f"framebuffer-check: PASS desktop={width}x{height} "
          "green-launcher purple-assistant panel taskbar")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError) as error:
        print(f"framebuffer-check: FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
