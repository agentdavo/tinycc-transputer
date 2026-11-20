#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
for cpu in t414 t425 t800 t9000; do
    case $cpu in
        t800) lib=transputer-libtcc1-t800.a; sim=-s8;;
        t9000) lib=transputer-libtcc1.a; sim=-s9;;
        *) lib=transputer-libtcc1-$cpu.a; sim=-s${cpu#t};;
    esac
    d=$WORK/$cpu; mkdir -p "$d"
    T4_STRICT_UNRESOLVED_CALLS=1 "$R/transputer-tcc" -m$cpu -nostdlib \
        -I"$ROOT/include" -o "$d/test.elf" "$ROOT/transputer/tests/test_boot.S" \
        "$HERE/test_inmos_rand.c" "$HERE/inmos-rand.c" "$R/$lib" >"$d/link.log" 2>&1
    [ ! -s "$d/link.log" ] || { cat "$d/link.log" >&2; exit 1; }
    "$R/transputer-netload" "$d/test.elf" "$d/test.btl" >"$d/netload.log" 2>&1
    status=0
    (cd "$d"; env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=uart \
        timeout -k 3 20 "$T4" "$sim" -sm 24 -ss -sb test.btl >run.log 2>&1) || status=$?
    [ "$status" -ne 124 ] && [ "$status" -ne 137 ]
    grep -q 'INMOS-RAND PASS' "$d/uart"
    echo "$cpu: captured 128-word sequence and reseed pass"
done
