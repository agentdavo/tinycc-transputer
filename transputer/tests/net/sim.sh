#!/usr/bin/env bash
# Run moe_net.c on seven T4 nodes joined by links (moe_net.map) with the
# T9000 model and network timing on, then print one timeline: when each
# node got the token, finished its expert and sent its output, the layer's
# latency at the root, and the single-node baseline the root's VERIFY pass
# measures.
#
# usage: bash transputer/tests/net/sim.sh          (from the checkout root)
# env:   TCC_DIR   dir with transputer-tcc and include/  (default: checkout)
#        T4_BIN    emulator                    (default: transputer/t4/t4)
#        NET       1 = senders wait for their link, 2 = link DMA overlaps
#                  (default 2)
#        PROFILE   T4_T9K_PROFILE for the compute model (default tvx3)
#        LINKMBPS  link payload rate, MB/s (default 9, a T9000 DS-link)
#        WORK      scratch dir                 (default: mktemp -d)
#        EXTRA     extra compile flags for every node, e.g. "-DCHUNK=1
#                  -DREDUCE=1" (see moe_net.c)
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
N=7
cd "$WORK" || exit 1

for n in $(seq 0 $((N - 1))); do
    v=0; [ $n -eq 0 ] && v=1
    "$R/transputer-tcc" -static -nostdlib -I"$R/include" -I"$T" -DNODE=$n -DVERIFY=$v \
        ${EXTRA:-} -o n$n.elf "$T/test_boot.S" "$HERE/moe_net.c" &&
    "$R/transputer-netload" n$n.elf n$n.btl >/dev/null 2>&1 || { echo "node $n: build failed"; exit 1; }
done
tr -d '\r' < "$HERE/moe_net.map" > moe_net.map
export SPYNET=moe_net.map
rm -f /tmp/node[0-9]-[0-3].lnk n*.uart
export T4_T9000=1 T4_T9K_MARKS=1 T4_T9K_NET=${NET:-2} T4_T9K_PROFILE=${PROFILE:-tvx3}
export T4_T9K_LINKMBPS=${LINKMBPS:-9}

PIDS=""
for n in $(seq $((N - 1)) -1 0); do
    env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=n$n.uart \
        timeout 600 "$T4" -s806 -sm 24 -sn $n -ss -sb n$n.btl > n$n.out 2>&1 &
    PIDS="$PIDS $!"
done
cleanup() { for p in $PIDS; do kill $p 2>/dev/null; done; }
trap 'cleanup; exit 130' INT TERM

# done when the root has verified and every expert node has sent
for i in $(seq 1 1200); do
    finished=1
    grep -qa 'VERIFY' n0.uart 2>/dev/null || finished=0
    for n in $(seq 1 $((N - 1))); do grep -qa 'sent' n$n.uart 2>/dev/null || finished=0; done
    [ $finished -eq 1 ] && break
    sleep 0.5
done
sleep 0.5
cleanup

mark() { grep -a "T9000-MARK\] .*$2 " n$1.out | head -n "${3:-1}" | tail -n 1 |
         sed 's/.*cycles=\([0-9]*\).*/\1/'; }
echo "network timing: NET=$T4_T9K_NET, links $T4_T9K_LINKMBPS MB/s, profile $T4_T9K_PROFILE, 50 MHz"
s=$(mark 0 Sstart)
printf "%-6s %12s %12s %12s\n" node got_token expert_done sent
for n in $(seq 1 $((N - 1))); do
    printf "%-6s %12d %12d %12d\n" $n $(( $(mark $n Sx) - s )) $(( $(mark $n expert) - s )) $(( $(mark $n sent) - s ))
done
lay=$(( $(mark 0 layer) - s ))
awk -v c=$lay 'BEGIN { printf "root: MoE layer (6 experts) in %d cycles = %.2f ms\n", c, c / 50000 }'
base=0
for k in 1 2 3 4 5 6; do
    base=$(( base + $(mark 0 computed $k) - $(mark 0 loaded $k) ))
done
awk -v c=$base -v l=$lay 'BEGIN { printf "root alone, same 6 experts: %d cycles = %.2f ms (network %.2fx faster)\n", c, c / 50000, c / l }'
grep -a 'VERIFY' n0.uart
