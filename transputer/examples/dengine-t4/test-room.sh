#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd)
R=${TCC_DIR:-$ROOT}; T4=${T4_BIN:-$ROOT/transputer/t4/t4}; WORK=$(mktemp -d); pid=
trap 'if [[ -n $pid ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; rm -rf "$WORK"' EXIT
PYTHONDONTWRITEBYTECODE=1 python3 "$HERE/test_room_export.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$HERE/export_room.py" "$ROOT/transputer/examples/dEngine" "$WORK/room_asset.h"
cc -O2 -I"$WORK" -o "$WORK/host" "$HERE/demo_room.c" "$HERE/test_demo_room.c"
"$WORK/host"
"$R/transputer-tcc" -mt800 -static -nostdlib -I"$R/include" -I"$WORK" -DTG_TARGET_TEST=1 -DTEST_LARGE_WORKSPACE=1 \
    -o "$WORK/test.elf" "$ROOT/transputer/tests/test_boot.S" "$HERE/demo_room.c" "$HERE/test_demo_room.c" "$R/transputer-libtcc1-t800.a"
"$R/transputer-netload" "$WORK/test.elf" "$WORK/test.btl" >"$WORK/build.log" 2>&1
env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/test.uart" "$T4" -s8 -sm 22 -ss -sb "$WORK/test.btl" >"$WORK/t4.log" 2>&1 & pid=$!
for ((i=0;i<60;i++)); do
    if grep -qa 'ROOM ' "$WORK/test.uart" 2>/dev/null; then break; fi
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 0.5
done
if ! grep -qa 'ROOM OK' "$WORK/test.uart" 2>/dev/null; then
    cat "$WORK/test.uart" "$WORK/t4.log" >&2 2>/dev/null || true; exit 1
fi
echo 'room clipping T800 tests OK'
