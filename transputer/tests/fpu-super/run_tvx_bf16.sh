#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
: "${TCC:?}" "${NETLOAD:?}" "${T4_BIN:?}" "${TVX_ARITH_OUT:?}"
out=$TVX_ARITH_OUT
work=$(mktemp -d /tmp/tvx-arithmetic.XXXXXX)
for path in software tvx; do
 target=t9000; runtime=("$root/lib/transputer/transputer-tvx.c"); [[ $path == tvx ]] && { target=t9000tvx; runtime=(); }
 dir=$work/$path;mkdir -p "$dir"
 "$TCC" -m"$target" -nostdlib -nostdinc -I"$root/lib/transputer/tvx/include" -DTVX_ARITH_STANDALONE -o "$dir/test.elf" "$root/transputer/tests/fpu-super/startup.S" "$root/transputer/tests/fpu-super/tvx_bf16_arithmetic.c" "${runtime[@]}" > "$dir/build.log" 2>&1
 python3 "$root/transputer/tests/fpu-super/audit_tvx_bf16.py" "$dir/test.elf" --backend "$path"
 "$NETLOAD" "$dir/test.elf" "$dir/direct.btl" > "$dir/netload.log" 2>&1
 python3 "$root/transputer/tests/fpu-super/link_boot.py" "$dir/direct.btl" "$dir/link.btl"
 for boot in direct link; do
  status=0
  (cd "$dir"; T4_PROTECT=1 T4_EST_CYCLES=2 T4_HEADLESS=1 SDL_VIDEODRIVER=dummy T4_UART_CONSOLE=0 T4_UART_LOG="$dir/$boot.uart" timeout -k 2 120 "$T4_BIN" -s9 -si -sm 18 -sb "$dir/$boot.btl" > "$dir/$boot.log" 2>&1) || status=$?
  echo "$path $boot exit=$status"
  python3 "$root/transputer/tests/fpu-super/validate_tvx_bf16.py" "$root/transputer/tests/fpu-super/tvx-bf16-reference.json" "$dir/$boot.uart"
 done
done
mkdir -p "$out";cp -a "$work/." "$out/"
