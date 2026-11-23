#!/usr/bin/env bash
# Prove that a fresh clone of HEAD builds and passes with no local state.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! git rev-parse --verify HEAD >/dev/null 2>&1; then
  echo "verify-clean: FAIL no commit yet; commit the tree first" >&2
  exit 2
fi

commit="$(git rev-parse HEAD)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "verify-clean: cloning $commit into a temporary directory"
git clone --quiet --no-local "$ROOT" "$tmp/repo"

echo "verify-clean: running make ci in the clone"
(
  cd "$tmp/repo"
  make ci
)

echo "verify-clean: ok at $commit"
