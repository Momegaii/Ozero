#!/usr/bin/env python3
"""Download the pinned third-party samples declared in the corpus manifest.

Only entries that carry ``source.fetch.method == "http"`` are downloaded
automatically. Everything else prints the operator instruction from the
manifest, because several sources are interactive (a download page, a
proprietary installer) or need a package manager.

Downloaded files land in ``.cache/`` (git-ignored) and are verified against the
SHA-256 recorded in the manifest before being accepted.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import shutil
import sys
import urllib.error
import urllib.request
import zipfile
from pathlib import Path


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, destination: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "ozero-a2-corpus/1"})
    with urllib.request.urlopen(request, timeout=120) as response:
        destination.write_bytes(response.read())


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path("data/test-corpus.json"))
    parser.add_argument("--cache", type=Path, default=Path(".cache"))
    parser.add_argument("--only", action="append", default=[], help="entry id to fetch (repeatable)")
    args = parser.parse_args(argv)

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    args.cache.mkdir(parents=True, exist_ok=True)
    failures = 0
    fetched = 0
    skipped = 0

    for entry in manifest["entries"]:
        entry_id = entry["id"]
        if args.only and entry_id not in args.only:
            continue
        source = entry.get("source", {})
        fetch = source.get("fetch")
        expected = entry.get("sha256")
        if not fetch:
            if entry.get("hash_pending"):
                print(f"SKIP {entry_id}: {entry.get('hash_pending_reason', 'not pinned')}")
            else:
                print(f"SKIP {entry_id}: no automatic fetch recipe (source: {source.get('url', 'n/a')})")
            skipped += 1
            continue
        method = fetch.get("method")
        if method == "pip":
            print(f"MANUAL {entry_id}: {fetch.get('command')}")
            if fetch.get("then"):
                print(f"       then: {fetch.get('then')}")
            skipped += 1
            continue
        if method != "http":
            print(f"SKIP {entry_id}: unknown fetch method {method}")
            skipped += 1
            continue

        filename = fetch.get("filename") or Path(source["url"]).name
        output_name = fetch.get("output") or filename
        target = args.cache / output_name
        if target.exists() and expected and sha256_of(target) == expected:
            print(f"OK   {entry_id}: {target} already present and verified")
            continue

        staging = args.cache / (filename + ".part")
        try:
            print(f"GET  {entry_id}: {source['url']}")
            download(source["url"], staging)
        except (urllib.error.URLError, TimeoutError) as error:
            print(f"FAIL {entry_id}: download failed: {error}")
            failures += 1
            staging.unlink(missing_ok=True)
            continue

        try:
            member = fetch.get("member")
            if member:
                with zipfile.ZipFile(io.BytesIO(staging.read_bytes())) as archive:
                    target.write_bytes(archive.read(member))
                staging.unlink()
            else:
                shutil.move(str(staging), str(target))
        except (zipfile.BadZipFile, KeyError) as error:
            print(f"FAIL {entry_id}: cannot extract {fetch.get('member')}: {error}")
            failures += 1
            staging.unlink(missing_ok=True)
            continue

        actual = sha256_of(target)
        if expected and actual != expected:
            print(f"FAIL {entry_id}: sha256 mismatch\n     expected {expected}\n     actual   {actual}")
            failures += 1
            continue
        print(f"OK   {entry_id}: {target} sha256={actual}")
        fetched += 1

    print(f"\n{fetched} fetched, {skipped} skipped, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
