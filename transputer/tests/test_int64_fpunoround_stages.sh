#!/usr/bin/env bash
# Diagnostic only: prints observed intermediate and final T800 results.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE="$ROOT/transputer/tests"
TCC_DIR=${TCC_DIR:-$ROOT}
T4_BIN=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
T4_STRICT_UNRESOLVED_CALLS=1 "$TCC_DIR/transputer-tcc" -mt800 -nostdlib -o "$WORK/stages.elf" "$ROOT/transputer/tests/test_boot.S" "$HERE/int64_real32_tcwg.S" "$HERE/int64_fpunoround_stages.S" "$HERE/int64_fpunoround_stages.c" "$TCC_DIR/transputer-libtcc1-t800.a" > "$WORK/link.log" 2>&1
"$TCC_DIR/transputer-netload" "$WORK/stages.elf" "$WORK/stages.btl" > "$WORK/netload.log" 2>&1
(cd "$WORK"; DISPLAY= WAYLAND_DISPLAY= T4_UART_CONSOLE=0 T4_UART_LOG=uart timeout -k 2 20 "$T4_BIN" -s8 -sm 26 -ss -sb stages.btl > emu.log 2>&1) || true
grep -q 'I00000002' "$WORK/uart" && grep -q 'N00000007' "$WORK/uart" || { echo 'Incomplete staged probe' >&2; exit 1; }
cat "$WORK/uart"
printf '\nLogs: %s\n' "$WORK"
