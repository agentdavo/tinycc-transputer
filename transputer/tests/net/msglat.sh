#!/usr/bin/env bash
# Run msglat.c on two T9000s joined through one IMS C104 (as one threaded
# T4 network) and print the cycles per message step of each way of starting
# a transfer, next to the bare wire time of one packet one way.
#
# usage: bash transputer/tests/net/msglat.sh     (from the checkout root)
# env:   TCC_DIR, T4_BIN, WORK, LEN (4), NET (2), LINKMBPS (9), LINKLAT (20),
#        C104LAT (20)
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
LEN=${LEN:-4}
mkdir -p "$WORK" && cd "$WORK" || exit 1

{
    echo "Connect HOST to processor 0 link 0"
    echo "router 2 links 2"
    for n in 0 1; do
        echo "Connect processor $n link 1 to router 2 link $n"
        echo "router 2 interval $n $((n + 1)) $n"
    done
    echo "router 2 delete 0-1"
} > msglat.map
for n in 0 1; do
    "$R/transputer-tcc" -mt9000tvx -static -nostdlib -I"$R/include" -DNODE=$n -DLEN=$LEN \
        -o n$n.elf "$T/test_boot.S" "$HERE/msglat.c" "$R/lib/transputer/transputer-conc.c" "$R/transputer-libtcc1.a" 2>/dev/null &&
    "$R/transputer-netload" n$n.elf n$n.btl >/dev/null 2>&1 || { echo "node $n: build failed"; exit 1; }
done
export SPYNET=msglat.map T4_NODE_BTL=n%d.btl
export T4_T9000=1 T4_T9K_MARKS=1 T4_T9K_NET=${NET:-2}
export T4_T9K_LINKMBPS=${LINKMBPS:-9} T4_T9K_LINKLAT=${LINKLAT:-20} T4_T9K_C104LAT=${C104LAT:-20}
rm -f msglat.uart
env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=msglat.uart \
    timeout 120 "$T4" -s806 -sm 26 -ss -st -sn -2 -sb n0.btl > msglat.out 2>&1 &
PID=$!
for i in $(seq 1 240); do
    grep -qa 'T9000-MARK\] n0 MSGLAT' msglat.out 2>/dev/null && break
    kill -0 $PID 2>/dev/null || break
    sleep 0.5
done
kill $PID 2>/dev/null

mark() { grep -a "T9000-MARK\] n0 S*$1 " msglat.out | head -n 1 | sed 's/.*cycles=\([0-9]*\).*/\1/'; }
s=$(mark start); pp=$(mark pp); par=$(mark par); vx=$(mark vxfer); vx3=$(mark vxfer3)
if [ -n "$s" ] && [ -n "$vx3" ]; then
    awk -v s=$s -v pp=$pp -v par=$par -v vx=$vx -v vx3=$vx3 -v len=$LEN \
        -v mb=$T4_T9K_LINKMBPS -v lat=$T4_T9K_LINKLAT -v rl=$T4_T9K_C104LAT 'BEGIN {
        r = mb / 50; steps = 64
        # one packet one way: 2-byte header, data, end token, through the switch
        wire = (len + 3) / r + 2 * lat + rl
        printf "%d-byte messages through one C104, links %d MB/s; node 0, cycles per step:\n", len, mb
        printf "  %-40s %8s %8s\n", "", "step", "- wire"
        printf "  %-40s %8.0f %8.0f\n", "ping-pong, half a round trip", (pp - s) / steps / 2, (pp - s) / steps / 2 - wire
        printf "  %-40s %8.0f %8.0f\n", "exchange, a process for the output", (par - pp) / steps, (par - pp) / steps - wire
        printf "  %-40s %8.0f %8.0f\n", "exchange, one __vxfer", (vx - par) / steps, (vx - par) / steps - wire
        printf "  %-40s %8.0f %8.0f\n", "3 links each way, one __vxfer", (vx3 - vx) / steps, (vx3 - vx) / steps - 3 * (len + 3) / r - 2 * lat - rl
        printf "  (wire: one packet one way, %.0f cycles)\n", wire
    }'
else
    echo "no timeline (see $WORK/msglat.out)"
fi
grep -a 'T9000-MARK\] n[01] MSGLAT' msglat.out | sed 's/^.*\] \(n[01] MSGLAT [A-Z]*\).*/\1/' |
    grep . || echo "MSGLAT no verdict"
