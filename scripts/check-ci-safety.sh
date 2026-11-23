#!/usr/bin/env bash
# Reject CI steps that need privileges, raw disks or third-party Windows binaries.
# Static check only: it reduces risk, it does not prove absence.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

files=()
while IFS= read -r f; do
  files+=("$f")
done < <(find "$ROOT/.github/workflows" "$ROOT/scripts" -maxdepth 1 -type f \
  \( -name '*.yml' -o -name '*.yaml' -o -name '*.sh' \) \
  ! -name 'check-ci-safety.sh' | sort)

if [[ ${#files[@]} -eq 0 ]]; then
  echo "check-ci-safety: no workflow or script files found" >&2
  exit 2
fi

fail=0

check() {
  local name="$1" re="$2" hits
  hits="$(grep -nE -- "$re" "${files[@]}" 2>/dev/null || true)"
  if [[ -n "$hits" ]]; then
    printf 'FAIL  %s\n%s\n' "$name" "$hits"
    fail=1
  fi
}

check "privileged container"        '--privileged'
check "docker socket mount"         'docker\.sock'
check "host device passthrough"     '--device(=|[[:space:]])'
check "added capabilities"          '--cap-add'
check "raw block device write"      '(dd|cat|tee)[^|]*of=/dev/(sd|nvme|loop|mmcblk|disk)'
check "loop device setup"           '\blosetup\b'
check "filesystem creation"         '\bmkfs(\.[a-z0-9]+)?\b'
check "partition table edit"        '\b(fdisk|parted|sgdisk|gdisk)\b'
check "kernel memory device"        '/dev/(mem|kmem|port)\b'
check "Windows binary execution"    '\bwine(64)?\b'
check "direct .exe execution"       '(^|[;&|[:space:]])(\./)?[^[:space:]]+\.exe([[:space:]]|$)'
check "curl piped to shell"         'curl[^|]*\|[[:space:]]*(ba)?sh'
check "wget piped to shell"         'wget[^|]*\|[[:space:]]*(ba)?sh'

if [[ $fail -ne 0 ]]; then
  echo
  echo "check-ci-safety: FAIL" >&2
  echo "Normal CI must not use privileges, raw disks or third-party Windows binaries." >&2
  echo "See docs/process/ci-safety.md." >&2
  exit 1
fi

echo "check-ci-safety: ok (${#files[@]} files scanned)"
