#!/usr/bin/env bash
# Verify local tools against toolchain/toolchain.lock.
# Default: minimum versions (what CI may use). --strict: exact reference versions.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../toolchain/toolchain.lock
source "$ROOT/toolchain/toolchain.lock"

strict=0
[[ "${1:-}" == "--strict" ]] && strict=1

fail=0

version_of() {
  case "$1" in
    as)      as --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1 ;;
    ld)      ld --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1 ;;
    objcopy) objcopy --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1 ;;
    make)    make --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1 ;;
    bash)    bash --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1 ;;
    qemu)    qemu-system-x86_64 --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 ;;
  esac
}

version_ge() {
  [[ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" == "$2" ]]
}

check() {
  local tool="$1" actual="$2" minimum="$3" reference="$4"
  local required="$minimum"
  [[ $strict -eq 1 ]] && required="$reference"

  if [[ -z "$actual" ]]; then
    printf 'FAIL  %-8s not found\n' "$tool"
    fail=1
    return
  fi
  if [[ "$actual" == "$required" ]]; then
    printf 'ok    %-8s %s (exact)\n' "$tool" "$actual"
  elif version_ge "$actual" "$required"; then
    printf 'ok    %-8s %s (>= %s)\n' "$tool" "$actual" "$required"
  else
    printf 'FAIL  %-8s %s < required %s\n' "$tool" "$actual" "$required"
    fail=1
  fi
}

for cmd in as ld objcopy make bash git qemu-system-x86_64 od sha256sum stat tr cut grep awk sed sort seq tail date mktemp tar; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    printf 'FAIL  %-8s missing from PATH\n' "$cmd"
    fail=1
  fi
done

if [[ $fail -ne 0 ]]; then
  echo
  echo "Install the missing tools before building. On Debian:"
  echo "  sudo apt-get install -y binutils make qemu-system-x86 git"
  exit 1
fi

echo "toolchain lock: $ROOT/toolchain/toolchain.lock"
echo "mode: $([[ $strict -eq 1 ]] && echo strict || echo minimum)"
echo

check as      "$(version_of as)"      "$MIN_AS"      "$REF_AS"
check ld      "$(version_of ld)"      "$MIN_LD"      "$REF_LD"
check objcopy "$(version_of objcopy)" "$MIN_OBJCOPY" "$REF_OBJCOPY"
check make    "$(version_of make)"    "$MIN_MAKE"    "$REF_MAKE"
check bash    "$(version_of bash)"    "$MIN_BASH"    "$REF_BASH"
check qemu    "$(version_of qemu)"    "$MIN_QEMU"    "$REF_QEMU"

if [[ $strict -eq 1 ]]; then
  echo
  echo "Reference environment for recorded evidence: $REF_OS"
fi

if [[ $fail -ne 0 ]]; then
  exit 1
fi

echo
echo "check-toolchain: ok"
