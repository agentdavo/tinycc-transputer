#!/usr/bin/env bash
# Run ffn2d.c (the shared-expert FFN on a 3 x 3 grid) on 9 T9000s and three
# IMS C104s as one threaded T4 network, and print node 0's phases next to
# plan.py's model of each collective (direct_bw plus one step).
#
# usage: bash transputer/tests/net/ffn2d.sh     (from the checkout root)
# env:   TCC_DIR, T4_BIN, WORK, NET (2), PROFILE (tvx3), LINKMBPS (9),
#        LINKLAT (20), C104LAT (20), VERIFY (1: every node sends node 0 its
#        part of y and node 0 checks it; 0: skip it),
#        TIMEOUT (900 seconds)
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
N=9
mkdir -p "$WORK" && cd "$WORK" || exit 1

{
    echo "Connect HOST to processor 0 link 0"
    for p in 1 2 3; do
        r=$((N + p - 1))
        echo "router $r links $N"
        for n in $(seq 0 $((N - 1))); do
            echo "Connect processor $n link $p to router $r link $n"
            echo "router $r interval $n $((n + 1)) $n"
        done
        echo "router $r delete 0-$((N - 1))"
    done
} > ffn2d.map
for n in $(seq 0 $((N - 1))); do
    "$R/transputer-tcc" -mt9000tvx -static -nostdlib -I"$R/include" -I"$T" -DNODE=$n -DVERIFY=${VERIFY:-1} \
        -o n$n.elf "$T/test_boot.S" "$HERE/ffn2d.c" "$R/transputer-libtcc1.a" 2>/dev/null &&
    "$R/transputer-netload" n$n.elf n$n.btl >/dev/null 2>&1 || { echo "node $n: build failed"; exit 1; }
done
export SPYNET=ffn2d.map T4_NODE_BTL=n%d.btl
export T4_T9000=1 T4_T9K_MARKS=1 T4_T9K_NET=${NET:-2} T4_T9K_PROFILE=${PROFILE:-tvx3}
export T4_T9K_LINKMBPS=${LINKMBPS:-9} T4_T9K_LINKLAT=${LINKLAT:-20} T4_T9K_C104LAT=${C104LAT:-20}
env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=ffn2d.uart \
    timeout ${TIMEOUT:-900} "$T4" -s806 -sm 26 -ss -st -sn -$N -sb n0.btl > ffn2d.out 2>&1 &
PID=$!
last=VERIFY; [ "${VERIFY:-1}" = 0 ] && last=done
for i in $(seq 1 $((2 * ${TIMEOUT:-900}))); do
    grep -qa "T9000-MARK\] n0 $last " ffn2d.out 2>/dev/null && break
    kill -0 $PID 2>/dev/null || break
    sleep 0.5
done
kill $PID 2>/dev/null

mark() { grep -a "T9000-MARK\] n0 S*$1 " ffn2d.out | head -n 1 | sed 's/.*cycles=\([0-9]*\).*/\1/'; }
s=$(mark start); gu=$(mark gateup); r1=$(mark rs1); sw=$(mark swiglu); ag=$(mark ag)
dn=$(mark down); r2=$(mark rs2); dd=$(mark done)
if [ -n "$s" ] && [ -n "$dd" ]; then
    awk -v s=$s -v gu=$gu -v r1=$r1 -v sw=$sw -v ag=$ag -v dn=$dn -v r2=$r2 -v dd=$dd \
        -v mb=$T4_T9K_LINKMBPS -v lat=$T4_T9K_LINKLAT -v rl=$T4_T9K_C104LAT '
    # every virtual link in flight at HOL 1.04 x the link time plus P_PKT
    # 11.3 a packet, or each one a round trip a packet if that is longer:
    # two packet times and RT_PATH 1.4 paths
    function dbw(n, peers, rr, path,   pk, w, l) {
        pk = n / 32 / 3
        w = n / (3 * rr * 32 / 38) * 1.04 + pk * 11.3
        l = pk / peers * (2 * 38 / rr + 1.4 * path)
        return w > l ? w : l
    }
    BEGIN {
        rr = mb / 50
        path = 2 * lat + rl                           # 1 hop
        step = 40 + path                              # plan.py step_lat
        # plan.py direct_bw, bytes each node sends to 2 peers
        m1 = step + dbw(4096, 2, rr, path)
        ma = step + dbw(584, 2, rr, path)
        m2 = step + dbw(4552, 2, rr, path)
        printf "2D FFN, 3 x 3 grid, 3 C104 planes, links %d MB/s; node 0, cycles:\n", mb
        printf "  %-40s %9s %9s\n", "phase", "measured", "model"
        printf "  %-40s %9d %9.0f\n", "gate+up compute (1.89 MB of weights)", gu - s, 1892352 / 16
        printf "  %-40s %9d %9.0f\n", "reduce-scatter gate/up (4096 B out)", r1 - gu, m1
        printf "  %-40s %9d %9s\n", "sum, SwiGLU, quantise h", sw - r1, "-"
        printf "  %-40s %9d %9.0f\n", "all-gather h in the column (584 B)", ag - sw, ma
        printf "  %-40s %9d %9.0f\n", "down compute (0.90 MB)", dn - ag, 1706 * 3 * 176 / 16
        printf "  %-40s %9d %9.0f\n", "reduce-scatter y (4552 B out)", r2 - dn, m2
        printf "  %-40s %9d %9s\n", "sum y", dd - r2, "-"
        printf "  %-40s %9d %9.0f\n", "collectives", (r1 - gu) + (ag - sw) + (r2 - dn), m1 + ma + m2
        printf "  %-40s %9d  = %.2f ms\n", "FFN total", dd - s, (dd - s) / 50000
    }'
else
    echo "no timeline (see $WORK/ffn2d.out)"
fi
grep -a 'T9000-MARK\] n0 VERIFY' ffn2d.out | sed 's/^.*\] n0 \(VERIFY [A-Z]*\).*/\1/' || echo "VERIFY no verdict"
