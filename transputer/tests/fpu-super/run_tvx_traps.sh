#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
: "${TCC:?}" "${NETLOAD:?}" "${T4_BIN:?}" "${TVX_TRAP_OUT:?}"
out=$TVX_TRAP_OUT
src=$root/transputer/tests/fpu-super
work=$(mktemp -d /tmp/tvx-soft-traps.XXXXXX)
backend=${TVX_TRAP_BACKEND:-software}
case "$backend" in software) target=t9000;mode=(--tvx-soft);; tvx) target=t9000tvx;mode=();; *) exit 2;; esac
[[ ${TVX_PROTECTED:-0} == 1 ]] && mode+=(--tvx-protected)
python3 "$src/generate.py" --cpu t9000 --tvx-traps "${mode[@]}" --out "$work"
"$TCC" -m"$target" -nostdlib -nostdinc -I"$work" -I"$root/lib/transputer/tvx/include" -DCPU_NAME=\"t9000-$backend\" -DTIMING_UNIT=\"instructions\" -o "$work/test.elf" "$src/startup.S" "$work/probes.S" "$src/main.c" "$src/tvx_soft_exception.c" "$root/lib/transputer/transputer-tvx.c" > "$work/build.log" 2>&1 || { cat "$work/build.log";exit 1; }
"$NETLOAD" "$work/test.elf" "$work/direct.btl" > "$work/netload.log" 2>&1
python3 "$src/link_boot.py" "$work/direct.btl" "$work/link.btl"
failed=0
for boot in direct link;do
 status=0
 (cd "$work"; T4_PROTECT=1 T4_EST_CYCLES=2 T4_HEADLESS=1 SDL_VIDEODRIVER=dummy T4_UART_CONSOLE=0 T4_UART_LOG="$work/$boot.uart" timeout -k 2 120 "$T4_BIN" -s9 -si -sm 18 -sb "$work/$boot.btl" > "$work/$boot.log" 2>&1) || status=$?
 python3 "$src/validate_uart.py" "$work/coverage.json" "$work/$boot.uart" --unit instructions > "$work/$boot.result.json" || failed=1
 cat "$work/$boot.result.json"
done
mkdir -p "$out";cp -a "$work/." "$out/"
exit "$failed"
