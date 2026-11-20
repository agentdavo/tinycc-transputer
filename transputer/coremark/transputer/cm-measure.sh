#!/bin/bash
# Measure CoreMark's dynamic instruction count per iteration and its
# instruction mix, for one toolchain and one CPU.
#
#   TCC_DIR=~/tpw/<tree> CPU=t9000|t800 OUT=dir bash cm-measure.sh
#
# Two images are built, with ITER_A and ITER_B iterations (default 10 and
# 30).  Each runs on t4 with T4_EST_CYCLES=2, so CoreMark's ldtimer reads the
# executed-instruction count and "Total ticks" is the instructions in the
# timed region (CoreMark's own timer is the host's otherwise, and unreliable).
# Each also runs on a counting t4 (T4_OPHIST, per-address execution counts:
# t4 HEAD with cm-ophist.patch.py applied, built by Makefile.linux; OPH_T4,
# default ~/tpw/cmt4/t4) and cm-mix.py reports the difference of the
# two histograms divided by ITER_B - ITER_A: exactly one iteration's mix,
# start-up and reporting cancelled out.
#
# NO_OPH=1 skips the counting runs (instructions per iteration only).
#
# Every run must print "Correct operation validated" and the 2K performance
# CRCs (seedcrc 0xe9f5, crclist 0xe714, crcmatrix 0x1fd7, crcstate 0x8e3a).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TCC_DIR="${TCC_DIR:?TCC_DIR=toolchain tree}"
CPU="${CPU:-t9000}"
OUT="${OUT:?OUT=output dir}"
ITER_A="${ITER_A:-10}"
ITER_B="${ITER_B:-30}"
T4="${T4:-$HERE/../../t4/t4}"
OPH_T4="${OPH_T4:-$HOME/tpw/cmt4/t4}"
mkdir -p "$OUT"
case "$CPU" in t9000) S=-s9 ;; *) S=-s8 ;; esac

for n in "$ITER_A" "$ITER_B"; do
    B="$OUT/coremark-$CPU-i$n.btl"
    WORKDIR="$OUT/obj-$CPU-i$n" BTL="$B" ITERATIONS=$n RUN_COREMARK=0 \
        TCC="$TCC_DIR/transputer-tcc" NETLOAD="$TCC_DIR/transputer-netload" \
        bash "$HERE/build.sh" > "$OUT/build-$CPU-i$n.log" 2>&1 \
        || { tail -20 "$OUT/build-$CPU-i$n.log"; exit 1; }
    U="$OUT/coremark-$CPU-i$n.uart"
    : > "$U"
    DISPLAY= T4_HEADLESS=1 SDL_VIDEODRIVER=dummy T4_UART_CONSOLE=0 \
        T4_UART_LOG="$U" T4_EST_CYCLES=2 \
        timeout -k 5 300 "$T4" $S -si -se -sb "$B" 2>&1 | tail -c 20000 \
        > "$OUT/run-$CPU-i$n.log" || true
    tr -d '\000' < "$U" > "$U.txt"
    for want in 'seedcrc          : 0xe9f5' '\[0\]crclist       : 0xe714' \
                '\[0\]crcmatrix     : 0x1fd7' '\[0\]crcstate      : 0x8e3a' \
                'Correct operation validated'; do
        grep -q "$want" "$U.txt" || { echo "FAIL $CPU i$n: missing '$want'"; cat "$U.txt"; exit 1; }
    done
    if grep -Eq 'Errors detected|Cannot validate' "$U.txt"; then
        echo "FAIL $CPU i$n: errors"; cat "$U.txt"; exit 1
    fi
    [ -n "${NO_OPH:-}" ] && continue
    H="$OUT/oph-$CPU-i$n.txt"
    U2="$OUT/coremark-$CPU-i$n.oph.uart"
    : > "$U2"
    DISPLAY= T4_HEADLESS=1 SDL_VIDEODRIVER=dummy T4_UART_CONSOLE=0 \
        T4_UART_LOG="$U2" T4_EST_CYCLES=2 T4_OPHIST="$H" \
        timeout -k 5 600 "$OPH_T4" $S -si -se -sb "$B" 2>&1 | tail -c 20000 \
        > "$OUT/ophrun-$CPU-i$n.log" || true
    tr -d '\000' < "$U2" | grep -q 'Correct operation validated' \
        || { echo "FAIL $CPU i$n: counting t4 run did not validate"; exit 1; }
done
TA=$(grep -a 'Total ticks' "$OUT/coremark-$CPU-i$ITER_A.uart.txt" | grep -o '[0-9]*$')
TB=$(grep -a 'Total ticks' "$OUT/coremark-$CPU-i$ITER_B.uart.txt" | grep -o '[0-9]*$')
echo "$CPU ticks(i$ITER_A)=$TA ticks(i$ITER_B)=$TB per-iteration(diff)=$(( (TB - TA) / (ITER_B - ITER_A) ))" \
    | tee "$OUT/summary-$CPU.txt"
[ -n "${NO_OPH:-}" ] && exit 0
python3 -I "$HERE/cm-mix.py" "$OUT/coremark-$CPU-i$ITER_B.elf" \
    "$OUT/oph-$CPU-i$ITER_A.txt" "$OUT/oph-$CPU-i$ITER_B.txt" \
    $((ITER_B - ITER_A)) > "$OUT/mix-$CPU.txt"
head -3 "$OUT/mix-$CPU.txt" | tee -a "$OUT/summary-$CPU.txt"
