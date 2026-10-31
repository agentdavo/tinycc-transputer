#!/usr/bin/env bash
# Run xlink.c on two T4 nodes, Link1 the link under test and Link2 a
# second cable to step through the cases, and print node 0's verdict
# (expected: XLINK OK).
#
# Both nodes run in one threaded t4 network (-st), where link faults
# reach the far end.  The t4 needs the two timer-list fixes (t4 351dd89),
# case 9 link outputs that complete only on the acknowledge (7cb262c), and
# cases 12 and 13 a resetch that empties a link input and a halted link
# that sends nothing (t4 067bbc9); see transputer/CLAUDE.md,
# "Communicate or fail".
#
# usage: bash transputer/tests/net/xlink.sh     (from the checkout root)
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
    "Connect processor 0 link 2 to processor 1 link 2" > xlink.map
for n in 0 1; do
    "$R/transputer-tcc" -mt9000 -static -nostdlib -I"$R/include" -DNODE=$n -o n$n.elf \
        "$T/test_boot.S" "$HERE/xlink.c" "$R/lib/transputer-conc.c" "$R/transputer-libtcc1.a" &&
    "$R/transputer-netload" n$n.elf n$n.btl >/dev/null 2>&1 || { echo "node $n: build failed"; exit 1; }
done
rm -f xlink.uart
SPYNET=xlink.map T4_NODE_BTL=n%d.btl env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=xlink.uart \
    timeout -k 5 60 "$T4" -s806 -sm 26 -ss -st -sn -2 -sb n0.btl > xlink.out 2>&1 &
PID=$!
for i in $(seq 1 120); do
    grep -qa 'XLINK' xlink.uart 2>/dev/null && break
    kill -0 $PID 2>/dev/null || break
    # bounded: a runaway t4 log must not fill the disk
    [ "$(stat -c %s xlink.out 2>/dev/null || echo 0)" -lt 50000000 ] || break
    sleep 0.5
done
kill $PID 2>/dev/null
wait $PID 2>/dev/null
grep -a 'XLINK\|:F' xlink.uart || { echo "XLINK no verdict"; tail -c 400 xlink.uart; }
