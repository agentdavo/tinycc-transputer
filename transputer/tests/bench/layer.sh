#!/usr/bin/env bash
# DeepSeek-V4.1-Flash, one layer, one token: T9000-model cycles of every
# weight matrix, on the f32-activation path and on the best TVX path
# (integer activations via VQ8K/VQ80, bf16 for float weights, 16 KB
# activation buffer, 64 integer MAC lanes), both on the 8-FP-lane machine
# with 16 B/cycle read and 8 B/cycle write.
#
# Each tensor is one QGEMM at its real input width, timed on SLICE rows and
# scaled to its real row count and multiplicity.  Routed experts are IQ1_S,
# as in the IQ1_M file.
#
# usage: bash transputer/tests/bench/layer.sh       (from the checkout root)
# env:   TCC_DIR (transputer-tcc + include), T4_BIN, SLICE, WORK,
#        NEWX (more T4_T9K_* settings for the best path, e.g.
#        T4_T9K_TERNLANES=256 for profile tvx4's ternary lanes)
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
SLICE=${SLICE:-128}
WORK=${WORK:-$(mktemp -d)}
cd "$WORK" || exit 1
OLD="T4_T9K_MACLANES=8 T4_T9K_RDBW=16 T4_T9K_WRBW=8"
NEW="$OLD T4_T9K_ACTBUF=16384 T4_T9K_INTLANES=64 ${NEWX:-}"

gemv() {    # fmt ne bsz kdim rows act env -> "quant-cycles gemv-cycles rows-run"
    local r=$5; [ "$r" -gt "$SLICE" ] && r=$SLICE
    local tag=g$1_$4_$6_$r
    "$R/transputer-tcc" -static -nostdlib -I"$R/include" -I"$T" \
        -DFMT=$1 -DNE=$2 -DBSZ=$3 -DKDIM=$4 -DROWS=$r -DACT=$6 \
        -o $tag.elf "$T/test_boot.S" "$HERE/bench_gemv.c" >/dev/null 2>&1 &&
    "$R/transputer-netload" $tag.elf $tag.btl >/dev/null 2>&1 || { echo "0 0 $r"; return; }
    env -u DISPLAY $7 T4_UART_CONSOLE=0 T4_UART_LOG=/dev/null T4_T9000=1 T4_T9K_MARKS=1 \
        timeout -k 5 600s "$T4" -s806 -sm 27 -ss -sb $tag.btl 2>&1 >/dev/null |
    awk -v r=$r '/\[T9000-MARK\]/ { c=$0; sub(/.*cycles=/, "", c); sub(/ .*/, "", c); v[++n]=c }
                 END { print v[2]-v[1], v[3]-v[2], r }'
}

# hc_fn (24 rows over 20480 bf16 activations) goes in K chunks of 8192 +
# 8192 + 4096, weights laid out per chunk when loaded, so each chunk's
# activations fit the 16 KB buffer; adding the 24-float partials is free.
# name fmt ne bsz kdim rows count f32-path-act best-path-act
# (act: 0 f32, 1 bf16, 8 Q8_0, 15 Q8_K)
TENSORS="
routed_gate_up_x6 19 256 50 5120 2304 12 0 15
routed_down_x6    19 256 50 2304 5120 6 0 15
shared_gate_up    13 256 176 5120 2304 2 0 15
shared_down       13 256 176 2304 5120 1 0 15
attn_q_a          8 32 34 5120 1280 1 0 8
attn_kv           8 32 34 5120 512 1 0 8
attn_q_b          13 256 176 1280 32768 1 0 15
attn_output_a_x8  13 256 176 4096 1024 8 0 15
attn_output_b     13 256 176 8192 5120 1 0 15
router            30 32 64 5120 384 1 0 1
hc_fn_x2_k8192    0 32 128 8192 24 4 0 1
hc_fn_x2_k4096    0 32 128 4096 24 2 0 1
"
printf "%-18s %12s %12s %8s\n" tensor f32_path best_path speedup
echo "$TENSORS" | while read -r name f ne bsz k rows cnt oa na; do
    [ -z "$name" ] && continue
    read -r q0 g0 r0 <<< "$(gemv $f $ne $bsz $k $rows $oa "$OLD")"
    read -r q1 g1 r1 <<< "$(gemv $f $ne $bsz $k $rows $na "$NEW")"
    awk -v n=$name -v q0=$q0 -v g0=$g0 -v q1=$q1 -v g1=$g1 -v r=$r0 -v rows=$rows -v c=$cnt '
        BEGIN { o = (q0 + g0 * rows / r) * c; w = (q1 + g1 * rows / r) * c
                printf "%-18s %12.0f %12.0f %7.1fx\n", n, o, w, o / w }'
done | tee layer.out
awk '{ o += $2; w += $3 } END { printf "%-18s %12.0f %12.0f %7.1fx\n", "LAYER", o, w, o / w }' layer.out
