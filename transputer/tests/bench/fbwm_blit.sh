#!/usr/bin/env bash
# Benchmark the real compositor blit. Usage: fbwm_blit.sh [cpu] [row|move]
# TCC_DIR/T4_BIN select the compiler and emulator; WORK retains artifacts.
# EXTRA_CFLAGS=-DTPOS_SPINE_CHIP tests the alternate address profile.
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
CPU=${1:-t800}
MODE=${2:-move}
case $CPU in
    t9000) S4=-s9; LIB=transputer-libtcc1.a ;;
    t805|t800) S4=-s8; LIB=transputer-libtcc1-t800.a ;;
    t425|t414) S4=-s${CPU#t}; LIB=transputer-libtcc1-$CPU.a ;;
    *) echo "unsupported target $CPU" >&2; exit 1 ;;
esac
case $MODE in
    row) FEATURE= ;;
    move) [ "$CPU" != t414 ] || { echo 'T414 has no MOVE2D' >&2; exit 1; }
          FEATURE=-DTPOS_HAVE_MOVE2D=1 ;;
    *) echo "unsupported mode $MODE" >&2; exit 1 ;;
esac
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
"$R/transputer-tcc" -m"$CPU" $FEATURE ${EXTRA_CFLAGS:-} -nostdlib -nostdinc \
    -I"$ROOT/transputer/native-headers" -I"$ROOT/transputer/fbwm" \
    -o "$WORK/bench.elf" "$ROOT/transputer/tests/test_boot.S" \
    "$HERE/bench_fbwm_blit.c" "$R/$LIB"
"$R/transputer-netload" "$WORK/bench.elf" "$WORK/bench.btl" >/dev/null 2>&1
status=0
env -u DISPLAY T4_EST_CYCLES=1 T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/bench.uart" \
    timeout -k 5 90s "$T4" "$S4" -sm 26 -ss -sb "$WORK/bench.btl" \
    >"$WORK/t4.log" 2>&1 || status=$?
[ "$status" -ne 124 ] && [ "$status" -ne 137 ] || { cat "$WORK/t4.log" >&2; exit "$status"; }
tr -d 'SH\000' < "$WORK/bench.uart"
grep -q '^fail 0' "$WORK/bench.uart"
grep -q '^slot3_overflow 0' "$WORK/bench.uart"
