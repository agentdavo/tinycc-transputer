#!/usr/bin/env bash
# Compare padded surface copies and check the entire destination and source.
# Usage: bash transputer/tests/bench/move2d.sh [t9000|t805|t800|t425]
# TCC_DIR and T4_BIN select a built compiler and t4. WORK retains artifacts.
# MOVE2D estimates require T4 commit 8177e21 or later. Earlier T4 versions
# omit transfer work; partial and unaligned row estimates remain approximate.
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
CPU=${1:-t800}
case $CPU in
    t9000) S4=-s9; LIB=transputer-libtcc1.a ;;
    t805|t800) S4=-s8; LIB=transputer-libtcc1-t800.a ;;
    t425) S4=-s425; LIB=transputer-libtcc1-t425.a ;;
    *) echo "MOVE2D is unavailable or unsupported for $CPU" >&2; exit 1 ;;
esac
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
"$R/transputer-tcc" -m"$CPU" -nostdlib -nostdinc -o "$WORK/bench.elf" \
    "$ROOT/transputer/tests/test_boot.S" "$HERE/bench_move2d.c" "$R/$LIB"
"$R/transputer-netload" "$WORK/bench.elf" "$WORK/bench.btl" >/dev/null 2>&1
status=0
env -u DISPLAY T4_EST_CYCLES=1 T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/bench.uart" \
    timeout -k 5 90s "$T4" "$S4" -sm 24 -ss -sb "$WORK/bench.btl" \
    >"$WORK/t4.log" 2>&1 || status=$?
[ "$status" -ne 124 ] && [ "$status" -ne 137 ] || { cat "$WORK/t4.log" >&2; exit "$status"; }
tr -d 'SH\000' < "$WORK/bench.uart"
grep -q '^fail 0' "$WORK/bench.uart"
printf '%s\n' 'MOVE2D estimate requires T4 8177e21 or later; unaligned rows are approximate.'
