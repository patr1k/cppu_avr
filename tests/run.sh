#!/usr/bin/env bash
# Builds and runs tests/scanline_equiv.cpp in both PPU configurations.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/avrboy-tests
mkdir -p "$OUT"
SRCS=("$ROOT/tests/scanline_equiv.cpp" "$ROOT/ppu_demo/src/cppu/ppu.cpp" "$ROOT/ppu_demo/src/demo/demo.cpp")
FLAGS=(-std=c++20 -O2 -Wall -Wextra -I"$ROOT/ppu_demo/src")
g++ "${FLAGS[@]}" "${SRCS[@]}" -o "$OUT/equiv_desktop"
g++ "${FLAGS[@]}" -DCPPU_LINE_OUTPUT=1 -DCPPU_COMPACT_VRAM=1 "${SRCS[@]}" -o "$OUT/equiv_avr"
"$OUT/equiv_desktop"
"$OUT/equiv_avr"
