#!/usr/bin/env python3
"""Boot Ozero, take a QEMU screendump, and verify the eight-bar boot pattern.

The kernel draws eight equal vertical bands and logs the centre pixel and a
checksum for each band. This tool takes an independent screendump through QMP
and compares the raw pixels with the same deterministic pattern. It is CI
evidence for the framebuffer path; it is not physical-hardware evidence.
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
            log = wait_for(serial, "OZERO: framebuffer checksum=", process,
                           args.timeout)
            # The marker is emitted after the pattern write; allow the VGA
            # refresh to observe the final pixels before asking for a dump.
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
    if width % 8 != 0:
        raise SystemExit(
            f"framebuffer-check: FAIL width={width} is not divisible by 8")
    bar_width = width // 8
    samples: list[tuple[int, int, int]] = []
    for band in range(8):
        x = width * (2 * band + 1) // 16
        for y in (height // 4, height // 2, 3 * height // 4):
            actual = sample(pixels, width, x, y)
            want = expected_pixel(x, y, width)
            if max(abs(a - b) for a, b in zip(actual, want)) > TOLERANCE:
                raise SystemExit(
                    f"framebuffer-check: FAIL band={band} x={x} y={y} "
                    f"actual={actual} expected={want}")
        samples.append(sample(pixels, width, x, height // 2))

    if len(set(samples)) != 8:
        raise SystemExit(
            f"framebuffer-check: FAIL bands are not distinct: {samples}")
    for x in (0, width - 1):
        actual = sample(pixels, width, x, height // 2)
        want = expected_pixel(x, height // 2, width)
        if max(abs(a - b) for a, b in zip(actual, want)) > TOLERANCE:
            raise SystemExit(
                f"framebuffer-check: FAIL margin at x={x} actual={actual} "
                f"expected={want}")

    marker = re.findall(r"OZERO: framebuffer checksum=(0x[0-9a-fA-F]+)", log)
    if not marker:
        raise SystemExit("framebuffer-check: FAIL no kernel checksum marker")
    kernel_checksum = int(marker[-1], 16) & 0xFFFFFFFF
    screenshot_checksum = fnv(samples)
    if kernel_checksum != screenshot_checksum:
        raise SystemExit(
            "framebuffer-check: FAIL checksum mismatch "
            f"kernel={kernel_checksum:#010x} "
            f"screenshot={screenshot_checksum:#010x}")

    print(f"framebuffer-check: PASS width={width} height={height} "
          f"bar_width={bar_width} bars=8 "
          f"checksum={screenshot_checksum:#010x}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError) as error:
        print(f"framebuffer-check: FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
