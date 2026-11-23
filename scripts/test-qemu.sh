#!/usr/bin/env bash
# QEMU boot smoke test.
#
# Boots an image, captures COM1, and requires every marker in order. Fails with
# exit 1 on a boot failure and exit 2 on a harness error. All artifacts stay in
# OUTDIR, including on failure.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

IMAGE="${IMAGE:-$ROOT/build/ozero-stage0.img}"
OUTDIR="${OUTDIR:-$ROOT/build/qemu}"
TIMEOUT="${TIMEOUT:-20}"
MARKERS="${MARKERS:-$ROOT/tests/qemu/expected-markers.txt}"
QEMU="${QEMU:-qemu-system-x86_64}"

usage() {
  cat <<'USAGE'
Usage: scripts/test-qemu.sh [options]

  --image PATH     image to boot        (env IMAGE,    default build/ozero-stage0.img)
  --outdir DIR     artifact directory   (env OUTDIR,   default build/qemu)
  --timeout SECS   boot timeout         (env TIMEOUT,  default 20)
  --markers FILE   required markers     (env MARKERS,  default tests/qemu/expected-markers.txt)
  --qemu PATH      qemu binary          (env QEMU,     default qemu-system-x86_64)

Exit codes: 0 pass, 1 boot/test failure, 2 harness error.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --image)   IMAGE="$2"; shift 2 ;;
    --outdir)  OUTDIR="$2"; shift 2 ;;
    --timeout) TIMEOUT="$2"; shift 2 ;;
    --markers) MARKERS="$2"; shift 2 ;;
    --qemu)    QEMU="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "test: harness error: unknown argument $1" >&2; usage >&2; exit 2 ;;
  esac
done

die() { echo "test: harness error: $*" >&2; exit 2; }

[[ "$TIMEOUT" =~ ^[0-9]+$ ]] || die "timeout must be a whole number of seconds"
[[ -f "$IMAGE" ]] || die "image not found: $IMAGE"
[[ -f "$MARKERS" ]] || die "markers file not found: $MARKERS"
command -v "$QEMU" >/dev/null 2>&1 || die "QEMU not found: $QEMU (apt-get install qemu-system-x86)"

size="$(stat -c %s "$IMAGE")"
[[ "$size" -gt 0 ]] || die "image is empty: $IMAGE"
[[ $((size % 512)) -eq 0 ]] || die "image size $size is not a multiple of 512"

if [[ "$size" -ge 512 ]]; then
  sig="$(od -An -tx1 -j510 -N2 "$IMAGE" | tr -d ' \n')"
  [[ "$sig" == "55aa" ]] || die "image has no 55 AA boot signature (got $sig)"
fi

