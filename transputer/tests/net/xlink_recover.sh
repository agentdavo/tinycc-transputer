#!/usr/bin/env bash
# Run xlink_recover.c - technical note 1's example 2, a link that is
# unplugged for a while and resynchronised - on two T4 nodes, Link1 the
# protected link and Link2 carrying node 1's figures, and print node 0's
# report (expected: XREC OK, after a line of figures for each node).
#
# Both nodes run in one threaded t4 network (-st), where link faults reach
# the far end.  The t4 needs the two timer-list fixes (t4 351dd89); see
# transputer/CLAUDE.md, "Communicate or fail".
#
# usage: bash transputer/tests/net/xlink_recover.sh     (from the checkout root)
# env:   TCC_DIR, T4_BIN, WORK
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK" && cd "$WORK" || exit 1

printf '%s\n' "Connect HOST to processor 0 link 0" \
    "Connect processor 0 link 1 to processor 1 link 1" \
    "Connect processor 0 link 2 to processor 1 link 2" > xrec.map
for n in 0 1; do
    "$R/transputer-tcc" -mt9000 -static -nostdlib -I"$R/include" -DNODE=$n -o n$n.elf \
        "$T/test_boot.S" "$HERE/xlink_recover.c" "$R/lib/transputer/transputer-conc.c" "$R/transputer-libtcc1.a" &&
    "$R/transputer-netload" n$n.elf n$n.btl >/dev/null 2>&1 || { echo "node $n: build failed"; exit 1; }
done
rm -f xrec.uart
SPYNET=xrec.map T4_NODE_BTL=n%d.btl env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=xrec.uart \
    timeout -k 5 90 "$T4" -s806 -sm 26 -ss -st -sn -2 -sb n0.btl > xrec.out 2>&1 &
PID=$!
for i in $(seq 1 180); do
    grep -qa 'XREC' xrec.uart 2>/dev/null && break
    kill -0 $PID 2>/dev/null || break
    # bounded: a runaway t4 log must not fill the disk
    [ "$(stat -c %s xrec.out 2>/dev/null || echo 0)" -lt 50000000 ] || break
    sleep 0.5
done
kill $PID 2>/dev/null
wait $PID 2>/dev/null
grep -a 'XREC\|:F\|^n[01]:' xrec.uart || { echo "XREC no verdict"; tail -c 400 xrec.uart; }
