#!/usr/bin/env bash
# Build / upload / monitor an avrboy sketch with the Arduino IDE's bundled
# arduino-cli.exe (run from WSL, so uploads go through the Windows COM port).
#
#   ./build.sh [compile|upload|monitor|all] [sketch]     (default: all ppu_demo)
#
# Env overrides: PORT (default COM3), OPT (default -O2), EXTRA_FLAGS (extra
# compiler flags, e.g. -DFOO=1), ARDUINO_CLI.
set -euo pipefail

ACTION=${1:-all}
SKETCH=${2:-ppu_demo}
PORT=${PORT:-COM3}
OPT=${OPT:--O2}
FQBN=arduino:avr:mega:cpu=atmega2560
CLI=${ARDUINO_CLI:-"/mnt/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"}

ROOT=$(cd "$(dirname "$0")" && pwd)
LIB="$ROOT/lib/TFT_HX8357"
if [[ ! -d "$LIB" ]]; then
    git clone --depth 1 https://github.com/Bodmer/TFT_HX8357 "$LIB"
fi

win() { wslpath -w "$1"; }

# cppu needs C++17 (the core's default is gnu++11). OPT replaces the core's
# -Os for speed; it is passed to the LTO link step too, since that is where
# the final code generation happens.
compile() {
    "$CLI" compile --fqbn "$FQBN" \
        --library "$(win "$LIB")" \
        --build-property "compiler.cpp.extra_flags=-std=gnu++17 $OPT ${EXTRA_FLAGS:-}" \
        --build-property "compiler.c.elf.extra_flags=$OPT" \
        --warnings default \
        "$@" "$(win "$ROOT/$SKETCH")"
}

case "$ACTION" in
    compile) compile ;;
    upload)  compile --upload --port "$PORT" ;;
    monitor) "$CLI" monitor --port "$PORT" --config baudrate=115200 ;;
    all)     compile --upload --port "$PORT"
             "$CLI" monitor --port "$PORT" --config baudrate=115200 ;;
    *) echo "usage: $0 [compile|upload|monitor|all] [sketch]" >&2; exit 1 ;;
esac
