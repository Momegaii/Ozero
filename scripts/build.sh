#!/usr/bin/env bash
# Build the Stage 0 boot fixture. Deterministic: same input, same bytes.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# shellcheck source=../toolchain/toolchain.lock
source "$ROOT/toolchain/toolchain.lock"

BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
mkdir -p "$BUILD_DIR"

export LC_ALL=C
export TZ=UTC
export SOURCE_DATE_EPOCH="$TOOLCHAIN_EPOCH"
umask 022

AS="${AS:-as}"
LD="${LD:-ld}"
OBJCOPY="${OBJCOPY:-objcopy}"

echo "build: assembling harness/fixture/boot.S"
"$AS" --32 -g --fatal-warnings --debug-prefix-map="$PWD"=. -o "$BUILD_DIR/boot.o" harness/fixture/boot.S

echo "build: linking at 0x7C00"
"$LD" -m elf_i386 --build-id=none --fatal-warnings \
  -T harness/fixture/boot.ld \
  -Map "$BUILD_DIR/ozero-stage0.map" \
  -o "$BUILD_DIR/ozero-stage0.elf" \
  "$BUILD_DIR/boot.o"

"$OBJCOPY" -O binary "$BUILD_DIR/ozero-stage0.elf" "$BUILD_DIR/ozero-stage0.img"

img="$BUILD_DIR/ozero-stage0.img"
size="$(stat -c %s "$img")"
if [[ "$size" -ne 512 ]]; then
  echo "build: FAIL image is $size bytes, expected 512" >&2
  exit 1
fi

sig="$(od -An -tx1 -j510 -N2 "$img" | tr -d ' \n')"
if [[ "$sig" != "55aa" ]]; then
  echo "build: FAIL missing 55 AA boot signature (got $sig)" >&2
  exit 1
fi

img_sha="$(sha256sum "$img" | cut -d' ' -f1)"
elf_sha="$(sha256sum "$BUILD_DIR/ozero-stage0.elf" | cut -d' ' -f1)"
commit="$(git -C "$ROOT" rev-parse --verify --quiet HEAD 2>/dev/null || echo unknown)"

cat > "$BUILD_DIR/manifest.json" <<JSON
{
  "schema": "ozero.build.manifest/v1",
  "commit": "$commit",
  "source_date_epoch": $TOOLCHAIN_EPOCH,
  "image": "ozero-stage0.img",
  "image_bytes": $size,
  "image_sha256": "$img_sha",
  "elf": "ozero-stage0.elf",
  "elf_sha256": "$elf_sha",
  "toolchain": {
    "as": "$(as --version | head -1)",
    "ld": "$(ld --version | head -1)",
    "objcopy": "$(objcopy --version | head -1)"
  }
}
JSON

echo "build: ok"
echo "  image  $BUILD_DIR/ozero-stage0.img  ($size bytes)"
echo "  sha256 $img_sha"
echo "  elf    $BUILD_DIR/ozero-stage0.elf  (debug symbols retained)"
echo "  map    $BUILD_DIR/ozero-stage0.map"
echo "  commit $commit"
