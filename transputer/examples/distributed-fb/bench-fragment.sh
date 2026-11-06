#!/usr/bin/env bash
# 1024 base and 1024 bump/spec/shadow fragment evaluations under T4's estimator.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd)
R=${TCC_DIR:-$ROOT}; T4=${T4_BIN:-$ROOT/transputer/t4/t4}; WORK=$(mktemp -d); pid=
trap 'if [[ -n $pid ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; rm -rf "$WORK"' EXIT
"$R/transputer-tcc" -mt800 -static -nostdlib -I"$R/include" -o "$WORK/bench.elf" \
    "$ROOT/transputer/tests/test_boot.S" "$HERE/bench_gles_fragment.c" "$HERE/gles2_fragment.c" "$R/transputer-libtcc1-t800.a"
"$R/transputer-netload" "$WORK/bench.elf" "$WORK/bench.btl" >"$WORK/build.log" 2>&1
env -u DISPLAY T4_EST_CYCLES=1 T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/bench.uart" "$T4" -s8 -sm 26 -ss -sb "$WORK/bench.btl" >"$WORK/t4.log" 2>&1 & pid=$!
for ((i=0;i<60;i++)); do
    if grep -qa 'FRAGBENCH full' "$WORK/bench.uart" 2>/dev/null; then break; fi
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 0.5
done
if ! grep -qaE 'FRAGBENCH full [0-9a-f]{8} [0-9a-f]{8} 00000000' "$WORK/bench.uart" 2>/dev/null; then
    cat "$WORK/bench.uart" "$WORK/t4.log" >&2 2>/dev/null || true; echo 'fragment benchmark failed' >&2; exit 1
fi
grep -aoE 'FRAGBENCH (base|full) [0-9a-f]{8} [0-9a-f]{8} [0-9a-f]{8}' "$WORK/bench.uart"
