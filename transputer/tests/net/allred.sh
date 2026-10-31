#!/usr/bin/env bash
# Run allred.c on 8 T9000s and three IMS C104s (one threaded T4 network,
# wired as c104.sh) and print node 0's cycles per 96-byte all-reduce, next
# to plan.py's model for the same network (one router hop).
#
# usage: bash transputer/tests/net/allred.sh     (from the checkout root)
# env:   TCC_DIR, T4_BIN, WORK, NET (2), LINKMBPS (9), LINKLAT (20),
#        C104LAT (20)
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
N=8
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
} > allred.map
for n in $(seq 0 $((N - 1))); do
    "$R/transputer-tcc" -mt9000tvx -static -nostdlib -I"$R/include" -DNODE=$n -o n$n.elf \
        "$T/test_boot.S" "$HERE/allred.c" "$R/transputer-libtcc1.a" 2>/dev/null &&
    "$R/transputer-netload" n$n.elf n$n.btl >/dev/null 2>&1 || { echo "node $n: build failed"; exit 1; }
done
export SPYNET=allred.map T4_NODE_BTL=n%d.btl
export T4_T9000=1 T4_T9K_MARKS=1 T4_T9K_NET=${NET:-2} T4_T9K_PROFILE=tvx3
export T4_T9K_LINKMBPS=${LINKMBPS:-9} T4_T9K_LINKLAT=${LINKLAT:-20} T4_T9K_C104LAT=${C104LAT:-20}
env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=allred.uart \
    timeout 300 "$T4" -s806 -sm 26 -ss -st -sn -$N -sb n0.btl > allred.out 2>&1 &
PID=$!
for i in $(seq 1 600); do
    grep -qa 'T9000-MARK\] n0 ALLRED' allred.out 2>/dev/null && break
    kill -0 $PID 2>/dev/null || break
    sleep 0.5
done
kill $PID 2>/dev/null

mark() { grep -a "T9000-MARK\] n0 S*$1 " allred.out | head -n 1 | sed 's/.*cycles=\([0-9]*\).*/\1/'; }
s=$(mark start); rd=$(mark rd); r8=$(mark r8)
if [ -n "$s" ] && [ -n "$r8" ]; then
    awk -v s=$s -v rd=$rd -v r8=$r8 -v mb=$T4_T9K_LINKMBPS -v lat=$T4_T9K_LINKLAT \
        -v rl=$T4_T9K_C104LAT 'BEGIN {
        reps = 32; rr = mb / 50; link = 3 * rr * 32 / 38   # three planes
        step = 40 + 2 * lat + rl                         # plan.py step_lat, 1 hop
        printf "96-byte all-reduce on 8 nodes, 3 C104 planes, links %d MB/s; cycles:\n", mb
        printf "  %-44s %8s %8s\n", "", "measured", "model"
        printf "  %-44s %8.0f %8.0f\n", "recursive doubling, whole vector, 3 steps",
            (rd - s) / reps, 3 * (step + 96 / link)
        printf "  %-44s %8.0f %8.0f\n", "radix 8, all seven peers in one step",
            (r8 - rd) / reps, step + 7 * (96 / link * 1.2 + 35)
    }'
else
    echo "no timeline (see $WORK/allred.out)"
fi
grep -a 'T9000-MARK\] n[0-7] ALLRED' allred.out | sed 's/^.*\] \(n[0-7] ALLRED [A-Z]*\).*/\1/' |
    awk '{ n++; ok += $3 == "OK" } END { print n + 0, "nodes,", ok + 0, "ALLRED OK" }'
