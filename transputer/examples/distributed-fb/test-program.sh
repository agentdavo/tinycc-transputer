#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd)
R=${TCC_DIR:-$ROOT}; T4=${T4_BIN:-$ROOT/transputer/t4/t4}; WORK=$(mktemp -d); pid=
trap 'if [[ -n $pid ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; rm -rf "$WORK"' EXIT
"$R/transputer-tcc" -mt800 -static -nostdlib -I"$R/include" -I"$ROOT/transputer/native-headers" -DTG_TARGET_TEST=1 \
    -o "$WORK/test.elf" "$ROOT/transputer/tests/test_boot.S" "$HERE/test_gles_program.c" "$HERE/tinygl.c" "$HERE/gles2_subset.c" "$HERE/gles2_draw.c" "$HERE/gles2_program.c" "$R/transputer-libtcc1-t800.a"
"$R/transputer-netload" "$WORK/test.elf" "$WORK/test.btl" >"$WORK/build.log" 2>&1
env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/test.uart" "$T4" -s8 -sm 26 -ss -sb "$WORK/test.btl" >"$WORK/t4.log" 2>&1 & pid=$!
for ((i=0;i<60;i++)); do
    if grep -qa 'GLES PROGRAM ' "$WORK/test.uart" 2>/dev/null; then break; fi
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 0.5
done
if ! grep -qa 'GLES PROGRAM OK' "$WORK/test.uart" 2>/dev/null; then
    cat "$WORK/test.uart" "$WORK/t4.log" >&2; echo 'GLES program target test failed' >&2; exit 1
fi
echo 'GLES program target test OK'
