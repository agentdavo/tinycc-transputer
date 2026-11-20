#!/usr/bin/env bash
# Run the division benchmark with T4's estimated T800 cycle counter.
# Usage: bash transputer/tests/bench/u64_div.sh [runtime-source]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
TCC="${TCC_BIN:-$ROOT/transputer-tcc}"
T4="${T4_BIN:-$ROOT/transputer/t4/t4}"
RUNTIME="${1:-$ROOT/lib/transputer/transputer-runtime.c}"
if ! grep -a -q 'T4_EST_CYCLES' "$T4"; then
    echo "T4 binary lacks the estimated-cycle counter" >&2
    exit 1
fi
"$TCC" -mt800 -static -nostdlib -I"$ROOT/include" -o "$WORK/bench.elf" \
    "$ROOT/transputer/tests/test_boot.S" "$ROOT/transputer/tests/bench/u64_div.c" \
    "$RUNTIME" "$ROOT/transputer-libtcc1-t800.a"
"$ROOT/transputer-netload" "$WORK/bench.elf" "$WORK/bench.btl" >/dev/null 2>&1
env -u DISPLAY T4_EST_CYCLES=1 T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/uart" \
    timeout -k 5 10 "$T4" -s8 -sm 26 -ss -sb "$WORK/bench.btl" >/dev/null 2>&1 || true
if ! test -s "$WORK/uart" || ! tail -c 1 "$WORK/uart" | grep -q H; then
    echo "benchmark did not reach halt" >&2
    exit 1
fi
cat "$WORK/uart"
