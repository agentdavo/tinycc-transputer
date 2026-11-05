#!/usr/bin/env bash
# bench_doom.c (Doom's column/span drawers, FixedMul/FixedDiv and the
# presentation copies) for one target, on t4 with T4_EST_CYCLES: each phase
# as estimated T800 cycles and as executed instructions, with its hash.
#
# usage: bash transputer/tests/bench/doom.sh [t9000|t805|t800|t425|t414] [cflags...]
# env:   TCC_DIR (transputer-tcc, include/, the libtcc1 archives; default
#        this tree), T4_BIN (a t4 with T4_EST_CYCLES: t4 daa9efa or later,
#        built with make -f Makefile.linux; default the in-tree binary),
#        UCLIBC (uClibc-ng tree for Doom's <stdio.h>; in WSL use a copy whose
#        symlinked headers are real files), WORK (keeps bench.elf and logs)
# The cycle column is the T800 data sheet timing of what ran (see
# transputer/t4/t800_cycles.h); for -mt9000 code read it as a T800-family
# figure, the instruction column applies to every part.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer
T4=${T4_BIN:-$T/t4/t4}
U=${UCLIBC:-$T/uClibc-ng}
DS=$T/doom/linuxdoom-1.10
CPU=${1:-t9000}
shift
case $CPU in
    t9000) S4=-s9; LIB=transputer-libtcc1.a ;;
    t805|t800) S4=-s8; LIB=transputer-libtcc1-t800.a ;;
    t425|t414) S4=-s${CPU#t}; LIB=transputer-libtcc1-$CPU.a ;;
    *) echo "unknown target $CPU"; exit 1 ;;
esac
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
cd "$WORK" || exit 1
printf 'extern int main(void);\nvoid test_main(void) { (void)main(); }\n' > wrap.c
"$R/transputer-tcc" -m$CPU -w "$@" -D__GNUC__=4 -D__GNUC_MINOR__=4 \
    -D__GNUC_PATCHLEVEL__=0 -DNORMALUNIX -DTRANSPUTER \
    -include "$DS/doom_tp_config.h" -nostdinc -I"$R/include/transputer" \
    -I"$R/include" -I"$U/include" -I"$U/libc/sysdeps/linux/transputer" \
    -I"$U/libc/sysdeps/linux/common" -I"$T/linux-2.6.39/include" -I"$DS" \
    -c -o bench.o "$HERE/bench_doom.c" &&
"$R/transputer-tcc" -m$CPU -c -o m_fixed_tp.o "$DS/m_fixed_tp.S" &&
"$R/transputer-tcc" -m$CPU -static -nostdlib -o bench.elf "$T/tests/test_boot.S" \
    bench.o m_fixed_tp.o wrap.c "$R/$LIB" &&
"$R/transputer-netload" bench.elf bench.btl >/dev/null 2>&1 || { echo "build failed"; exit 1; }
for m in 1 2; do
    env -u DISPLAY T4_EST_CYCLES=$m T4_UART_CONSOLE=0 T4_UART_LOG=bench$m.uart \
        timeout -k 5 600s "$T4" $S4 -sm 24 -ss -sb bench.btl >t4-$m.log 2>&1
done
grep -q "T800 cycle estimate enabled" t4-1.log ||
    echo "warning: $T4 has no T4_EST_CYCLES: the columns are host timer ticks"
printf '%-9s %12s %12s  %s\n' "$CPU" cycles instructions hash
tr -d 'SH' < bench2.uart > i.txt
tr -d 'SH' < bench1.uart | while read -r name c h; do
    case $name in done|check) echo "$name $c $h"; continue ;; esac
    i=$(awk -v n="$name" '$1 == n { print $2 }' i.txt)
    printf '%-9s %12s %12s  %s\n' "$name" "$c" "$i" "$h"
done
