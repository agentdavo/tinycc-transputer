#!/usr/bin/env bash
# bench_softfp.c on a strict T425 or T414 (t4 -s425/-s414): timer ticks per
# phase, the results' hash, and t4's instruction profile of the whole run.
#
# usage: bash transputer/tests/bench/softfp.sh [t425|t414] [cflags...]
# env:   TCC_DIR (transputer-tcc, include/, the libtcc1 archives; default
#        this tree), T4_BIN, WORK (keeps bench.elf, the UART log and t4's
#        profile there)
# The ticks are wall-clock time on t4.  The cycle figures in CLAUDE.md
# came from a t4 built with T4PROFILE and a T4_EST_TIMER=1 knob that makes
# ldtimer count estimated T414 cycles (TCWG timings), which the in-tree
# release build does not have: run it with T4_BIN pointing at such a t4.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
CPU=${1:-t425}
shift
WORK=${WORK:-$(mktemp -d)}
cd "$WORK" || exit 1
printf 'extern int main(void);\nvoid test_main(void) { (void)main(); }\n' > wrap.c
"$R/transputer-tcc" -m$CPU "$@" -static -nostdlib -I"$R/include/transputer" -I"$R/include" \
    -o bench.elf "$T/test_boot.S" "$HERE/bench_softfp.c" wrap.c \
    "$R/transputer-libtcc1-$CPU.a" &&
"$R/transputer-netload" bench.elf bench.btl >/dev/null 2>&1 || { echo "build failed"; exit 1; }
rm -f profile
env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=bench.uart \
    timeout -k 5 600s "$T4" -s${CPU#t} -sm 26 -su -ss -sb bench.btl >/dev/null 2>&1
tr -d 'SH' < bench.uart
sed -n 's/^Instructions[[:space:]]*/instructions /p' profile 2>/dev/null
