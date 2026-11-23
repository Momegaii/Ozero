#!/usr/bin/env python3
"""Build the PE fixtures and record reproducible provenance.

The manifest written to ``fixtures/build-manifest.json`` is the record the
review asked for: source hash, exact command, compiler version, target, output
hash and the expected observable behaviour for every fixture.

Use ``--check`` to rebuild and compare against the committed manifest; a
difference means the toolchain changed and the evidence must be regenerated.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

FIXTURES = [
    {
        "id": "console_hello",
        "source": "fixtures/src/console_hello.c",
        "output": "build/console_hello.exe",
        "language": "c",
        "flags": ["-O2", "-s"],
        "expected": {
            "level": "L2",
            "stdout": "a2 console fixture: compute=22 module=<non-null pointer>",
            "stdout_match": "prefix:a2 console fixture: compute=22 module=0x",
            "exit_code": 0,
        },
    },
    {
        "id": "fileproc",
        "source": "fixtures/src/fileproc.c",
        "output": "build/fileproc.exe",
        "language": "c",
        "flags": ["-O2", "-s"],
        "expected": {
            "level": "L2",
            "stdout": "a2 file/process fixture: read=4 written=4",
            "stdout_match": "exact:a2 file/process fixture: read=4 written=4",
            "exit_code": 0,
        },
    },
    {
        "id": "gui_messagebox",
        "source": "fixtures/src/gui_messagebox.c",
        "output": "build/gui_messagebox.exe",
        "language": "c",
        "flags": ["-O2", "-s", "-Wl,--subsystem,windows"],
        "expected": {
            "level": "L3",
            "stdout": None,
            "stdout_match": None,
            "exit_code": 0,
            "notes": "A message box titled 'Ozero A2' must appear and dismiss with OK.",
        },
    },
    {
        "id": "tls_dll",
        "source": "fixtures/src/tls_dll.c",
        "output": "build/tls_dll.dll",
        "language": "c",
        "flags": ["-O2", "-s", "-shared"],
        "expected": {
            "level": "L2",
            "stdout": None,
            "stdout_match": None,
            "exit_code": 0,
            "notes": "Host process calls a2_add(2,3)=5 and a2_get_counter()=8 after the TLS callback runs.",
        },
    },
    {
        "id": "cpp_throw",
        "source": "fixtures/src/cpp_throw.cpp",
        "output": "build/cpp_throw.exe",
        "language": "cpp",
        "flags": ["-O2", "-s", "-fexceptions", "-lc++", "-lc++abi"],
        "expected": {
            "level": "L2",
            "stdout": "a2 c++ fixture: caught=42",
            "stdout_match": "exact:a2 c++ fixture: caught=42",
            "exit_code": 0,
        },
    },
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def compiler_version(command: list[str]) -> str:
    if "ziglang" in command:
        probe = [part for part in command if part != "cc"]
        probe.append("version")
    else:
        probe = [command[0], "--version"]
    try:
        completed = subprocess.run(probe, capture_output=True, text=True, timeout=120)
    except (OSError, subprocess.SubprocessError) as error:
        return f"unknown ({error})"
    text = (completed.stdout or completed.stderr).strip().splitlines()
    return text[0] if text else "unknown"


def build_one(root: Path, compiler: list[str], target: str, fixture: dict) -> dict:
    source = root / fixture["source"]
    output = root / fixture["output"]
    output.parent.mkdir(parents=True, exist_ok=True)
    command = compiler + ["-target", target] + fixture["flags"] + [str(source), "-o", str(output)]
    completed = subprocess.run(command, capture_output=True, text=True, cwd=root)
    if completed.returncode != 0 or not output.exists():
        raise RuntimeError(
            f"{fixture['id']} failed with exit {completed.returncode}\n"
            f"command: {shlex.join(command)}\n{completed.stdout}\n{completed.stderr}"
        )
    return {
        "id": fixture["id"],
        "source": fixture["source"],
        "source_sha256": sha256(source),
        "language": fixture["language"],
        "command": shlex.join(command),
        "target": target,
        "output": fixture["output"],
        "output_sha256": sha256(output),
        "output_size": output.stat().st_size,
        "expected": fixture["expected"],
        "build_warnings": completed.stderr.strip().splitlines(),
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--cc", default="", help="compiler command, e.g. 'python -m ziglang cc'")
    parser.add_argument("--target", default="x86_64-windows-gnu")
    parser.add_argument("--manifest", type=Path, default=Path("fixtures/build-manifest.json"))
    parser.add_argument("--check", action="store_true", help="compare with the existing manifest instead of writing")
    args = parser.parse_args(argv)

    root = args.root.resolve()
    if args.cc:
        compiler = shlex.split(args.cc)
    elif shutil.which("zig"):
        compiler = ["zig", "cc"]
    else:
        compiler = [sys.executable, "-m", "ziglang", "cc"]

    version = compiler_version(compiler)
    entries = []
    try:
        for fixture in FIXTURES:
            entries.append(build_one(root, compiler, args.target, fixture))
    except RuntimeError as error:
        print(f"BUILD FAILED\n{error}", file=sys.stderr)
        return 1

    manifest = {
        "schema_version": 1,
        "generated_at": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "compiler": {"command": shlex.join(compiler), "version": version},
        "target": args.target,
        "fixtures": entries,
    }

    if args.check:
        existing_path = root / args.manifest
        if not existing_path.exists():
            print(f"no manifest at {existing_path}", file=sys.stderr)
            return 1
        existing = json.loads(existing_path.read_text(encoding="utf-8"))
        old = {entry["id"]: entry["output_sha256"] for entry in existing.get("fixtures", [])}
        new = {entry["id"]: entry["output_sha256"] for entry in entries}
        drift = [key for key in new if old.get(key) != new[key]]
        if existing.get("compiler", {}).get("version") != version:
            print(f"compiler drift: {existing.get('compiler', {}).get('version')} -> {version}")
        if drift:
            print(f"output drift: {', '.join(drift)}")
            return 1
        print(f"manifest matches: {len(entries)} fixtures, compiler {version}")
        return 0

    manifest_path = root / args.manifest
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    for entry in entries:
        print(f"{entry['id']:<16} {entry['output_size']:>7} bytes  {entry['output_sha256'][:16]}…")
    print(f"compiler {version}; manifest written to {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
