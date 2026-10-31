#!/usr/bin/env bash
# Run vlbw.c on two T4 nodes - Link1 virtual, Link2 byte stream - under the
# T9000 cycle model, and print node 0's cycles per phase next to the
# byte-stream time the link rate alone predicts (expected: VLBW OK).
#
# usage: bash transputer/tests/net/vlbw.sh     (from the checkout root)
# env:   TCC_DIR, T4_BIN, WORK, NET (default 2), LINKMBPS (9), LINKLAT (20)
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
cd "$WORK" || exit 1

printf '%s\n' "Connect HOST to processor 0 link 0" "Connect HOST to processor 1 link 0" \
    "Connect processor 0 link 1 to processor 1 link 1" \
    "Connect processor 0 link 2 to processor 1 link 2" > vlbw.map
for n in 0 1; do
    "$R/transputer-tcc" -static -nostdlib -I"$R/include" -DNODE=$n -o n$n.elf \
        "$T/test_boot.S" "$HERE/vlbw.c" "$R/lib/transputer-conc.c" "$R/transputer-libtcc1.a" 2>/dev/null &&
    "$R/transputer-netload" n$n.elf n$n.btl >/dev/null 2>&1 || { echo "node $n: build failed"; exit 1; }
done
export SPYNET=vlbw.map
export T4_T9000=1 T4_T9K_MARKS=1 T4_T9K_NET=${NET:-2}
export T4_T9K_LINKMBPS=${LINKMBPS:-9} T4_T9K_LINKLAT=${LINKLAT:-20}
rm -f /tmp/node[0-9]-[0-3].lnk n*.uart
PIDS=""
for n in 1 0; do
    env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=n$n.uart \
        timeout 120 "$T4" -s806 -sm 26 -sn $n -ss -sb n$n.btl > n$n.out 2>&1 &
    PIDS="$PIDS $!"
done
for i in $(seq 1 240); do
    grep -qa 'VLBW' n0.uart 2>/dev/null && break
    sleep 0.5
done
for p in $PIDS; do kill $p 2>/dev/null; done

mark() { grep -a "T9000-MARK\] .*$1 " n0.out | head -n 1 | sed 's/.*cycles=\([0-9]*\).*/\1/'; }
s=$(mark Sstart); a=$(mark vl1); b=$(mark vl3); c=$(mark bs)
if [ -n "$s" ] && [ -n "$c" ]; then
    awk -v s=$s -v a=$a -v b=$b -v c=$c -v mb=$T4_T9K_LINKMBPS 'BEGIN {
        ideal = 24576 / (mb / 50)               # bytes / (bytes per cycle)
        printf "24 KB node 0 -> node 1 at %d MB/s, cycles (link rate alone: %d)\n", mb, ideal
        printf "  one virtual link        %9d  %.2fx\n", a - s, (a - s) / ideal
        printf "  three virtual links     %9d  %.2fx\n", b - a, (b - a) / ideal
        printf "  byte-stream link        %9d  %.2fx\n", c - b, (c - b) / ideal
    }'
fi
grep -a 'VLBW' n0.uart || echo "VLBW no verdict"
grep -a 'node1' n1.uart || echo 'node1 no verdict'
