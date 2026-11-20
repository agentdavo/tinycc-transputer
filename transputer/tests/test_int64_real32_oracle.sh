#!/usr/bin/env bash
# Diagnostic: report observed results; do not treat mismatches as test passes.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE="$ROOT/transputer/tests"
TCC_DIR=${TCC_DIR:-$ROOT}
T4_BIN=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
python3 "$HERE/int64_real32_oracle.py" "$WORK"
"${HOST_CC:-cc}" -O2 -o "$WORK/host" "$WORK/host.c"
"$WORK/host"
for cpu in t414 t425 t800 t805 t9000 t9000tvx; do
 case "$cpu" in
 t414) mode=-s414; rt=transputer-libtcc1-t414.a; fp=0;;
 t425) mode=-s425; rt=transputer-libtcc1-t425.a; fp=0;;
 t800|t805) mode=-s8; rt=transputer-libtcc1-t800.a; fp=1;;
 *) mode=-s9; rt=transputer-libtcc1.a; fp=1;; esac
 dir="$WORK/$cpu"; mkdir -p "$dir"
 extra=(); if [ "$fp" = 1 ]; then extra=(-DTEST_TC_ENC "$HERE/int64_real32_tcwg.S"); fi
 T4_STRICT_UNRESOLVED_CALLS=1 "$TCC_DIR/transputer-tcc" -m"$cpu" -nostdlib "${extra[@]}" -o "$dir/test.elf" "$ROOT/transputer/tests/test_boot.S" "$WORK/vectors.c" "$TCC_DIR/$rt" > "$dir/link.log" 2>&1
 "$TCC_DIR/transputer-netload" "$dir/test.elf" "$dir/test.btl" > "$dir/netload.log" 2>&1
 (cd "$dir"; DISPLAY= WAYLAND_DISPLAY= T4_UART_CONSOLE=0 T4_UART_LOG=uart timeout -k 2 30 "$T4_BIN" "$mode" -sm 26 -ss -sb test.btl > emu.log 2>&1) || true
 printf '%s: ' "$cpu"; grep -a TOTAL "$dir/uart" || { echo 'NO RESULT'; exit 1; }
done
D="$WORK/cost"; mkdir -p "$D"
T4_STRICT_UNRESOLVED_CALLS=1 "$TCC_DIR/transputer-tcc" -mt800 -nostdlib -o "$D/test.elf" "$ROOT/transputer/tests/test_boot.S" "$ROOT/transputer/coremark/transputer/transputer_ldtimer.S" "$HERE/int64_real32_tcwg.S" "$HERE/int64_real32_cost.c" "$TCC_DIR/transputer-libtcc1-t800.a" > "$D/link.log" 2>&1
"$TCC_DIR/transputer-netload" "$D/test.elf" "$D/test.btl" > "$D/netload.log" 2>&1
(cd "$D"; DISPLAY= WAYLAND_DISPLAY= T4_EST_CYCLES=1 T4_UART_CONSOLE=0 T4_UART_LOG=uart timeout -k 2 20 "$T4_BIN" -s8 -sm 26 -ss -sb test.btl > emu.log 2>&1) || true
cat "$D/uart"
echo "Detailed vectors, UART and emulator logs: $WORK"
