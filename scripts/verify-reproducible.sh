#!/usr/bin/env bash
# Prove that two clean builds of the same commit are bit-identical.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! git rev-parse --verify HEAD >/dev/null 2>&1; then
  echo "verify-repro: FAIL no commit yet; commit the tree first" >&2
  exit 2
fi

commit="$(git rev-parse HEAD)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

for n in 1 2; do
  mkdir -p "$tmp/$n"
  git archive --format=tar HEAD | tar -x -C "$tmp/$n"
  echo "verify-repro: building copy $n"
  BUILD_DIR="$tmp/$n/build" "$tmp/$n/scripts/build.sh" >/dev/null
done

fail=0
for artifact in ozero-stage0.img ozero-stage0.elf manifest.json; do
  a="$(sha256sum "$tmp/1/build/$artifact" | cut -d' ' -f1)"
  b="$(sha256sum "$tmp/2/build/$artifact" | cut -d' ' -f1)"
  if [[ "$a" == "$b" ]]; then
    printf 'ok    %-18s %s\n' "$artifact" "$a"
  else
    printf 'FAIL  %-18s %s != %s\n' "$artifact" "$a" "$b"
    fail=1
  fi
done

if [[ $fail -ne 0 ]]; then
  echo "verify-repro: FAIL builds differ at $commit" >&2
  exit 1
fi

echo "verify-repro: ok at $commit"