mapfile -t markers < <(sed -e 's/#.*//' -e 's/[[:space:]]*$//' "$MARKERS" | grep -v '^[[:space:]]*$')
[[ ${#markers[@]} -gt 0 ]] || die "no markers defined in $MARKERS"

mkdir -p "$OUTDIR"
SERIAL="$OUTDIR/serial.log"
DEBUG="$OUTDIR/qemu-debug.log"
STDOUT="$OUTDIR/qemu.stdout"
STDERR="$OUTDIR/qemu.stderr"
: > "$SERIAL"
: > "$DEBUG"
: > "$STDOUT"
: > "$STDERR"

qemu_args=(
  -machine pc
  -accel tcg,thread=single
  -m 128M
  -nodefaults
  -display none
  -monitor none
  -nic none
  -no-reboot
  -boot c
  -serial "file:$SERIAL"
  -drive "file=$IMAGE,format=raw,if=ide,index=0,media=disk"
  -device isa-debug-exit,iobase=0x501,iosize=0x04
  -d guest_errors
  -D "$DEBUG"
)

qemu_version="$("$QEMU" --version | head -1)"
start_ms="$(date +%s%3N)"

"$QEMU" "${qemu_args[@]}" >"$STDOUT" 2>"$STDERR" &
pid=$!

missing=()
out_of_order=()

check_markers() {
  missing=()
  out_of_order=()
  local last=0 line m
  for m in "${markers[@]}"; do
    line="$(grep -nF -m1 -- "$m" "$SERIAL" 2>/dev/null | cut -d: -f1 || true)"
    if [[ -z "$line" ]]; then
      missing+=("$m")
      return 1
    fi
    if (( line < last )); then
      out_of_order+=("$m")
      return 1
    fi
    last="$line"
  done
  return 0
}

qemu_alive() {
  kill -0 "$pid" 2>/dev/null || return 1
  local state
  state="$(awk '{print $3}' "/proc/$pid/stat" 2>/dev/null || echo X)"
  [[ "$state" != "Z" ]]
}

status="timeout"
while :; do
  if check_markers; then
    status="pass"
    break
  fi
  if ! qemu_alive; then
    if check_markers; then status="pass"; else status="failed"; fi
    break
  fi
  now_ms="$(date +%s%3N)"
  if (( now_ms - start_ms >= TIMEOUT * 1000 )); then
    if check_markers; then status="pass"; else status="timeout"; fi
    break
  fi
  sleep 0.1
done

if kill -0 "$pid" 2>/dev/null; then
  kill -TERM "$pid" 2>/dev/null || true
  for _ in $(seq 1 50); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.1
  done
  kill -KILL "$pid" 2>/dev/null || true
fi
wait "$pid" 2>/dev/null || true

end_ms="$(date +%s%3N)"
duration_ms=$(( end_ms - start_ms ))

json_array() {
  local first=1 a
  printf '['
  for a in "$@"; do
    (( first )) || printf ','
    first=0
    a="${a//\\/\\\\}"
    a="${a//\"/\\\"}"
    printf '"%s"' "$a"
  done
  printf ']'
}

image_sha="$(sha256sum "$IMAGE" | cut -d' ' -f1)"
image_rel="${IMAGE#"$ROOT"/}"
serial_rel="${SERIAL#"$ROOT"/}"
debug_rel="${DEBUG#"$ROOT"/}"

# Keep evidence portable: no absolute workspace paths in the recorded command.
display_args=()
for arg in "$QEMU" "${qemu_args[@]}"; do
  display_args+=("${arg//"$ROOT"\//}")
done

{
  printf '{\n'
  printf '  "schema": "ozero.test.result/v1",\n'
  printf '  "test": "qemu-boot-smoke",\n'
  printf '  "status": "%s",\n' "$status"
  printf '  "image": "%s",\n' "$image_rel"
  printf '  "image_sha256": "%s",\n' "$image_sha"
  printf '  "qemu_version": "%s",\n' "${qemu_version//\"/\\\"}"
  printf '  "timeout_s": %s,\n' "$TIMEOUT"
  printf '  "duration_ms": %s,\n' "$duration_ms"
  printf '  "markers_expected": '
  json_array "${markers[@]}"
  printf ',\n  "markers_missing": '
  json_array "${missing[@]}"
  printf ',\n  "markers_out_of_order": '
  json_array "${out_of_order[@]}"
  printf ',\n  "serial_log": "%s",\n' "$serial_rel"
  printf '  "debug_log": "%s",\n' "$debug_rel"
  printf '  "command": '
  json_array "${display_args[@]}"
  printf '\n}\n'
} > "$OUTDIR/result.json"

{
  echo "qemu boot smoke test"
  echo "status:   $status"
  echo "image:    $image_rel"
  echo "sha256:   $image_sha"
  echo "qemu:     $qemu_version"
  echo "timeout:  ${TIMEOUT}s"
  echo "duration: ${duration_ms}ms"
  echo "serial:   $serial_rel"
  if [[ ${#missing[@]} -gt 0 ]]; then
    echo "missing:  ${missing[*]}"
  fi
  if [[ ${#out_of_order[@]} -gt 0 ]]; then
    echo "order:    ${out_of_order[*]}"
  fi
} > "$OUTDIR/summary.txt"

cat "$OUTDIR/summary.txt"

if [[ "$status" != "pass" ]]; then
  echo >&2
  echo "test: FAIL ($status)" >&2
  if [[ ${#missing[@]} -gt 0 ]]; then
    echo "  missing markers: ${missing[*]}" >&2
  fi
  if [[ ${#out_of_order[@]} -gt 0 ]]; then
    echo "  out of order:    ${out_of_order[*]}" >&2
  fi
  echo "  serial log:      $SERIAL" >&2
  echo "  qemu stderr:     $STDERR" >&2
  echo "  last serial lines:" >&2
  tail -n 20 "$SERIAL" >&2 || true
  exit 1
fi

echo "test: ok (${#markers[@]} markers in order)"
