#!/usr/bin/env bash
# Run ffn_cube.c on an 8-node hypercube of T4 nodes (1D or 2D sharding),
# print node 0's phase timeline next to the collective times plan.py's
# model predicts, and the root's VERIFY result.
#
# usage: bash transputer/tests/net/cube.sh [1|2]     (from the checkout root)
# env:   TCC_DIR, T4_BIN, NET (default 2), PROFILE (tvx3), LINKMBPS (9), WORK
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
SCHEME=${1:-1}
N=8
cd "$WORK" || exit 1

{
    for n in $(seq 0 $((N - 1))); do echo "Connect HOST to processor $n link 0"; done
    for n in $(seq 0 $((N - 1))); do
        for d in 0 1 2; do
            m=$((n ^ (1 << d)))
            [ $n -lt $m ] && echo "Connect processor $n link $((d + 1)) to processor $m link $((d + 1))"
        done
    done
} > cube.map
for n in $(seq 0 $((N - 1))); do
    v=0; [ $n -eq 0 ] && v=1
    "$R/transputer-tcc" -mt9000tvx -static -nostdlib -I"$R/include" -I"$T" -DNODE=$n -DVERIFY=$v \
        -DSCHEME=$SCHEME -o n$n.elf "$T/test_boot.S" "$HERE/ffn_cube.c" "$R/lib/transputer/transputer-conc.c" &&
    "$R/transputer-netload" n$n.elf n$n.btl >/dev/null 2>&1 || { echo "node $n: build failed"; exit 1; }
done
export SPYNET=cube.map
rm -f /tmp/node[0-9]-[0-3].lnk n*.uart
export T4_T9000=1 T4_T9K_MARKS=1 T4_T9K_NET=${NET:-2} T4_T9K_PROFILE=${PROFILE:-tvx3}
export T4_T9K_LINKMBPS=${LINKMBPS:-9}
PIDS=""
for n in $(seq $((N - 1)) -1 0); do
    env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=n$n.uart \
        timeout 600 "$T4" -s806 -sm 26 -sn $n -ss -sb n$n.btl > n$n.out 2>&1 &
    PIDS="$PIDS $!"
done
cleanup() { for p in $PIDS; do kill $p 2>/dev/null; done; }
trap 'cleanup; exit 130' INT TERM
for i in $(seq 1 1200); do
    finished=1
    grep -qa 'VERIFY' n0.uart 2>/dev/null || finished=0
    for n in $(seq 1 $((N - 1))); do grep -qa 'done' n$n.uart 2>/dev/null || finished=0; done
    [ $finished -eq 1 ] && break
    sleep 0.5
done
sleep 0.5
cleanup

mark() { grep -a "T9000-MARK\] .*$1 " n0.out | head -n 1 | sed 's/.*cycles=\([0-9]*\).*/\1/'; }
s=$(mark Sstart); gu=$(mark gateup); hh=$(mark h); dn=$(mark down); dd=$(mark done)
rs=$(mark rs); [ -z "$rs" ] && rs=$gu
awk -v s=$s -v gu=$gu -v rs=$rs -v hh=$hh -v dn=$dn -v dd=$dd -v sch=$SCHEME \
    -v mb=$T4_T9K_LINKMBPS 'BEGIN {
    r = mb / 50; lat = 20                       # bytes/cycle, cycles per hop
    p_rs = (sch == 2) ? 2 * 288 * 4 / r + lat : 0
    p_h  = (1 + 2 + 4) * 288 * 4 / r + 3 * lat
    p_y  = (sch == 1) ? (1 + 2 + 4) * 640 * 4 / r + 3 * lat : (1 + 2) * 640 * 4 / r + 2 * lat
    printf "scheme %s on 8 nodes, links %d MB/s; node 0, cycles:\n", sch == 1 ? "1D" : "2D", mb
    printf "  %-30s %9s %9s\n", "phase", "measured", "model"
    printf "  %-30s %9d\n", "gate+up compute", gu - s
    if (sch == 2) printf "  %-30s %9d %9d\n", "reduce-scatter partials", rs - gu, p_rs
    printf "  %-30s %9d %9d\n", "SwiGLU + all-gather h", hh - rs, p_h
    printf "  %-30s %9d\n", "quantise h + down compute", dn - hh
    printf "  %-30s %9d %9d\n", sch == 1 ? "all-gather y (8 nodes)" : "all-gather y half (4 nodes)", dd - dn, p_y
    printf "  %-30s %9d  = %.2f ms\n", "FFN total", dd - s, (dd - s) / 50000
}'
grep -a 'VERIFY' n0.uart
