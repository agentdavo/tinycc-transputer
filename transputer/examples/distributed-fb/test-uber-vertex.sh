#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd)
R=${TCC_DIR:-$ROOT}; T4=${T4_BIN:-$ROOT/transputer/t4/t4}; WORK=$(mktemp -d); pid=
trap 'if [[ -n $pid ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; rm -rf "$WORK"' EXIT
cc -O2 -o "$WORK/host" "$HERE/test_uber_vertex.c" "$HERE/gles2_uber_vertex.c" "$HERE/gles2_uber_vm.c" "$HERE/glsl_vm.c" "$HERE/gles2_fragment.c" -lm; "$WORK/host"
"$R/transputer-tcc" -mt800 -static -nostdlib -I"$R/include" -DTG_TARGET_TEST=1 \
    -o "$WORK/test.elf" "$ROOT/transputer/tests/test_boot.S" "$HERE/test_uber_vertex.c" "$HERE/gles2_uber_vertex.c" "$HERE/gles2_uber_vm.c" "$HERE/glsl_vm.c" "$HERE/gles2_fragment.c" "$R/transputer-libtcc1-t800.a"
"$R/transputer-netload" "$WORK/test.elf" "$WORK/test.btl" >"$WORK/build.log" 2>&1
env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/test.uart" "$T4" -s8 -sm 26 -ss -sb "$WORK/test.btl" >"$WORK/t4.log" 2>&1 & pid=$!
for ((i=0;i<60;i++)); do
    if grep -qa 'VERTEX UBER ' "$WORK/test.uart" 2>/dev/null; then break; fi
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 0.5
done
if ! grep -qa 'VERTEX UBER OK' "$WORK/test.uart" 2>/dev/null; then cat "$WORK/test.uart" "$WORK/t4.log" >&2 2>/dev/null || true; exit 1; fi
echo 'uber vertex T800 tests OK'
