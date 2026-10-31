#!/usr/bin/env bash
# One DeepSeek-V4.1-Flash routed expert for one token (bench_moe.c), cycles
# per phase on the f32 path and the TVX integer path.
#
# usage: bash transputer/tests/bench/moe.sh [fmt:block-bytes ...]
#        default: 19:50 (IQ1_S) 17:74 (IQ2_XS) 13:176 (Q5_K)
# env:   TCC_DIR (transputer-tcc + include), T4_BIN, WORK
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
FMTS=${*:-"19:50 17:74 13:176"}
cd "$WORK" || exit 1
M1="T4_T9K_MACLANES=8 T4_T9K_RDBW=16 T4_T9K_WRBW=8"
M2="$M1 T4_T9K_ACTBUF=16384 T4_T9K_INTLANES=64"

run() {     # label fmt bsz q8k tvx-axpy machine-env
    local tag=m$2_$4_$5
    "$R/transputer-tcc" -static -nostdlib -I"$R/include" -I"$T" \
        -DFMT=$2 -DBSZ=$3 -DQ8K=$4 -DTVX_AXPY=$5 \
        -o $tag.elf "$T/test_boot.S" "$HERE/bench_moe.c" >/dev/null 2>&1 &&
    "$R/transputer-netload" $tag.elf $tag.btl >/dev/null 2>&1 || { echo "$1: build failed"; return; }
    env -u DISPLAY $6 T4_UART_CONSOLE=0 T4_UART_LOG=/dev/null T4_T9000=1 T4_T9K_MARKS=1 \
        timeout -k 5 600s "$T4" -s806 -sm 27 -ss -sb $tag.btl 2>&1 >/dev/null |
    awk -v label="$1" '
        /\[T9000-MARK\]/ { tag=$2; c=$0; sub(/.*cycles=/, "", c); sub(/ .*/, "", c)
                           if (tag ~ /start$/) { s = c; p = c; next }
                           d[++n] = tag "=" (c - p); p = c }
        END { line = sprintf("%-28s total=%9d |", label, p - s)
              for (i = 1; i <= n; i++) line = line " " d[i]
              print line }'
}

for fb in $FMTS; do
    f=${fb%%:*}; b=${fb##*:}
    echo "== ggml type $f (block $b bytes)"
    run "f32 acts, 8 FP lanes"        $f $b 0 0 "$M1"
    run "Q8_K, 8 lanes"               $f $b 1 0 "$M1"
    run "Q8_K, 16K buf, 64 int lanes" $f $b 1 0 "$M2"
    run "  same, TVX accumulate"      $f $b 1 1 "$M2"
done
