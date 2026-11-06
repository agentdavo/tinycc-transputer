#!/usr/bin/env bash
# Estimated T800 cycles for all 32 orientations and all four tiles.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
R=${TCC_DIR:-$ROOT}
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=$(mktemp -d)
pid=
trap 'if [[ -n $pid ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; rm -rf "$WORK"' EXIT
extra=()
if [[ ${SCALAR_CLEAR:-0} == 1 ]]; then extra=(-DTG_SCALAR_CLEAR=1); fi
if [[ ${NO_CULL:-0} == 1 ]]; then extra+=(-DTG_NO_CULL=1); fi
sources=("$HERE/bench.c" "$HERE/tinygl.c" "$HERE/gles2_subset.c" "$HERE/cube.c")
marker=CUBEBENCH
if [[ ${BENCH:-cube} == blit ]]; then sources=("$HERE/bench_blit.c"); marker=BLITBENCH; fi
"$R/transputer-tcc" -mt800 -static -nostdlib -I"$R/include" "${extra[@]}" -o "$WORK/bench.elf" \
    "$ROOT/transputer/tests/test_boot.S" "${sources[@]}" "$R/transputer-libtcc1-t800.a"
"$R/transputer-netload" "$WORK/bench.elf" "$WORK/bench.btl" >"$WORK/build.log" 2>&1
env -u DISPLAY T4_EST_CYCLES=1 T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/bench.uart" \
    "$T4" -s8 -sm 26 -ss -sb "$WORK/bench.btl" >"$WORK/t4.log" 2>&1 &
pid=$!
for ((i=0; i<180; ++i)); do
    if grep -aq "$marker " "$WORK/bench.uart" 2>/dev/null; then break; fi
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 0.5
done
if ! grep -aq "$marker " "$WORK/bench.uart" 2>/dev/null; then
    cat "$WORK/t4.log" >&2; echo "$marker failed" >&2; exit 1
fi
result=$(grep -ao "$marker [0-9a-f ]*" "$WORK/bench.uart")
echo "$result"
if [[ $marker == BLITBENCH && $result != *' 00000000' ]]; then
    echo 'MOVE2D pixel verification failed' >&2; exit 1
fi
