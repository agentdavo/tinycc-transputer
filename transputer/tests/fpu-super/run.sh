#!/bin/bash
# Strict: known emulator defects remain failures, never accepted as passing.
set -euo pipefail
src=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$src/../../.." && pwd)
out=${FPU_SUPER_OUT:-$root/logs/fpu-super}
t4=${T4_BIN:-$root/transputer/t4/t4}
timing=$(cat "$out/timing-mode")
case "$timing" in ticks) counter=0;; instructions) counter=2;; t800_model_cycles) counter=1;; *) exit 2;; esac
failed=0
cpus=(t805 t9000)
if [[ -f $out/cpu-list ]]; then
    mapfile -t cpus < "$out/cpu-list"
elif [[ -f $out/t9000tvx/bf16-manifest.json ]]; then
    cpus+=(t9000tvx)
fi
for cpu in "${cpus[@]}"; do
    [[ $timing == t800_model_cycles && $cpu != t805 ]] && continue
    mode=-s8; [[ $cpu != t805 ]] && mode=-s9
    for format in direct link; do
        name=fpu-super-$cpu-$format
        status=0
        (cd "$out"; T4_PROTECT=1 T4_EST_CYCLES="$counter" T4_HEADLESS=1 SDL_VIDEODRIVER=dummy T4_UART_CONSOLE=0 T4_UART_LOG="$out/$name.uart" timeout -k 2 "${FPU_TIMEOUT:-600}" "$t4" "$mode" -si -sm 18 -sb "$out/$name.btl" > "$out/$name.run.log" 2>&1) || status=$?
        echo "$name emulator exit=$status"
        # Do not use -se: fpchkerr deliberately transfers an FP error to Error.
        python3 "$src/validate_uart.py" "$out/$cpu/coverage.json" "$out/$name.uart" --unit "$timing" > "$out/$name.result.json" || failed=1
        cat "$out/$name.result.json"
    done
done
exit "$failed"
