#!/usr/bin/env bash
# Run bytestream.c on two T4 nodes joined by link 1 and print node 0's verdict
# (expected: BYTESTREAM OK).
#
# usage: bash transputer/tests/net/bytestream.sh     (from the checkout root)
# env:   TCC_DIR, T4_BIN, WORK
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
cd "$WORK" || exit 1

printf '%s\n' "Connect HOST to processor 0 link 0" "Connect HOST to processor 1 link 0" \
    "Connect processor 0 link 1 to processor 1 link 1" > bytestream.map
for n in 0 1; do
    "$R/transputer-tcc" -static -nostdlib -I"$R/include" -DNODE=$n -o n$n.elf \
        "$T/test_boot.S" "$HERE/bytestream.c" "$R/lib/transputer-conc.c" "$R/transputer-libtcc1.a" &&
    "$R/transputer-netload" n$n.elf n$n.btl >/dev/null 2>&1 || { echo "node $n: build failed"; exit 1; }
done
export SPYNET=bytestream.map
rm -f /tmp/node[0-9]-[0-3].lnk n*.uart
PIDS=""
for n in 1 0; do
    env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=n$n.uart \
        timeout 60 "$T4" -s806 -sm 26 -sn $n -ss -sb n$n.btl > n$n.out 2>&1 &
    PIDS="$PIDS $!"
done
for i in $(seq 1 120); do
    grep -qa 'BYTESTREAM' n0.uart 2>/dev/null && break
    sleep 0.5
done
for p in $PIDS; do kill $p 2>/dev/null; done
grep -a 'BYTESTREAM' n0.uart || echo "BYTESTREAM no verdict"
grep -a 'node1' n1.uart
