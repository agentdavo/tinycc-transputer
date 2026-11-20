#!/usr/bin/env bash
# bench_memset.c for one target, on t4 with T4_EST_CYCLES: every
# implementation checked, then Doom's three memset shapes and a size sweep
# (aligned/odd address) per call, as estimated T800 cycles (=1) and as
# instructions executed (=2).  The T800 cycle figures stand for the
# T800-family timing of what ran; for -mt9000 code read the instruction
# line too.
#
# usage: bash transputer/tests/bench/memset.sh [t9000|t805|t800|t425|t414] [cflags...]
# env:   TCC_DIR (transputer-tcc, include/, libtcc1 archives; default this
#        tree), T4_BIN (a t4 with T4_EST_CYCLES), WORK (keeps the build),
#        LIBC (a uClibc libc.a for the part: its memset is checked and
#        timed too, as "libc")
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer
T4=${T4_BIN:-$T/t4/t4}
CPU=${1:-t9000}
[ "$#" -eq 0 ] || shift
case $CPU in
    t9000) S4=-s9; LIB=transputer-libtcc1.a ;;
    t805|t800) S4=-s8; LIB=transputer-libtcc1-t800.a ;;
    t425|t414) S4=-s${CPU#t}; LIB=transputer-libtcc1-$CPU.a ;;
    *) echo "unknown target $CPU"; exit 1 ;;
esac
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
cd "$WORK"
printf 'extern int main(void);\nvoid test_main(void) { (void)main(); }\n' > wrap.c
EXTRA=()
[ -z "${LIBC:-}" ] || EXTRA=(-DWITH_LIBC "$LIBC")
T4_STRICT_UNRESOLVED_CALLS=1 "$R/transputer-tcc" -m$CPU -w "$@" -static -nostdlib \
    -I"$R/include/transputer" -I"$R/include" -o bench.elf "$T/tests/test_boot.S" \
    "$HERE/bench_memset.c" wrap.c "${EXTRA[@]}" "$R/$LIB" &&
"$R/transputer-netload" bench.elf bench.btl >/dev/null 2>&1 || { echo "build failed"; exit 1; }
for m in 1 2; do
    : > "bench$m.uart"
    env -u DISPLAY T4_EST_CYCLES=$m T4_UART_CONSOLE=0 T4_UART_LOG=bench$m.uart \
        timeout -k 5 600s "$T4" $S4 -sm 24 -ss -sb bench.btl >t4-$m.log 2>&1 || true
done
echo "== $CPU cycles (T800 model)"; tr -d '\0' < bench1.uart | tr -d 'SH' | sed '/^$/d'
echo "== $CPU instructions";        tr -d '\0' < bench2.uart | tr -d 'SH' | sed '/^$/d'
